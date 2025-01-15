#include <Engine.h>

#include <vulkan/vulkan.h>

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <utils.h>

#include <vk_mem_alloc.h>

typedef struct {
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
} AllocatedImage;

#define FRAME_OVERLAP 2

typedef struct {
	VkQueue queue;
	uint32_t index;
	VkCommandPool pool;
} EngineQueue;


struct Engine {
	size_t testNumber;
    VkDevice device;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
	bool hardwareRayTracing;

    VkSurfaceKHR surface;
	EngineQueue graphics, compute, presentation;

    struct {
		VkSurfaceCapabilitiesKHR capabilities;
		VkSurfaceFormatKHR format;
		VkPresentModeKHR presentMode;
	} swapchainDetails;
	VkSwapchainKHR swapchain, oldSwapchain;
	VkImage *swapchainImages;
	VkImageView *swapchainImageViews;
	uint32_t swapchainImageCount;
	VkFence frameFence[FRAME_OVERLAP];

	VkExtent2D pixelResolution;

	VkDeviceSize deviceMinimumOffset;

	uint32_t cur_frame, cur_swapchainIndex;
	VmaAllocator allocator;

	AllocatedImage renderImages[FRAME_OVERLAP];

	VkSemaphore swapchainSemaphores[FRAME_OVERLAP],
				frameReadySemaphore[FRAME_OVERLAP],
				bufferCopySemaphore[FRAME_OVERLAP],
				tmpSemaphore[FRAME_OVERLAP];
	/*Constant command buffers*/
	VkCommandBuffer backgroundBuffer[FRAME_OVERLAP], copyBuffer[FRAME_OVERLAP];
	
	size_t shaderModulesCount;
	VkShaderModule *shaderModules;
	
	VkPipelineLayout pipelineLayout;
	VkPipeline *pipelines;

	VkDescriptorSetLayout descriptorSetLayout;

	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet[FRAME_OVERLAP];
};

uint32_t EngineGetFrame(Engine *engine) {
	return engine->cur_frame;
}

uint32_t NextFrame(Engine *engine) {
	uint32_t cur_frame = engine->cur_frame;
	cur_frame++;
	if(cur_frame >= FRAME_OVERLAP)
		cur_frame = 0;
	return cur_frame;
}

void updateCurrentFrame_(Engine *engine) {
	engine->cur_frame = NextFrame(engine);
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
		return (EngineResult) {engineCode, vulkanCode} \

#define ENGINE_RESULT_SUCCESS (EngineResult) {ENGINE_SUCCESS, VK_SUCCESS}

#define MandatoryDeviceExtensionsCount 1

const char *deviceExtensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,

	VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
	VK_KHR_RAY_QUERY_EXTENSION_NAME,
	VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
	VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
};

#define ARR_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))
#define ALLOWED_SWAPCHAIN_RECREATION_LOOP 2

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

typedef struct {
	uint32_t point;
	uint32_t graphicsI, presentationI, computeI;
	bool supportsRayTracing;
	VkSurfaceFormatKHR format;
	VkPhysicalDevice device;
	VkDeviceSize minimumOffset;
} deviceStats;

VkImageCreateInfo imageCreateInfo(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent) {
    VkImageCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.pNext = NULL,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = extent,
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.flags = 0,
	};

    return info;
}
VkImageViewCreateInfo imageViewCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags) {
    // build a image-view for the depth image to use for rendering
    VkImageViewCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.pNext = NULL;

    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.image = image;
    info.format = format;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    info.subresourceRange.aspectMask = aspectFlags;

    return info;
}
void ImageCopy(VkCommandBuffer cmd, VkImage src, VkImage dst, VkExtent2D srcSize, VkExtent2D dstSize) {
	VkImageSubresourceLayers subresourceLayers = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseArrayLayer = 0,
		.mipLevel = 0,
		.layerCount = 1
	};
	VkImageBlit2 blitRegion = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
		.pNext = NULL,
		.srcOffsets[1] = (VkOffset3D) {
			.x = srcSize.width,
			.y = srcSize.height,
			.z = 1
		},
		.dstOffsets[1] = (VkOffset3D) {
			.x = dstSize.width,
			.y = dstSize.height,
			.z = 1
		},
		.srcSubresource = subresourceLayers,
		.dstSubresource = subresourceLayers
	};
	VkBlitImageInfo2 blitImageInfo = {
		.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
		.dstImage = dst,
		.srcImage = src,
		.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.filter = VK_FILTER_LINEAR,
		.regionCount = 1,
		.pRegions = &blitRegion
	};
	vkCmdBlitImage2(cmd, &blitImageInfo);
}

