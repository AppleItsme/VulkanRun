#include "vk.h"

#include <vulkan/vulkan.h>

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
	VkCommandPool pool;
	VkCommandBuffer buffer;
	VkSemaphore swapchainSemaphore, renderSemaphore;
	VkFence renderFence;
} QueueCommand;

#define FRAME_OVERLAP 2

struct Engine {
	size_t testNumber;
    VkDevice device;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;

    VkSurfaceKHR surface;
    struct {
		uint32_t graphics;
		uint32_t presentation;
		uint32_t compute;
		uint32_t transfer;
	} queue_indices;
	struct {
		VkQueue graphics;
		VkQueue presentation;
		VkQueue compute;
		VkQueue transfer;
	} queues;
    struct {
		VkSurfaceCapabilitiesKHR capabilities;
		VkSurfaceFormatKHR format;
		VkPresentModeKHR presentMode;
	} swapchainDetails;
	VkSwapchainKHR swapchain, oldSwapchain;
	VkImage *swapchainImages;
	VkImageView *swapchainImageViews;
	uint32_t swapchainImageCount;
	VkExtent2D pixelResolution;
	
	uint32_t cur_frame;
	struct {
		QueueCommand graphics[FRAME_OVERLAP],
					compute,
					transfer;
	} QueueCommands;
};

void updateCurrentFrame_(Engine *engine) {
	engine->cur_frame++;
	if(engine->cur_frame >= FRAME_OVERLAP) {
		engine->cur_frame = 0;
	}
}

typedef enum {
    ENGINE_GRAPHICS,
    ENGINE_PRESENT,
    ENGINE_COMPUTE
} CommandPoolType;

inline uint32_t clampU32(uint32_t val, uint32_t min, uint32_t max) {
	val = val < min ? min : val;
	return val > max ? max : val;
}

#define ERR_CHECK(condition, engineCode, vulkanCode)\
	if(!(condition)) \
		return (EngineResult) {engineCode, vulkanCode}
#define ENGINE_RESULT_SUCCESS (EngineResult) {SUCCESS, VK_SUCCESS}


// const char *instanceExtensions[] = {
// 	""
// };
// const size_t instanceExtensionsCount = sizeof(instanceExtensions)/sizeof(char*);

const char *deviceExtensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};
#define ARR_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))

#ifndef NDEBUG

const char *validationLayers[] = {"VK_LAYER_KHRONOS_validation"};

bool checkValidationSupport() {
	uint32_t layerCount = 0;
	vkEnumerateInstanceLayerProperties(&layerCount, NULL);
	VkLayerProperties *layerProps = malloc(sizeof(VkLayerProperties) * layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, layerProps);
	
	bool found = false;

	for(int i = 0; i < layerCount; i++) {
		if(strcmp(layerProps[i].layerName, validationLayers[0])) {
			found = true;
			break;
		}
	}
	free(layerProps);
	return found;
}

#endif

VkResult res;