EngineResult findSuitablePhysicalDevice(VkPhysicalDevice *devices, size_t deviceCount, Engine *engine) {
	size_t queueMemSize = 0,
			extensionsMemSize = 0,
			formatMemSize = 0,
			presentMemSize = 0;
	VkQueueFamilyProperties *queueProps = NULL;
	VkExtensionProperties *extensionProps = NULL;
	VkSurfaceFormatKHR *formats = NULL;

	deviceStats bestDeviceStats = {.point = 0, .device = VK_NULL_HANDLE};

	for(int i = 0; i < deviceCount; i++) {
		deviceStats cur_deviceStats = {.point = 0, .device = devices[i], .supportsRayTracing = false};
		VkPhysicalDeviceProperties props = {0};
		vkGetPhysicalDeviceProperties(devices[i], &props);
		debug_msg("Device %d: %s\n", i, props.deviceName);

		cur_deviceStats.graphicsI = -1; 
		cur_deviceStats.presentationI = -1; 
		cur_deviceStats.computeI = -1; 
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
				cur_deviceStats.graphicsI = j;
			}
			if(queueProps[j].queueFlags & VK_QUEUE_COMPUTE_BIT) {
				cur_deviceStats.computeI = j;
			}
			VkBool32 presentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], j, engine->surface, &presentSupport);
			if(presentSupport)
				cur_deviceStats.presentationI = j;
			if(cur_deviceStats.graphicsI > -1 && cur_deviceStats.presentationI > -1 && cur_deviceStats.computeI > -1)
				break;
		}
		if(cur_deviceStats.graphicsI == -1 || cur_deviceStats.presentationI == -1 || cur_deviceStats.computeI == -1) {
			continue;
		}

		VkPhysicalDeviceVulkan12Features desiredFeatures12 = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
			.pNext = NULL,
		};
		VkPhysicalDeviceVulkan13Features desiredFeatures13 = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
			.pNext = &desiredFeatures12,
		};
		VkPhysicalDeviceFeatures2 deviceFeatures = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = &desiredFeatures13,
		};
		vkGetPhysicalDeviceFeatures2(devices[i], &deviceFeatures);
		if(!(desiredFeatures13.dynamicRendering && 
			desiredFeatures13.synchronization2 &&
			desiredFeatures12.bufferDeviceAddress &&
			desiredFeatures12.descriptorIndexing)) {
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

		size_t found = 0, rayTraceSupport = 0;
		for(int j = 0; j < extensionCount; j++) {
			for(int k = 0; k < MandatoryDeviceExtensionsCount; k++) {
				if(!strcmp(deviceExtensions[k], extensionProps[j].extensionName)) {
					debug_msg("\tFound extension: %s\n", extensionProps[j].extensionName);
					found++;
					break;
				}
			}
			for(int k = MandatoryDeviceExtensionsCount; k < ARR_SIZE(deviceExtensions); k++) {
				if(!strcmp(deviceExtensions[k], extensionProps[j].extensionName)) {
					debug_msg("\tFound extension: %s\n", extensionProps[j].extensionName);
					rayTraceSupport++;
					break;
				}
			}
		}
		if(found < MandatoryDeviceExtensionsCount) {
			continue;
		}
		if(rayTraceSupport == ARR_SIZE(deviceExtensions) - MandatoryDeviceExtensionsCount) {
			debug_msg("\tSupports raytracing\n");
			cur_deviceStats.point++;
			cur_deviceStats.supportsRayTracing = true;
		}

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
		bool goodFormat = false;
		for(int i = 0; i < formatCount; i++) {
			if(formats[i].format == VK_FORMAT_R8G8B8A8_SRGB && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				goodFormat = true;
				cur_deviceStats.format = formats[i];
				break;
			}
		}
		if(!goodFormat)
			continue;
		cur_deviceStats.point++; //make it strictly better than a 0 point
		if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
			cur_deviceStats.point++;
		}
		debug_msg("\tDevice Passed with points: %d\n", cur_deviceStats.point);
		cur_deviceStats.minimumOffset = props.limits.minStorageBufferOffsetAlignment;
		if(cur_deviceStats.point > bestDeviceStats.point) {
			bestDeviceStats = cur_deviceStats;
		}
	}
	if(bestDeviceStats.point < 1) {
		free(queueProps);
		free(extensionProps);
		free(formats);
		return (EngineResult) {ENGINE_INAPPROPRIATE_GPU, VK_SUCCESS};
	} 
	engine->physicalDevice = bestDeviceStats.device;
	engine->swapchainDetails.format = bestDeviceStats.format;
	engine->swapchainDetails.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	engine->compute.index = bestDeviceStats.computeI;
	engine->graphics.index = bestDeviceStats.graphicsI;
	engine->presentation.index = bestDeviceStats.presentationI;
	engine->hardwareRayTracing = bestDeviceStats.supportsRayTracing;

	free(queueProps);
	free(extensionProps);
	free(formats);

	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(engine->physicalDevice, &props);
	debug_msg("\x1b[1;37mThe chosen device: %s\n\x1b[0m", props.deviceName);
	return ENGINE_RESULT_SUCCESS;
}
EngineResult EngineSwapchainCreate(Engine *engine, uint32_t frameBufferWidth, uint32_t frameBufferHeight, EngineImage *renderImages) {
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
	
	uint32_t queueFamilyIndices[2] = {engine->graphics.index, engine->presentation.index};
	swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if(engine->graphics.index == engine->presentation.index) {
		swapchainCI.queueFamilyIndexCount = 2;
		swapchainCI.pQueueFamilyIndices = &queueFamilyIndices;
	}

	res = vkCreateSwapchainKHR(engine->device, &swapchainCI, NULL, &engine->swapchain);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_SWAPCHAIN_FAILED, res);
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
	for(int i = 0; i < FRAME_OVERLAP; i++) {
		engine->renderImages[i].imageExtent = (VkExtent3D){
			.width = engine->pixelResolution.width,
			.height = engine->pixelResolution.height,
			.depth = 1,
		};
		engine->renderImages[i].imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
		
		VkImageCreateInfo renderImageCI = imageCreateInfo(engine->renderImages[i].imageFormat, 
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT | (engine->hardwareRayTracing ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0),
			engine->renderImages[i].imageExtent
		);
		renderImageCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT | (engine->hardwareRayTracing ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0);
		VmaAllocationCreateInfo renderImageAllocationCI = {
			.usage = VMA_MEMORY_USAGE_GPU_ONLY,
			.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		};
		res = vmaCreateImage(engine->allocator, &renderImageCI, &renderImageAllocationCI, &engine->renderImages[i].image, &engine->renderImages[i].allocation, NULL);
		ERR_CHECK(res == VK_SUCCESS, ENGINE_SWAPCHAIN_FAILED, res);
		VkImageViewCreateInfo imageViewCI = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = engine->renderImages[i].image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = engine->renderImages[i].imageFormat,
			.components = {VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY},
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			},
		};
		res = vkCreateImageView(engine->device, &imageViewCI, NULL, &engine->renderImages[i].imageView);
		ERR_CHECK(res == VK_SUCCESS, ENGINE_SWAPCHAIN_FAILED, res);
		renderImages[i] = (EngineImage) {
			.image = engine->renderImages[i].image,
			.layout = VK_IMAGE_LAYOUT_GENERAL,
			.view = engine->renderImages[i].imageView
		};
	}
	//do err check later ^^
	// VkImageViewCreateInfo renderingImageViewCI = imageViewCreateInfo(engine->renderingImage.imageFormat, engine->renderingImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
	// // vkCreateImageView(engine->device, &renderingImageViewCI, NULL, &engine->renderingImage.imageView);
	//do err check later ^^

	return ENGINE_RESULT_SUCCESS;
}
void EngineSwapchainDestroy(Engine *engine) {
	vkQueueWaitIdle(engine->graphics.queue);
	for(int i = 0; i < FRAME_OVERLAP; i++) {
		vkDestroyImageView(engine->device, engine->renderImages[i].imageView, NULL);
		vmaDestroyImage(engine->allocator, engine->renderImages[i].image, engine->renderImages[i].allocation);
	}
	for(int i = 0; i < engine->swapchainImageCount; i++) {
		vkDestroyImageView(engine->device, engine->swapchainImageViews[i], NULL);
	}
	free(engine->swapchainImages);
	free(engine->swapchainImageViews);
	vkDestroySwapchainKHR(engine->device, engine->swapchain, NULL);
}

EngineResult CreateQueue(Engine *engine, EngineQueue *queue) {
	vkGetDeviceQueue(engine->device, queue->index, 0, &queue->queue);

	VkCommandPoolCreateInfo commandPoolCI = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.pNext = NULL,
		.queueFamilyIndex = queue->index
	};
	res = vkCreateCommandPool(engine->device, &commandPoolCI, NULL, &queue->pool);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_QUEUECOMMAND_CREATION_FAILED, res);
	return ENGINE_RESULT_SUCCESS;
}
EngineResult DestroyQueue(Engine *engine, EngineQueue *queue) {
	vkQueueWaitIdle(queue->queue);
	vkDestroyCommandPool(engine->device, queue->pool, NULL);
	return ENGINE_RESULT_SUCCESS;
}

EngineResult ChangeImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags2 pipelineStage) {
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
		.srcStageMask = pipelineStage,
		.dstStageMask = pipelineStage,
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
EngineResult EngineDrawStart(Engine *engine, EngineColor background, EngineSemaphore *signalSemaphore) {
	res = vkWaitForFences(engine->device, 1, &engine->frameFence[engine->cur_frame], true, 1000000000);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_FENCE_NOT_WORKING, res);
	EngineResult eRes = {0};
	vkAcquireNextImageKHR(engine->device, engine->swapchain, 1000000000, engine->swapchainSemaphores[engine->cur_frame], NULL, &engine->cur_swapchainIndex);

	res = vkResetFences(engine->device, 1, &engine->frameFence[engine->cur_frame]);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_FENCE_NOT_WORKING, res);	
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
	
	vkResetCommandBuffer(engine->backgroundBuffer[engine->cur_frame], 0);
	vkBeginCommandBuffer(engine->backgroundBuffer[engine->cur_frame], &beginInfo);
	
	VkClearColorValue backgroundColor = {
		.float32 = {background.r, background.g, background.b, background.a}
	};
	ChangeImageLayout(engine->backgroundBuffer[engine->cur_frame], 
				engine->renderImages[engine->cur_frame].image, 
				VK_IMAGE_LAYOUT_UNDEFINED, 
				VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
	vkCmdClearColorImage(engine->backgroundBuffer[engine->cur_frame], engine->renderImages[engine->cur_frame].image, VK_IMAGE_LAYOUT_GENERAL, &backgroundColor, 1, &backgroundSubresourceRange);
	res = vkEndCommandBuffer(engine->backgroundBuffer[engine->cur_frame]);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_PREPARE_FOR_SUBMISSION, res);

	VkCommandBufferSubmitInfo cmdSubmitInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = engine->backgroundBuffer[engine->cur_frame],
		.deviceMask = 0,
		.pNext = NULL
	};

	VkSemaphoreSubmitInfo signalInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.deviceIndex = 0,
		.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		.semaphore = engine->frameReadySemaphore[engine->cur_frame]
	};
	*signalSemaphore = engine->frameReadySemaphore[engine->cur_frame];

	VkSubmitInfo2 queueSubmitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &cmdSubmitInfo,
		.signalSemaphoreInfoCount = 1,
		.pSignalSemaphoreInfos = &signalInfo,
		.waitSemaphoreInfoCount = 0,
		.pWaitSemaphoreInfos = NULL,
		.flags = 0
	};

	res = vkQueueSubmit2(engine->graphics.queue, 1, &queueSubmitInfo, NULL);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_SUBMIT_TO_GPU, res);
	return ENGINE_RESULT_SUCCESS;
}
EngineResult EngineDrawEnd(Engine *engine, EngineSemaphore *waitSemaphore) {
	VkCommandBufferBeginInfo cmdBeginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = 0,
		.pNext = NULL,
		.pInheritanceInfo = NULL
	};

	vkBeginCommandBuffer(engine->copyBuffer[engine->cur_frame], &cmdBeginInfo);
	ChangeImageLayout(engine->copyBuffer[engine->cur_frame], engine->renderImages[engine->cur_frame].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
	ChangeImageLayout(engine->copyBuffer[engine->cur_frame], engine->swapchainImages[engine->cur_swapchainIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
	ImageCopy(engine->copyBuffer[engine->cur_frame], engine->renderImages[engine->cur_frame].image, engine->swapchainImages[engine->cur_swapchainIndex], (VkExtent2D){
		.height = engine->renderImages[engine->cur_frame].imageExtent.height,
		.width = engine->renderImages[engine->cur_frame].imageExtent.width
	}, engine->pixelResolution);
	ChangeImageLayout(engine->copyBuffer[engine->cur_frame], engine->swapchainImages[engine->cur_swapchainIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
	vkEndCommandBuffer(engine->copyBuffer[engine->cur_frame]);
	VkCommandBufferSubmitInfo cmdSubmitInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = engine->copyBuffer[engine->cur_frame],
		.deviceMask = 0,
		.pNext = NULL
	};
	VkSemaphoreSubmitInfo waitSemaphoreInfo[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.deviceIndex = 0,
			.pNext = NULL,
			.stageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			.semaphore = engine->swapchainSemaphores[engine->cur_frame]
		}, 
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.deviceIndex = 0,
			.pNext = NULL,
			.stageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			.semaphore = *waitSemaphore
		}
	}, 					
	signalSemaphoreInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.deviceIndex = 0,
		.pNext = NULL,
		.stageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
		.semaphore = engine->bufferCopySemaphore[engine->cur_frame]
	};

	VkSubmitInfo2 queueSubmitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &cmdSubmitInfo,
		.signalSemaphoreInfoCount = 1,
		.pSignalSemaphoreInfos = &signalSemaphoreInfo,
		.waitSemaphoreInfoCount = 2,
		.pWaitSemaphoreInfos = waitSemaphoreInfo,
		.flags = 0
	};
	res = vkQueueSubmit2(engine->graphics.queue, 1, &queueSubmitInfo, engine->frameFence[engine->cur_frame]);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_SUBMIT_TO_GPU, res);
	VkPresentInfoKHR presentInfo = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.pNext = NULL,
		.swapchainCount = 1,
		.pSwapchains = &engine->swapchain,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &engine->bufferCopySemaphore[engine->cur_frame],
		.pImageIndices = &engine->cur_swapchainIndex,
	};
	res = vkQueuePresentKHR(engine->graphics.queue, &presentInfo);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_DISPLAY, res);	
	updateCurrentFrame_(engine);
	return ENGINE_RESULT_SUCCESS;
}
EngineResult EngineInit(Engine **engine_instance, EngineCI engineCI, uintptr_t *vkInstance) {
	Engine *engine = malloc(sizeof(Engine));
	*engine_instance = engine;
	
	engine->oldSwapchain = VK_NULL_HANDLE;
	engine->swapchain = VK_NULL_HANDLE;
	engine->shaderModulesCount = 0;

	#ifndef NDEBUG
	ERR_CHECK(checkValidationSupport(), ENGINE_DEBUG_CREATION_FAILED, VK_SUCCESS);
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
	ERR_CHECK(engine != NULL, ENGINE_OUT_OF_MEMORY, VK_SUCCESS);
	engine->instance = VK_NULL_HANDLE;
	res = vkCreateInstance(&instanceCI, NULL, &engine->instance);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_INSTANCE_CREATION_FAILED, res);
	debug_msg("Vulkan Instance created successfully\n");
	*vkInstance = engine->instance;
	return ENGINE_RESULT_SUCCESS;
};
EngineResult EngineFinishSetup(Engine *engine, uintptr_t surface) {
	engine->surface = surface;
	engine->physicalDevice = VK_NULL_HANDLE;
	size_t physicalDeviceCount = 0;
	vkEnumeratePhysicalDevices(engine->instance, &physicalDeviceCount, NULL);
	debug_msg("Physical Devices Found: %d\n", physicalDeviceCount);
	ERR_CHECK(physicalDeviceCount > 0, ENGINE_GPU_NOT_FOUND, VK_SUCCESS);
	VkPhysicalDevice *physicalDevices = malloc(physicalDeviceCount * sizeof(VkPhysicalDevice));
	vkEnumeratePhysicalDevices(engine->instance, &physicalDeviceCount, physicalDevices);
	
	debug_msg("picking physical device\n");
	EngineResult eRes = findSuitablePhysicalDevice(physicalDevices, physicalDeviceCount, engine);
	ERR_CHECK(eRes.EngineCode == ENGINE_SUCCESS, eRes.EngineCode, eRes.VulkanCode);
	free(physicalDevices);

	engine->swapchainImageCount = engine->swapchainDetails.capabilities.minImageCount + 1;

	float queuePriority = 1;
	debug_msg("Graphics queue index: %d\nCompute queue index: %d\nPresentation queue index: %d\n", engine->graphics.index, engine->compute.index, engine->presentation.index);
	uint32_t queueI[] = {engine->graphics.index, engine->compute.index, engine->presentation.index};
	uint32_t uniqueQueueI[ARR_SIZE(queueI)] = {engine->graphics.index};

	uint32_t queueCI_len = 1;
	VkDeviceQueueCreateInfo queueCI[ARR_SIZE(uniqueQueueI)] = {
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.pNext = NULL,
			.pQueuePriorities = &queuePriority,
			.queueCount = 1,
			.queueFamilyIndex = uniqueQueueI[0]
		}
	};
	for(int i = 1; i < ARR_SIZE(uniqueQueueI); i++) { //yes i'm looping it like an array, tho i prefer to keep it in class as struct
		bool isUnique = true;
		for(int j = 0; j < ARR_SIZE(uniqueQueueI); j++) {
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
	for(int i = 0; i < queueCI_len; i++) {
		debug_msg("unique index %d: %d\n", i, uniqueQueueI[i]);
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
		.enabledExtensionCount = engine->hardwareRayTracing ? ARR_SIZE(deviceExtensions) : MandatoryDeviceExtensionsCount,
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
	ERR_CHECK(res == VK_SUCCESS, ENGINE_DEVICE_CREATION_FAILED, res);
	debug_msg("Device created\n");

	CreateQueue(engine, &engine->graphics);
	CreateQueue(engine, &engine->compute);
	CreateQueue(engine, &engine->presentation);

	VkSemaphoreCreateInfo semaphoreCI = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.flags = VK_SEMAPHORE_TYPE_BINARY,
		.pNext = NULL
	};
	VkFenceCreateInfo fenceCI = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
		.pNext = NULL
	};
	VkCommandBufferAllocateInfo info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandBufferCount = 2,
		.commandPool = engine->graphics.pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.pNext = NULL
	};
	res = vkAllocateCommandBuffers(engine->device, &info, engine->backgroundBuffer);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_QUEUECOMMAND_ALLOCATION_FAILED, res);
	res = vkAllocateCommandBuffers(engine->device, &info, engine->copyBuffer);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_QUEUECOMMAND_ALLOCATION_FAILED, res);
	


	VmaAllocatorCreateInfo allocatorCI = {
		.device = engine->device,
		.instance = engine->instance,
		.physicalDevice = engine->physicalDevice,
		.vulkanApiVersion = VK_API_VERSION_1_3,
		.pAllocationCallbacks = NULL,
		.pDeviceMemoryCallbacks = NULL,
	};
	vmaCreateAllocator(&allocatorCI, &engine->allocator);

	for(int i = 0; i < FRAME_OVERLAP; i++) {
		res = vkCreateSemaphore(engine->device, &semaphoreCI, NULL, &engine->swapchainSemaphores[i]);
		ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_CREATE_SYNCHRONISING_VARIABLES, res);
		res = vkCreateSemaphore(engine->device, &semaphoreCI, NULL, &engine->frameReadySemaphore[i]);
		ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_CREATE_SYNCHRONISING_VARIABLES, res);
		res = vkCreateSemaphore(engine->device, &semaphoreCI, NULL, &engine->bufferCopySemaphore[i]);
		ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_CREATE_SYNCHRONISING_VARIABLES, res);
		res = vkCreateSemaphore(engine->device, &semaphoreCI, NULL, &engine->tmpSemaphore[i]);
		ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_CREATE_SYNCHRONISING_VARIABLES, res);
		res = vkCreateFence(engine->device, &fenceCI, NULL, &engine->frameFence[i]);
		ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_CREATE_SYNCHRONISING_VARIABLES, res);
	}
	engine->cur_frame = 0;
	debug_msg("Initialisation complete\n");
	return ENGINE_RESULT_SUCCESS;
}
void EngineDestroy(Engine *engine) {
	vkDeviceWaitIdle(engine->device);
	vmaDestroyAllocator(engine->allocator);

	if(engine->descriptorSet != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(engine->device, engine->descriptorPool, NULL);
		vkDestroyDescriptorSetLayout(engine->device, engine->descriptorSetLayout, NULL);
	}

	if(engine->shaderModulesCount > 0) {
		vkDestroyPipelineLayout(engine->device, engine->pipelineLayout, NULL);
	}

	for(int i = 0; i < engine->shaderModulesCount; i++) {
		vkDestroyShaderModule(engine->device, engine->shaderModules[i], NULL);
		vkDestroyPipeline(engine->device, engine->pipelines[i], NULL);
	}

	for(int i = 0; i < FRAME_OVERLAP; i++) {
		vkDestroySemaphore(engine->device, engine->swapchainSemaphores[i], NULL);
		vkDestroySemaphore(engine->device, engine->frameReadySemaphore[i], NULL);
		vkDestroySemaphore(engine->device, engine->bufferCopySemaphore[i], NULL);
		vkDestroySemaphore(engine->device, engine->tmpSemaphore[i], NULL);
		vkDestroyFence(engine->device, engine->frameFence[i], NULL);
	}

	DestroyQueue(engine, &engine->graphics);
	DestroyQueue(engine, &engine->compute);
	DestroyQueue(engine, &engine->presentation);

	vkDestroyDevice(engine->device, NULL);
	vkDestroySurfaceKHR(engine->instance, engine->surface, NULL);
	vkDestroyInstance(engine->instance, NULL);
	free(engine);
}