EngineResult findSuitablePhysicalDevice(VkPhysicalDevice *devices, size_t deviceCount, Engine *engine) {
	size_t queueMemSize = 0,
			extensionsMemSize = 0,
			formatMemSize = 0,
			presentMemSize = 0;
	VkQueueFamilyProperties *queueProps = NULL;
	VkExtensionProperties *extensionProps = NULL;
	VkSurfaceFormatKHR *formats = NULL;
	bool suitableDeviceFound = false;

	for(int i = 0; i < deviceCount; i++) {
		VkPhysicalDeviceProperties props = {0};
		vkGetPhysicalDeviceProperties(devices[i], &props);
		printf("Device Name: %s\n", props.deviceName);

		size_t graphicsI = -1, presentationI = -1, computeI = -1, transferI = -1;
		//First checking for queue capabilities; if they are not sufficient then the rest doesnt even matter
		size_t queuePropCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queuePropCount, NULL);
		if(queuePropCount == 0)
			continue;
		if(queuePropCount > queueMemSize) {
			if(queueProps == NULL) {
				queueProps = malloc(sizeof(VkQueueFamilyProperties) * queuePropCount);
			} else {
				queueProps = realloc(queueProps, sizeof(VkQueueFamilyProperties) * queuePropCount);
			}
			queueMemSize = queuePropCount;
		}
		vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queuePropCount, queueProps);
		for(int j = 0; j < queuePropCount; j++) {
			if(queueProps[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				graphicsI = j;
			}
			if(queueProps[j].queueFlags & VK_QUEUE_COMPUTE_BIT) {
				computeI = j;
			}
			if(queueProps[j].queueFlags & VK_QUEUE_TRANSFER_BIT) {
				transferI = j;
			}
			VkBool32 presentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], j, engine->surface, &presentSupport);
			if(presentSupport)
				presentationI = j;
			if(graphicsI > -1 && presentationI > -1 && computeI > -1)
				break;
		}
		if(graphicsI == -1 || presentationI == -1 || computeI == -1 || transferI == -1) {
			continue;
		}

		uint32_t extensionCount = 0;
		vkEnumerateDeviceExtensionProperties(devices[i], NULL, &extensionCount, NULL);
		if(extensionCount == 0)
			continue;
		if(extensionCount > extensionsMemSize) {
			if(extensionProps == NULL) {
				extensionProps = malloc(sizeof(VkExtensionProperties) * extensionCount);
			} else {
				extensionProps = realloc(extensionProps, sizeof(VkExtensionProperties) * extensionCount);
			}
			extensionsMemSize = extensionCount;
		}
		vkEnumerateDeviceExtensionProperties(devices[i], NULL, &extensionCount, extensionProps);

		size_t found = 0;
		for(int j = 0; j < extensionCount; j++) {
			for(int k = 0; k < ARR_SIZE(deviceExtensions); k++) {
				if(strcmp(deviceExtensions[k], extensionProps[j].extensionName)) {
					found++;
					break;
				}
			}
		}
		
		if(found < ARR_SIZE(deviceExtensions)) {
			continue;
		}
		VkPhysicalDeviceVulkan12Features desiredFeatures12 = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
			.pNext = NULL
		};
		VkPhysicalDeviceVulkan13Features desiredFeatures13 = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
			.pNext = &desiredFeatures12
		};
		VkPhysicalDeviceFeatures2 deviceFeatures = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = &desiredFeatures13
		};
		vkGetPhysicalDeviceFeatures2(devices[i], &deviceFeatures);

		size_t formatCount = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(devices[i], engine->surface, &formatCount, NULL);
		if(formatCount == 0)
			continue;
		if(formatCount > formatMemSize) {
			if(formats == NULL) {
				formats = malloc(sizeof(VkSurfaceFormatKHR) * formatCount);
			} else {
				formats = realloc(formats, sizeof(VkSurfaceFormatKHR) * formatCount);
			}
			formatMemSize = formatCount;
		}
		vkGetPhysicalDeviceSurfaceFormatsKHR(devices[i], engine->surface, &formatCount, formats);
		int32_t formatIndex = -1;
		for(int i = 0; i < formatCount; i++) {
			if(formats[i].format == VK_FORMAT_B8G8R8A8_SRGB && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				formatIndex = i;
				break;
			}
		}
		if(formatIndex == -1)
			continue;
		if(desiredFeatures13.dynamicRendering && 
			desiredFeatures13.synchronization2 &&
			desiredFeatures12.bufferDeviceAddress &&
			desiredFeatures12.descriptorIndexing) {

			engine->physicalDevice = devices[i];

			engine->queue_indices.graphics = graphicsI;
			engine->queue_indices.compute = computeI;
			engine->queue_indices.presentation = presentationI;
			engine->queue_indices.transfer = transferI;

			engine->swapchainDetails.format = formats[formatIndex];
			engine->swapchainDetails.presentMode = VK_PRESENT_MODE_FIFO_KHR;
			suitableDeviceFound = true;
			if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
				break;
			}
		}
	}
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(engine->physicalDevice, &props);
	printf("\x1b[1;37mThe chosen device: %s\n\x1b[0m", props.deviceName);
	free(queueProps);
	free(extensionProps);
	free(formats);
	ERR_CHECK(suitableDeviceFound, INAPPROPRIATE_GPU, VK_SUCCESS);
	return ENGINE_RESULT_SUCCESS;
}

EngineResult EngineSwapchainCreate(Engine *engine, uint32_t frameBufferWidth, uint32_t frameBufferHeight) {
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(engine->physicalDevice, engine->surface, &engine->swapchainDetails.capabilities);
	
	engine->pixelResolution.width = clampU32(frameBufferWidth, engine->swapchainDetails.capabilities.minImageExtent.width, 
													engine->swapchainDetails.capabilities.maxImageExtent.width);
	engine->pixelResolution.height = clampU32(frameBufferHeight, engine->swapchainDetails.capabilities.minImageExtent.height, 
													engine->swapchainDetails.capabilities.maxImageExtent.height);
	uint32_t imageCount = engine->swapchainDetails.capabilities.minImageCount + 1;
	if(engine->swapchainDetails.capabilities.maxImageCount > 0) {
		imageCount = clampU32(imageCount, imageCount, engine->swapchainDetails.capabilities.maxImageCount);
	}
	VkSwapchainCreateInfoKHR swapchainCI = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = engine->surface,
		.imageFormat = engine->swapchainDetails.format.format,
		.imageColorSpace = engine->swapchainDetails.format.colorSpace,
		.presentMode = engine->swapchainDetails.presentMode,
		.imageExtent = engine->pixelResolution,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		.preTransform = engine->swapchainDetails.capabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.clipped = VK_TRUE,
		.oldSwapchain = engine->oldSwapchain,
		.minImageCount = imageCount
	};
	
	//MIGHT change it to compute instead of graphics
	uint32_t queueFamilyIndices[2] = {engine->queue_indices.graphics, engine->queue_indices.presentation};
	swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if(engine->queue_indices.graphics == engine->queue_indices.presentation) {
		swapchainCI.queueFamilyIndexCount = 2;
		swapchainCI.pQueueFamilyIndices = &queueFamilyIndices;
	}

	res = vkCreateSwapchainKHR(engine->device, &swapchainCI, NULL, &engine->swapchain);
	ERR_CHECK(res == VK_SUCCESS, SWAPCHAIN_FAILED, res);

	vkGetSwapchainImagesKHR(engine->device, engine->swapchain, &engine->swapchainImageCount, NULL);
	engine->swapchainImages = malloc(sizeof(VkImage) * engine->swapchainImageCount);
	vkGetSwapchainImagesKHR(engine->device, engine->swapchain, &engine->swapchainImageCount, engine->swapchainImages);

	engine->swapchainImageViews = malloc(sizeof(VkImageView) * engine->swapchainImageCount);
	for(int i = 0; i < engine->swapchainImageCount; i++) {
		VkImageViewCreateInfo imageViewCI = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = engine->swapchainImages[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = engine->swapchainDetails.format.format,
			.components = {VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY},
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			},
		};
		vkCreateImageView(engine->device, &imageViewCI, NULL, &engine->swapchainImageViews[i]);
	}
	return ENGINE_RESULT_SUCCESS;
}

void EngineSwapchainDestroy(Engine *engine) {
	for(int i = 0; i < engine->swapchainImageCount; i++) {
		vkDestroyImageView(engine->device, engine->swapchainImageViews[i], NULL);
	}
	free(engine->swapchainImages);
	free(engine->swapchainImageViews);
	vkDestroySwapchainKHR(engine->device, engine->swapchain, NULL);
}

EngineResult QueueCommandCreate(Engine *engine, CommandPoolType type, QueueCommand *queueCommand) {
	uint32_t index = 0;
	switch(type) {
	case ENGINE_GRAPHICS:
		index = engine->queue_indices.graphics;
		break;
	case ENGINE_COMPUTE:
		index = engine->queue_indices.compute;
		break;
	case ENGINE_PRESENT:
		index = engine->queue_indices.presentation;
		break;
	}
	VkCommandPoolCreateInfo commandPoolCI = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.pNext = NULL,
		.queueFamilyIndex = index
	};
	res = vkCreateCommandPool(engine->device, &commandPoolCI, NULL, &queueCommand->pool);
	printf("Pool address: %d\n", queueCommand->pool);
	ERR_CHECK(res == VK_SUCCESS, QUEUECOMMAND_CREATION_FAILED, res);

	return ENGINE_RESULT_SUCCESS;
}

EngineResult QueueCommandAllocate(Engine *engine, QueueCommand *queueCommand) {
	VkCommandBufferAllocateInfo bufferAllocateInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = queueCommand->pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
		.pNext = NULL
	};
	res = vkAllocateCommandBuffers(engine->device, &bufferAllocateInfo, &queueCommand->buffer);
	ERR_CHECK(res == VK_SUCCESS, QUEUECOMMAND_ALLOCATION_FAILED, res);
	return ENGINE_RESULT_SUCCESS;
}