EngineResult EngineLoadShaders(Engine *engine, EngineShaderInfo *shaders, size_t shaderCount) {
	engine->shaderModulesCount = shaderCount;
	engine->shaderModules = malloc(sizeof(VkShaderModule) * shaderCount);
	engine->pipelines = malloc(sizeof(VkPipeline) * shaderCount);
	VkComputePipelineCreateInfo *pipelineCIs = malloc(sizeof(VkComputePipelineCreateInfo) * shaderCount);
	VkPipelineLayoutCreateInfo pipelineLayoutCI = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pNext = NULL,
		.pushConstantRangeCount = 0,
		.setLayoutCount = 1,
		.pSetLayouts = &engine->descriptorSetLayout,
	};
	res = vkCreatePipelineLayout(engine->device, &pipelineLayoutCI, NULL, &engine->pipelineLayout);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_SHADER_CREATION_FAILED, res);
	for(size_t i = 0; i < shaderCount; i++) {
		VkShaderModuleCreateInfo shaderModuleCI = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.pNext = NULL,
			.codeSize = shaders[i].byteSize,
			.pCode = shaders[i].code
		};
		res = vkCreateShaderModule(engine->device, &shaderModuleCI, NULL, &engine->shaderModules[i]);
		ERR_CHECK(res == VK_SUCCESS, ENGINE_SHADER_CREATION_FAILED, res);
		pipelineCIs[i] = (VkComputePipelineCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.pNext = NULL,
			.flags = 0,
			.layout = engine->pipelineLayout,
			.stage = (VkPipelineShaderStageCreateInfo) {
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_COMPUTE_BIT,
				.pName = "main",
				.pNext = NULL,
				.pSpecializationInfo = NULL,
				.module = engine->shaderModules[i],
			},
		};
	}
	res = vkCreateComputePipelines(engine->device, NULL, shaderCount, pipelineCIs, NULL, engine->pipelines);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_SHADER_CREATION_FAILED, res);
	return ENGINE_RESULT_SUCCESS;
}
EngineResult EngineDeclareDataSet(Engine *engine, EngineDataTypeInfo *datatypes, size_t datatypeCount) {
	VkDescriptorSetLayoutBinding *bindings = malloc(sizeof(VkDescriptorSetLayoutBinding) * datatypeCount);
	VkDescriptorPoolSize *poolSizes = malloc(sizeof(VkDescriptorPoolSize) * datatypeCount);
	for(int i = 0; i < datatypeCount; i++) {
		VkDescriptorType type = 0;
		switch(datatypes[i].type) {
			case ENGINE_BUFFER_STORAGE:
				type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				break;
			case ENGINE_BUFFER_UNIFORM:
				type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				break;
			case ENGINE_IMAGE:
				type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				break;
		}
		bindings[i] = (VkDescriptorSetLayoutBinding) {
			.binding = datatypes[i].bindingIndex,
			.descriptorType = type,
			.descriptorCount = datatypes[i].length,
			.pImmutableSamplers = NULL, //for now we keep it NULL. Consider looking at it later
			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT //consider ALL_SHADERS tho
		};
		poolSizes[i] = (VkDescriptorPoolSize) {
			.descriptorCount = datatypes[i].length * FRAME_OVERLAP,
			.type = type
		};
	}
	VkDescriptorSetLayoutCreateInfo layoutCI = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = NULL,
		.bindingCount = datatypeCount,
		.pBindings = bindings,
		.flags = 0
	};
	res = vkCreateDescriptorSetLayout(engine->device, &layoutCI, NULL, &engine->descriptorSetLayout);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_DATASET_DECLARATION_FAILED, res);
	debug_msg("Descriptor set layout created\n");
	free(bindings);
	VkDescriptorPoolCreateInfo poolCI = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = FRAME_OVERLAP,
		.poolSizeCount = datatypeCount,
		.pPoolSizes = poolSizes,
		.pNext = NULL
	};
	res = vkCreateDescriptorPool(engine->device, &poolCI, NULL, &engine->descriptorPool);
	debug_msg("Descriptor pool created\n");
	free(poolSizes);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_DATASET_DECLARATION_FAILED, res);
	VkDescriptorSetLayout layouts[2] = {engine->descriptorSetLayout, engine->descriptorSetLayout};
	VkDescriptorSetAllocateInfo allocateInfo = {
		.descriptorPool = engine->descriptorPool,
		.descriptorSetCount = 2,
		.pNext = NULL,
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pSetLayouts = layouts
	};	
	res = vkAllocateDescriptorSets(engine->device, &allocateInfo, engine->descriptorSet);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_DATASET_DECLARATION_FAILED, res);
	debug_msg("Descriptor set created\n");
	return ENGINE_RESULT_SUCCESS;
}
void EngineWriteData(Engine *engine, EngineWriteDataInfo info) {
	VkDescriptorImageInfo imageInfo = {
		.imageLayout = info.content.image.layout,
		.imageView = info.content.image.view,
		.sampler = VK_NULL_HANDLE
	};
	VkDescriptorBufferInfo bufferInfo = {
		.buffer = info.content.buffer._buffer,
		.offset = 0,
		.range = VK_WHOLE_SIZE
	};
	VkWriteDescriptorSet writeSet = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = NULL,
		.dstSet = engine->descriptorSet[engine->cur_frame],
		.dstBinding = info.binding,
		.dstArrayElement = info.startingIndex,
		.descriptorCount = info.endIndex - info.startingIndex + 1,
	};
	
	switch(info.type) {
		case ENGINE_BUFFER_STORAGE:
			writeSet.pBufferInfo = &bufferInfo;
			writeSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			break;
		case ENGINE_BUFFER_UNIFORM:
			writeSet.pBufferInfo = &bufferInfo;
			writeSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			break;
		case ENGINE_IMAGE:
			writeSet.pImageInfo = &imageInfo;
			writeSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			break;
	}
	vkUpdateDescriptorSets(engine->device, 1, &writeSet, 0, NULL);
}