EngineResult TransferImage(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
	printf("Transferring Image\n");
	VkImageSubresourceRange subresourceRange = {
		.aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, //idk why but for now i keep
		.baseMipLevel = 0,
		.levelCount = VK_REMAINING_MIP_LEVELS,
		.baseArrayLayer = 0,
		.layerCount = VK_REMAINING_ARRAY_LAYERS
	};
	VkImageMemoryBarrier2 imageBarrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.pNext = NULL,
		.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
		.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
		.oldLayout = oldLayout,
		.newLayout = newLayout,
		.subresourceRange = subresourceRange,
		.image = image
	};
	VkDependencyInfo depInfo = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.pNext = NULL,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &imageBarrier
	};
	vkCmdPipelineBarrier2(cmd, &depInfo);
	return ENGINE_RESULT_SUCCESS;
}

EngineResult EngineDraw(Engine *engine, EngineColor background) {
	EngineResult eRes = {0};
	eRes = QueueCommandAllocate(engine, &engine->QueueCommands.graphics[engine->cur_frame]);
	ERR_CHECK(eRes.EngineCode == SUCCESS, eRes.EngineCode, eRes.VulkanCode);

	res = vkWaitForFences(engine->device, 1, &engine->QueueCommands.graphics[engine->cur_frame].renderFence, true, 1000000000);
	ERR_CHECK(res == VK_SUCCESS, FENCE_NOT_WORKING, res);
	res = vkResetFences(engine->device, 1, &engine->QueueCommands.graphics[engine->cur_frame].renderFence);
	ERR_CHECK(res == VK_SUCCESS, FENCE_NOT_WORKING, res);
	
	uint32_t swapchainImageIndex = 0;
	vkAcquireNextImageKHR(engine->device, engine->swapchain, 1000000000, engine->QueueCommands.graphics[engine->cur_frame].swapchainSemaphore, NULL, &swapchainImageIndex);
	
	// vkCreateImage(engine->device, )
	VkCommandBufferBeginInfo beginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = NULL,
		.pNext = NULL
	};

	VkImageSubresourceRange backgroundSubresourceRange = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1
	};
	vkResetCommandBuffer(engine->QueueCommands.graphics[engine->cur_frame].buffer, 0);
	vkBeginCommandBuffer(engine->QueueCommands.graphics[engine->cur_frame].buffer, &beginInfo);
	VkClearColorValue backgroundColor = {
		.float32 = {background.r, background.g, background.b, background.a}
	};
	TransferImage(engine->QueueCommands.graphics[engine->cur_frame].buffer, engine->swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkCmdClearColorImage(engine->QueueCommands.graphics[engine->cur_frame].buffer, engine->swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_GENERAL, &backgroundColor, 1, &backgroundSubresourceRange);
	/*more drawing code will go here*/
	TransferImage(engine->QueueCommands.graphics[engine->cur_frame].buffer, engine->swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	res = vkEndCommandBuffer(engine->QueueCommands.graphics[engine->cur_frame].buffer);
	ERR_CHECK(res == VK_SUCCESS, CANNOT_PREPARE_FOR_SUBMISSION, res);

	VkCommandBufferSubmitInfo cmdSubmitInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = engine->QueueCommands.graphics[engine->cur_frame].buffer,
		.deviceMask = 0,
		.pNext = NULL
	};

	VkSemaphoreSubmitInfo waitInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.deviceIndex = 0,
		.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		.semaphore = engine->QueueCommands.graphics[engine->cur_frame].swapchainSemaphore
	}, signalInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.deviceIndex = 0,
		.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		.semaphore = engine->QueueCommands.graphics[engine->cur_frame].renderSemaphore
	};

	VkSubmitInfo2 queueSubmitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &cmdSubmitInfo,
		.signalSemaphoreInfoCount = 1,
		.pSignalSemaphoreInfos = &signalInfo,
		.waitSemaphoreInfoCount = 1,
		.pWaitSemaphoreInfos = &waitInfo,
		.flags = 0
	};

	res = vkQueueSubmit2(engine->queues.graphics, 1, &queueSubmitInfo, engine->QueueCommands.graphics[engine->cur_frame].renderFence);
	ERR_CHECK(res == VK_SUCCESS, CANNOT_SUBMIT_TO_GPU, res);
	
	VkPresentInfoKHR presentInfo = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.pNext = NULL,
		.swapchainCount = 1,
		.pSwapchains = &engine->swapchain,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &engine->QueueCommands.graphics[engine->cur_frame].renderSemaphore,
		.pImageIndices = &swapchainImageIndex,
	};
	res = vkQueuePresentKHR(engine->queues.graphics, &presentInfo);
	ERR_CHECK(res == VK_SUCCESS, CANNOT_DISPLAY, res);	
	updateCurrentFrame_(engine);
	
	// glfwPollEvents();
	
	// VkCommandBufferBeginInfo transferBeginInfo = {
	// 	.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	// 	.flags = 0,
	// 	.pInheritanceInfo = NULL,
	// 	.pNext = NULL
	// };
	// vkBeginCommandBuffer(engine->QueueCommands.transfer.buffer, &transferBeginInfo);
	// vkCmdCopyImage(engine->QueueCommands.transfer.buffer, cur_image, )

	return ENGINE_RESULT_SUCCESS;
}