EngineResult EngineCreateCommand(Engine *engine, EngineCommand *cmd) {
	VkCommandBufferAllocateInfo allocateInfo = {
		.commandBufferCount = 1,
		.commandPool = engine->compute.pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext = NULL
	};
	res = vkAllocateCommandBuffers(engine->device, &allocateInfo, cmd);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_START_COMMAND, res);
}
EngineResult EngineCommandRecordingStart(Engine *engine, EngineCommand cmd, EngineCommandRecordingType type) {
	VkCommandBufferBeginInfo beginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = NULL,
		.pInheritanceInfo = NULL,
	};
	switch(type) {
		case ENGINE_COMMAND_ONE_TIME:
			beginInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			break;
		case ENGINE_COMMAND_REUSABLE:
			beginInfo.flags |= VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
			break;
	}
	res = vkBeginCommandBuffer(cmd, &beginInfo);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_START_COMMAND, res);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, engine->pipelineLayout, 0, 1, &engine->descriptorSet[engine->cur_frame], 0, NULL);
}
EngineResult EngineCommandRecordingEnd(Engine *engine, EngineCommand cmd) {
	res = vkEndCommandBuffer(cmd);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_PREPARE_FOR_SUBMISSION, res);	
	return ENGINE_RESULT_SUCCESS;
}
EngineResult EngineSubmitCommand(Engine *engine, EngineCommand cmd, EngineSemaphore *waitSemaphore, EngineSemaphore *signalSemaphore) {
	VkCommandBufferSubmitInfo cmdSubmitInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = cmd,
		.deviceMask = 0,
		.pNext = NULL
	};

	VkSemaphoreSubmitInfo signalInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.deviceIndex = 0,
		.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		.semaphore = *signalSemaphore
	};
	VkSemaphoreSubmitInfo waitInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.deviceIndex = 0,
		.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		.semaphore = *waitSemaphore
	};

	VkSubmitInfo2 queueSubmitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &cmdSubmitInfo,
		.signalSemaphoreInfoCount = signalSemaphore != NULL ? 1 : 0,
		.pSignalSemaphoreInfos = &signalInfo,
		.waitSemaphoreInfoCount = waitSemaphore != NULL ? 1 : 0,
		.pWaitSemaphoreInfos = &waitInfo,
		.flags = 0
	};

	res = vkQueueSubmit2(engine->compute.queue, 1, &queueSubmitInfo, NULL);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_SUBMIT_TO_GPU, res);
	return ENGINE_RESULT_SUCCESS;
}
void EngineDestroyCommand(Engine *engine, EngineCommand cmd) {
	vkQueueWaitIdle(engine->compute.queue);
	vkResetCommandBuffer(cmd, NULL);
}