EngineResult EngineInit(Engine **engine_instance, EngineCI engineCI, uintptr_t *vkInstance) {
	Engine *engine = malloc(sizeof(Engine));
	*engine_instance = engine;
	
	engine->oldSwapchain = VK_NULL_HANDLE;

	#ifndef NDEBUG
	ERR_CHECK(checkValidationSupport(), DEBUG_CREATION_FAILED, VK_SUCCESS);
	#endif

	VkApplicationInfo appInfo = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.apiVersion = VK_API_VERSION_1_3,
		.applicationVersion = engineCI.appVersion,
		.pApplicationName = engineCI.appName,
		.engineVersion = VK_MAKE_VERSION(1,0,0),
		.pEngineName = "vulkanik",
	};

	VkInstanceCreateInfo instanceCI = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &appInfo,
		.enabledExtensionCount = engineCI.extensionsCount,
		.ppEnabledExtensionNames = engineCI.extensions,
		#ifndef NDEBUG
		.enabledLayerCount = 1,
		.ppEnabledLayerNames = validationLayers,
		#else
		.enabledLayerCount = 0,
		.ppEnabledLayerNames = NULL,
		#endif
		.pNext = NULL,
		.flags = 0
	};
	ERR_CHECK(engine != NULL, OUT_OF_MEMORY, VK_SUCCESS);
	engine->instance = VK_NULL_HANDLE;
	res = vkCreateInstance(&instanceCI, NULL, &engine->instance);
	ERR_CHECK(res == VK_SUCCESS, INSTANCE_CREATION_FAILED, res);
	printf("Vulkan Instance created successfully\n");
	*vkInstance = engine->instance;
	return ENGINE_RESULT_SUCCESS;
};