void EngineRunShader(Engine *engine, EngineCommand cmd, size_t index, EngineShaderRunInfo runInfo) {
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, engine->pipelines[index]);
	vkCmdDispatch(cmd, runInfo.groupSizeX, runInfo.groupSizeY, runInfo.groupSizeZ);
}

EngineResult EngineCreateSemaphore(Engine *engine, EngineSemaphore *semaphore) {
	VkSemaphoreCreateInfo semaphoreCI = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = NULL,
		.flags = VK_SEMAPHORE_TYPE_BINARY
	};
	res = vkCreateSemaphore(engine->device, &semaphoreCI, NULL, semaphore);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_CREATE_SYNCHRONISING_VARIABLES, res);
	return ENGINE_RESULT_SUCCESS;
}
void EngineDestroySemaphore(Engine *engine, EngineSemaphore semaphore) {
	vkDestroySemaphore(engine->device, semaphore, NULL);
}

EngineResult EngineCreateBuffer(Engine *engine, EngineBuffer *buffer, EngineDataType type) {
	VkBufferCreateInfo buffCI = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = NULL,
		.pQueueFamilyIndices = &engine->compute.index,
		.queueFamilyIndexCount = 1,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.size = buffer->length * buffer->elementByteSize,
	};
	switch(type) {
		case ENGINE_BUFFER_STORAGE:
			buffCI.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			break;
		case ENGINE_BUFFER_UNIFORM:
			buffCI.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			break;
	}

	VmaAllocationCreateInfo allocCI = {
		.usage = VMA_MEMORY_USAGE_CPU_TO_GPU
	};
	res = vmaCreateBuffer(engine->allocator, &buffCI, &allocCI, &buffer->_buffer, &buffer->_allocation, NULL);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_BUFFER_CREATION_FAILED, res);
	res = vmaMapMemory(engine->allocator, buffer->_allocation, &buffer->data);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_BUFFER_CREATION_FAILED, res);
	return ENGINE_RESULT_SUCCESS;
}
void EngineDestroyBuffer(Engine *engine, EngineBuffer buffer) {
	vkQueueWaitIdle(engine->compute.queue);
	vmaUnmapMemory(engine->allocator, buffer._allocation);
	vmaDestroyBuffer(engine->allocator, buffer._buffer, buffer._allocation);
}