EngineResult EngineFinishSetup(Engine *engine, uintptr_t surface) {
	engine->surface = surface;
	engine->physicalDevice = VK_NULL_HANDLE;
	size_t physicalDeviceCount = 0;
	vkEnumeratePhysicalDevices(engine->instance, &physicalDeviceCount, NULL);
	printf("Physical Devices Found: %d\n", physicalDeviceCount);
	ERR_CHECK(physicalDeviceCount > 0, GPU_NOT_FOUND, VK_SUCCESS);
	VkPhysicalDevice *physicalDevices = malloc(physicalDeviceCount * sizeof(VkPhysicalDevice));
	vkEnumeratePhysicalDevices(engine->instance, &physicalDeviceCount, physicalDevices);
	
	printf("picking physical device\n");
	EngineResult eRes = findSuitablePhysicalDevice(physicalDevices, physicalDeviceCount, engine);
	ERR_CHECK(eRes.EngineCode == SUCCESS, eRes.EngineCode, eRes.VulkanCode);
	printf("Physical Device Chosen\n");
	free(physicalDevices);

	engine->swapchainImageCount = engine->swapchainDetails.capabilities.minImageCount + 1;

	float queuePriority = 1;
	uint32_t *queueI = (void*)(&engine->queue_indices);
	size_t queueCI_len = 1;

	#define QUEUECOUNT sizeof(engine->queue_indices)/sizeof(uint32_t)
	uint32_t uniqueQueueI[QUEUECOUNT] = {queueI[0]};

	VkDeviceQueueCreateInfo queueCI[QUEUECOUNT] = {
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.pNext = NULL,
			.pQueuePriorities = &queuePriority,
			.queueCount = 1,
			.queueFamilyIndex = queueI[0]
		}
	};
	for(int i = 1; i < QUEUECOUNT; i++) { //yes i'm looping it like an array, tho i prefer to keep it in class as struct
		bool isUnique = true;
		for(int j = 0; j < queueCI_len; j++) {
			if(uniqueQueueI[j] == queueI[i]) {
				isUnique = false;
				break;
			}	
		}
		if(!isUnique) {
			continue;
		}
		queueCI[queueCI_len] = (VkDeviceQueueCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.pNext = NULL,
			.pQueuePriorities = &queuePriority,
			.queueCount = 1,
			.queueFamilyIndex = queueI[i]
		};
		uniqueQueueI[queueCI_len] = queueI[i];
		queueCI_len++; 
	}

	VkPhysicalDeviceVulkan12Features desiredFeatures12 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = NULL,
		.bufferDeviceAddress = true,
		.descriptorIndexing = true
	};
	VkPhysicalDeviceVulkan13Features desiredFeatures13 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.dynamicRendering = true,
		.synchronization2 = true,
		.pNext = &desiredFeatures12
	};
	VkPhysicalDeviceFeatures2 deviceFeatures = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &desiredFeatures13,
		.features = {0}
	};
	VkDeviceCreateInfo deviceCI = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.enabledExtensionCount = ARR_SIZE(deviceExtensions),
		.ppEnabledExtensionNames = deviceExtensions,
		.queueCreateInfoCount = queueCI_len,
		.pQueueCreateInfos = queueCI,
		.pEnabledFeatures = NULL,
		.pNext = &deviceFeatures,
		#ifndef NDEBUG
		.enabledLayerCount = 1,
		.ppEnabledLayerNames = validationLayers
		#else
		.enabledLayerCount = 0,
		#endif
	};

	res = vkCreateDevice(engine->physicalDevice, &deviceCI, NULL, &engine->device);
	ERR_CHECK(res == VK_SUCCESS, DEVICE_CREATION_FAILED, res);
	vkGetDeviceQueue(engine->device, engine->queue_indices.graphics, 0, &engine->queues.graphics);
	vkGetDeviceQueue(engine->device, engine->queue_indices.compute, 0, &engine->queues.compute);
	vkGetDeviceQueue(engine->device, engine->queue_indices.presentation, 0, &engine->queues.presentation);
	vkGetDeviceQueue(engine->device, engine->queue_indices.transfer, 0, &engine->queues.transfer);

	engine->cur_frame = 0;

	VkFenceCreateInfo fenceCI = {
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = NULL
	};

	VkSemaphoreCreateInfo semaphoreCI = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.flags = 0,
		.pNext = NULL,
	};

	for(int i = 0; i < FRAME_OVERLAP; i++) {
		EngineResult eRes = QueueCommandCreate(engine, ENGINE_GRAPHICS, &engine->QueueCommands.graphics[i]);
		ERR_CHECK(eRes.EngineCode == SUCCESS, eRes.EngineCode, eRes.VulkanCode);
		res = vkCreateFence(engine->device, &fenceCI, NULL, &engine->QueueCommands.graphics[i].renderFence);
		ERR_CHECK(res == VK_SUCCESS, FENCE_CREATION_FAILED, res);
		res = vkCreateSemaphore(engine->device, &semaphoreCI, NULL, &engine->QueueCommands.graphics[i].renderSemaphore);
		ERR_CHECK(res == VK_SUCCESS, SEMAPHORE_CREATION_FAILED, res);
		res = vkCreateSemaphore(engine->device, &semaphoreCI, NULL, &engine->QueueCommands.graphics[i].swapchainSemaphore);
		ERR_CHECK(res == VK_SUCCESS, SEMAPHORE_CREATION_FAILED, res);
	}
}

void EngineDestroy(Engine *engine) {
	vkDeviceWaitIdle(engine->device);

	for(int i = 0; i < FRAME_OVERLAP; i++) {
		vkDestroyFence(engine->device, engine->QueueCommands.graphics[i].renderFence, NULL);
		vkDestroySemaphore(engine->device, engine->QueueCommands.graphics[i].renderSemaphore, NULL);
		vkDestroySemaphore(engine->device, engine->QueueCommands.graphics[i].swapchainSemaphore, NULL);
	}

	// vkDestroyCommandPool(engine->device, engine->QueueCommands.transfer.pool, NULL);
	vkDestroyCommandPool(engine->device, engine->QueueCommands.graphics[0].pool, NULL);
	vkDestroyCommandPool(engine->device, engine->QueueCommands.graphics[1].pool, NULL);
	// vkDestroyCommandPool(engine->device, engine->QueueCommands.compute.pool, NULL);

	vkDestroyDevice(engine->device, NULL);
	vkDestroySurfaceKHR(engine->instance, engine->surface, NULL);
	vkDestroyInstance(engine->instance, NULL);
	free(engine);
}