#include <Engine.h>

#include <vulkan/vulkan.h>

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <utils.h>
#include <math.h>

#include <vk_mem_alloc.h>
#include <stb_image.h>

typedef struct {
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
} AllocatedImage;

typedef struct {
	VkQueue queue;
	uint32_t index;
	VkCommandPool pool;
} vulkanQueue;

#define FRAME_OVERLAP 2

struct Engine {
    VkDevice device;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
	VkPhysicalDeviceProperties physicalDeviceProperties;
	uint32_t workgroupSize;
	bool hardwareRayTracing;

    VkSurfaceKHR surface;
	vulkanQueue graphics, compute, presentation;

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

	uint32_t cur_swapchainIndex;
	VmaAllocator allocator;

	AllocatedImage renderImages[FRAME_OVERLAP];

	VkSemaphore swapchainSemaphores[FRAME_OVERLAP],
				frameReadySemaphores[FRAME_OVERLAP],
				bufferCopySemaphores[FRAME_OVERLAP],
				tmpSemaphores[FRAME_OVERLAP];
	/*Constant command buffers*/
	VkCommandBuffer backgroundBufferCmd[FRAME_OVERLAP], copyBufferCmd[FRAME_OVERLAP];
	size_t cur_frame;
	
	size_t shaderModulesCount;
	VkShaderModule *shaderModules;
	
	VkPipelineLayout pipelineLayout;
	VkPipeline *pipelines;

	VkDescriptorSetLayout descriptorSetLayout;
	EngineDataTypeInfo descriptorDataTypes[ENGINE_DATATYPE_INFO_LENGTH];
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet[FRAME_OVERLAP];

	EngineHeapArray writeQueue;
	EngineBuffer materialBuffer, sphereBuffer, sunlightBuffer, cameraBuffer;

	EngineObjectLimits limits;
};

uint32_t EngineGetFrame(Engine *engine) {
	return engine->cur_frame;
}
uint32_t NextFrame(uint32_t frame) {
	frame++;
	if(frame >= FRAME_OVERLAP)
		frame = 0;
	return frame;
}
void updateCurrentFrame_(Engine *engine) {
	engine->cur_frame = NextFrame(engine->cur_frame);
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
	VkPhysicalDeviceProperties props;
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
		deviceStats cur_deviceStats = {
			.point = 0, 
			.device = devices[i], 
			.supportsRayTracing = false
		};
		vkGetPhysicalDeviceProperties(devices[i], &cur_deviceStats.props);
		debug_msg("Device %d: %s\n", i, cur_deviceStats.props.deviceName);

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
		if(cur_deviceStats.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
			cur_deviceStats.point++;
		}
		debug_msg("\tDevice Passed with points: %d\n", cur_deviceStats.point);
		debug_msg("\tMax workgroups for that device:\n\t\tmaxComputeWorkGroupSize: %llu\n\t\tmaxComputeWorkGroupCount: %llu\n\t\tmaxComputeWorkGroupInvocations: %llu\n", 
			cur_deviceStats.props.limits.maxComputeWorkGroupSize,
			cur_deviceStats.props.limits.maxComputeWorkGroupCount,
			cur_deviceStats.props.limits.maxComputeWorkGroupInvocations
		);

		cur_deviceStats.minimumOffset = cur_deviceStats.props.limits.minStorageBufferOffsetAlignment;
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
	engine->physicalDeviceProperties = bestDeviceStats.props;
	engine->swapchainDetails.format = bestDeviceStats.format;
	engine->swapchainDetails.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	engine->compute.index = bestDeviceStats.computeI;
	engine->graphics.index = bestDeviceStats.graphicsI;
	engine->presentation.index = bestDeviceStats.presentationI;
	engine->hardwareRayTracing = bestDeviceStats.supportsRayTracing;
	engine->physicalDeviceProperties = bestDeviceStats.props;

	free(queueProps);
	free(extensionProps);
	free(formats);

	debug_msg("\x1b[1;37mThe chosen device: %s\n\x1b[0m", engine->physicalDeviceProperties.deviceName);
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
	}
	vkDeviceWaitIdle(engine->device);
	VkDescriptorImageInfo imageInfos[FRAME_OVERLAP] = {0};
	VkWriteDescriptorSet writeSets[FRAME_OVERLAP] = {0};
	for(size_t i = 0; i < FRAME_OVERLAP; i++) {
		imageInfos[i] = (VkDescriptorImageInfo) {
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
			.imageView = engine->renderImages[i].imageView,
			.sampler = VK_NULL_HANDLE
		};
		writeSets[i] = (VkWriteDescriptorSet) {
			.dstSet = engine->descriptorSet[i],
			.dstBinding = 0,
			.pImageInfo = &imageInfos[i],
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pNext = NULL,
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstArrayElement = 0,
			.descriptorCount = 1
		};
	}
	vkUpdateDescriptorSets(engine->device, FRAME_OVERLAP, writeSets, 0, NULL);
	return ENGINE_RESULT_SUCCESS;
}
void EngineSwapchainDestroy(Engine *engine) {
	vkQueueWaitIdle(engine->graphics.queue);
	for(size_t i = 0; i < FRAME_OVERLAP; i++) {
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

EngineResult CreateQueue(Engine *engine, vulkanQueue *queue) {
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
EngineResult DestroyQueue(Engine *engine, vulkanQueue *queue) {
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

typedef struct {
	VkWriteDescriptorSet writeSet;
	size_t frame;
	size_t updateLeft;
} writeQueueElement;


void updateDescriptorSets(Engine *engine) {
	if(!engine->writeQueue.count)
		return;
	size_t writeSetCount = 0;
	VkWriteDescriptorSet *writeSets = malloc(sizeof(VkWriteDescriptorSet)*engine->writeQueue.count);
	bool *shouldFree = malloc(sizeof(bool) * engine->writeQueue.count);
	debug_msg("queue element info:\n\t===\n\tcur_frame: %d\n", engine->cur_frame);
	for(size_t i = 0; i < engine->writeQueue.count; i++) {
		writeQueueElement cur = ((writeQueueElement*)engine->writeQueue.arr)[i];
		debug_msg("\tbinding: %d\n\ttype: %d\n\tframe: %d\n", cur.writeSet.dstBinding, cur.writeSet.descriptorType, cur.frame);
		debug_msg("\t===\n");
	}
	debug_msg("Descriptor sets:\n");
	for(size_t i = 0; i < FRAME_OVERLAP; i++) {
		debug_msg("\t0x%zX\n", engine->descriptorSet[i]);
	}
	while(engine->writeQueue.count > 0) {
		writeQueueElement cur = ENGINE_HEAPARR(engine->writeQueue, writeQueueElement)[0];
		if(cur.frame != engine->cur_frame) {
			break;
		}
		EngineHeapArrayDequeue(&engine->writeQueue, &cur);
		writeSets[writeSetCount] = cur.writeSet;
		writeSets[writeSetCount].dstSet = engine->descriptorSet[engine->cur_frame];
		debug_msg("index: %d\n\tbinding: %d\n\tset: 0x%zX\n\ttype: %d\n\tbufferAddress: 0x%zX\n\timageAddress: 0x%zX\n\tupdateLeft: %llu\n", 
			writeSetCount, 
			writeSets[writeSetCount].dstBinding, 
			writeSets[writeSetCount].dstSet,
			writeSets[writeSetCount].descriptorType,
			writeSets[writeSetCount].pBufferInfo != NULL ? cur.writeSet.pBufferInfo->buffer : NULL,
			writeSets[writeSetCount].pImageInfo,
			cur.updateLeft
		);
		if(cur.updateLeft <= 1) {
			shouldFree[writeSetCount] = true;
			writeSetCount++;
			continue;
		}
		shouldFree[writeSetCount] = false;
		writeSetCount++;
		cur.updateLeft--;
		cur.frame = NextFrame(engine->cur_frame);
		EngineHeapArrayEnqueue(&engine->writeQueue, &cur);
	}
	debug_msg("writeSetCount = %llu\n", writeSetCount);
	vkUpdateDescriptorSets(engine->device, writeSetCount, writeSets, 0, NULL);
	for(size_t i = 0; i < writeSetCount; i++) {
		if(!shouldFree[i]) {
			continue;
		}
		switch(writeSets[i].descriptorType) {
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
				free(writeSets[i].pBufferInfo);
				break;
			case ENGINE_IMAGE:
				free(writeSets[i].pImageInfo);
				break;
		}
	}
	free(writeSets);
	free(shouldFree);
	debug_msg("update ended\n");
};

EngineResult EngineDrawStart(Engine *engine, EngineColor background, EngineSemaphore *signalSemaphore) {
	res = vkWaitForFences(engine->device, 1, &engine->frameFence, true, 1000000000);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_FENCE_NOT_WORKING, res);
	EngineResult eRes = {0};
	vkAcquireNextImageKHR(engine->device, engine->swapchain, 1000000000, engine->swapchainSemaphores[engine->cur_frame], NULL, &engine->cur_swapchainIndex);

	updateDescriptorSets(engine);

	res = vkResetFences(engine->device, 1, &engine->frameFence[engine->cur_frame]);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_FENCE_NOT_WORKING, res);	

	// VkDescriptorImageInfo renderImageInfo = {
	// 	.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	// 	.imageView = engine->renderImages[engine->cur_frame].imageView,
	// 	.sampler = VK_NULL_HANDLE
	// };
	// VkWriteDescriptorSet writeSet = {
	// 	.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	// 	.pNext = NULL,
	// 	.dstSet = engine->descriptorSet[engine->cur_frame],
	// 	.dstBinding = 0,
	// 	.dstArrayElement = 0,
	// 	.descriptorCount = 1,
	// 	.pImageInfo = &renderImageInfo,
	// 	.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
	// };
	// vkUpdateDescriptorSets(engine->device, 1, &writeSet, 0, NULL);

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
	
	vkResetCommandBuffer(engine->backgroundBufferCmd[engine->cur_frame], 0);
	vkBeginCommandBuffer(engine->backgroundBufferCmd[engine->cur_frame], &beginInfo);
	
	VkClearColorValue backgroundColor = {
		.float32 = {background[0], background[1], background[2], background[3]}
	};
	ChangeImageLayout(engine->backgroundBufferCmd[engine->cur_frame], 
				engine->renderImages[engine->cur_frame].image, 
				VK_IMAGE_LAYOUT_UNDEFINED, 
				VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
	vkCmdClearColorImage(engine->backgroundBufferCmd[engine->cur_frame], engine->renderImages[engine->cur_frame].image, VK_IMAGE_LAYOUT_GENERAL, &backgroundColor, 1, &backgroundSubresourceRange);
	res = vkEndCommandBuffer(engine->backgroundBufferCmd[engine->cur_frame]);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_PREPARE_FOR_SUBMISSION, res);

	VkCommandBufferSubmitInfo cmdSubmitInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = engine->backgroundBufferCmd[engine->cur_frame],
		.deviceMask = 0,
		.pNext = NULL
	};

	VkSemaphoreSubmitInfo signalInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.deviceIndex = 0,
		.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		.semaphore = engine->frameReadySemaphores[engine->cur_frame]
	};
	*signalSemaphore = engine->frameReadySemaphores[engine->cur_frame];

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

	vkBeginCommandBuffer(engine->copyBufferCmd[engine->cur_frame], &cmdBeginInfo);
	ChangeImageLayout(engine->copyBufferCmd[engine->cur_frame], engine->renderImages[engine->cur_frame].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
	ChangeImageLayout(engine->copyBufferCmd[engine->cur_frame], engine->swapchainImages[engine->cur_swapchainIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
	ImageCopy(engine->copyBufferCmd[engine->cur_frame], engine->renderImages[engine->cur_frame].image, engine->swapchainImages[engine->cur_swapchainIndex], (VkExtent2D){
		.height = engine->renderImages[engine->cur_frame].imageExtent.height,
		.width = engine->renderImages[engine->cur_frame].imageExtent.width
	}, engine->pixelResolution);
	ChangeImageLayout(engine->copyBufferCmd[engine->cur_frame], engine->swapchainImages[engine->cur_swapchainIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
	ChangeImageLayout(engine->copyBufferCmd[engine->cur_frame], engine->renderImages[engine->cur_frame].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
	vkEndCommandBuffer(engine->copyBufferCmd[engine->cur_frame]);
	VkCommandBufferSubmitInfo cmdSubmitInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = engine->copyBufferCmd[engine->cur_frame],
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
		.semaphore = engine->bufferCopySemaphores[engine->cur_frame]
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
		.pWaitSemaphores = &engine->bufferCopySemaphores[engine->cur_frame],
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
EngineResult EngineFinishSetup(Engine *engine, uintptr_t surface, EngineObjectLimits limits) {

	engine->limits = limits;

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
		.commandBufferCount = FRAME_OVERLAP,
		.commandPool = engine->graphics.pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.pNext = NULL
	};
	res = vkAllocateCommandBuffers(engine->device, &info, engine->backgroundBufferCmd);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_QUEUECOMMAND_ALLOCATION_FAILED, res);
	res = vkAllocateCommandBuffers(engine->device, &info, engine->copyBufferCmd);
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
		res = vkCreateSemaphore(engine->device, &semaphoreCI, NULL, &engine->frameReadySemaphores[i]);
		ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_CREATE_SYNCHRONISING_VARIABLES, res);
		res = vkCreateSemaphore(engine->device, &semaphoreCI, NULL, &engine->bufferCopySemaphores[i]);
		ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_CREATE_SYNCHRONISING_VARIABLES, res);
		res = vkCreateSemaphore(engine->device, &semaphoreCI, NULL, &engine->tmpSemaphores[i]);
		ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_CREATE_SYNCHRONISING_VARIABLES, res);
		res = vkCreateFence(engine->device, &fenceCI, NULL, &engine->frameFence[i]);
		ERR_CHECK(res == VK_SUCCESS, ENGINE_CANNOT_CREATE_SYNCHRONISING_VARIABLES, res);
	}
	engine->cur_frame = 0;
	engine->writeQueue.byteSize = sizeof(writeQueueElement);
	engine->writeQueue.length = 10;

	engine->sphereBuffer.data = NULL;
	engine->materialBuffer = (EngineBuffer){0};
	engine->cameraBuffer = (EngineBuffer){0};

	//TODO
	//decide whether to let the user generate datatypes or we control it
	//if we let the USER control it then its easier on the engine to let them have their own variables
	//on the other hand its less work for them if ENGINE controls it

	EngineCreateHeapArray(&engine->writeQueue);
	debug_msg("Initialisation complete\n");
	return ENGINE_RESULT_SUCCESS;
}

EngineResult EngineLoadShaders(Engine *engine, EngineShaderInfo *shaders, size_t shaderCount) {
	engine->shaderModulesCount = shaderCount;
	engine->shaderModules = malloc(sizeof(VkShaderModule) * shaderCount);
	engine->pipelines = malloc(sizeof(VkPipeline) * shaderCount);
	VkComputePipelineCreateInfo *pipelineCIs = malloc(sizeof(VkComputePipelineCreateInfo) * shaderCount);

	engine->workgroupSize = ceil(sqrtl(engine->physicalDeviceProperties.limits.maxComputeWorkGroupInvocations));
	debug_msg("workgroup size per axis: %lu\n", engine->workgroupSize);
	debug_msg("Light source length: %zu\n", engine->sunlightBuffer.length);
	uint32_t specialisationData[] = {
		engine->workgroupSize, engine->materialBuffer.length, engine->sunlightBuffer.length
	};
	VkSpecializationMapEntry mapEntries[] = {
		{
			.constantID = 1,
			.offset = 0,
			.size = sizeof(uint32_t)
		},
		{
			.constantID = 2,
			.offset = 0,
			.size = sizeof(uint32_t)
		},
		{
			.constantID = 3,
			.offset = sizeof(uint32_t),
			.size = sizeof(uint32_t)
		}, 
		{
			.constantID = 4,
			.offset = 2 * sizeof(uint32_t),
			.size = sizeof(uint32_t)
		}
	};

	VkSpecializationInfo specialInfo = {
		.dataSize = sizeof(specialisationData),
		.pData = specialisationData,
		.mapEntryCount = ARR_SIZE(mapEntries),
		.pMapEntries = mapEntries
	};
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
				.pSpecializationInfo = &specialInfo,
				.module = engine->shaderModules[i],
			},
		};
	}
	res = vkCreateComputePipelines(engine->device, NULL, shaderCount, pipelineCIs, NULL, engine->pipelines);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_SHADER_CREATION_FAILED, res);
	

	return ENGINE_RESULT_SUCCESS;
}

#define ENGINE_DATATYPE(B, T) (EngineDataTypeInfo) {.bindingIndex = B, .count = 1, .type = T}


#define BINDING_SPHERE_BUFFER 1
#define BINDING_MATERIAL_BUFFER 3
#define BINDING_TRANSFORMATION_BUFFER 2
#define BINDING_SUNLIGHT_BUFFER 4
#define BINDING_MISC_BUFFER 5
#define BINDING_CAMERA_BUFFER 6


inline void EngineGenerateDataTypeInfo(EngineDataTypeInfo *dataTypeInfo) {
	dataTypeInfo[0] = ENGINE_DATATYPE(0, ENGINE_IMAGE);
	dataTypeInfo[BINDING_SPHERE_BUFFER] = ENGINE_DATATYPE(BINDING_SPHERE_BUFFER, ENGINE_BUFFER_STORAGE);
	dataTypeInfo[BINDING_TRANSFORMATION_BUFFER] = ENGINE_DATATYPE(BINDING_TRANSFORMATION_BUFFER, ENGINE_BUFFER_STORAGE);
	dataTypeInfo[BINDING_MATERIAL_BUFFER] = ENGINE_DATATYPE(BINDING_MATERIAL_BUFFER, ENGINE_BUFFER_UNIFORM);
	dataTypeInfo[BINDING_SUNLIGHT_BUFFER] = ENGINE_DATATYPE(BINDING_SUNLIGHT_BUFFER, ENGINE_BUFFER_UNIFORM);
	dataTypeInfo[BINDING_MISC_BUFFER] = ENGINE_DATATYPE(BINDING_MISC_BUFFER, ENGINE_BUFFER_UNIFORM);
	dataTypeInfo[BINDING_CAMERA_BUFFER] = ENGINE_DATATYPE(BINDING_CAMERA_BUFFER, ENGINE_BUFFER_STORAGE);
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
			.descriptorCount = datatypes[i].count,
			.pImmutableSamplers = NULL, //for now we keep it NULL. Consider looking at it later
			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT //consider ALL_SHADERS tho
		};
		poolSizes[i] = (VkDescriptorPoolSize) {
			.descriptorCount = datatypes[i].count * FRAME_OVERLAP,
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
		.pNext = NULL,
		.flags = 0
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
		.pSetLayouts = layouts,
	};	
	res = vkAllocateDescriptorSets(engine->device, &allocateInfo, engine->descriptorSet);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_DATASET_DECLARATION_FAILED, res);
	debug_msg("Descriptor set created\n");
	return ENGINE_RESULT_SUCCESS;
}

void EngineAttachData(Engine *engine, EngineAttachDataInfo info) {
	VkWriteDescriptorSet writeSet = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = NULL,
		.dstBinding = info.binding,
		.dstSet = engine->descriptorSet,
		.dstArrayElement = info.startingIndex,
		.descriptorCount = info.endIndex - info.startingIndex + 1,
	};
	
	VkDescriptorImageInfo *imageInfo = NULL; 
	VkDescriptorBufferInfo *bufferInfo = NULL;
	
	switch(info.type) {
		case ENGINE_BUFFER_STORAGE:
			bufferInfo = malloc(sizeof(VkDescriptorBufferInfo));
			*bufferInfo = (VkDescriptorBufferInfo){
				.buffer = info.content.buffer._buffer,
				.offset = 0,
				.range = VK_WHOLE_SIZE
			};
			writeSet.pBufferInfo = bufferInfo;
			writeSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			break;
		case ENGINE_BUFFER_UNIFORM:
			bufferInfo = malloc(sizeof(VkDescriptorBufferInfo));
			*bufferInfo = (VkDescriptorBufferInfo){
				.buffer = info.content.buffer._buffer,
				.offset = 0,
				.range = VK_WHOLE_SIZE
			};
			writeSet.pBufferInfo = bufferInfo;
			writeSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			break;
		case ENGINE_IMAGE:
			imageInfo = malloc(sizeof(VkDescriptorImageInfo));
			*imageInfo = (VkDescriptorImageInfo){
				.imageLayout = info.content.image.layout,
				.imageView = info.content.image.view,
				.sampler = VK_NULL_HANDLE
			};
			writeSet.pImageInfo = imageInfo;
			writeSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			break;
		case ENGINE_SAMPLED_IMAGE_ARRAY:
			imageInfo = malloc(sizeof(VkDescriptorImageInfo));
			*imageInfo = (VkDescriptorImageInfo){
				.imageLayout = info.content.image.layout,
				.imageView = info.content.image.view,
				.sampler = VK_NULL_HANDLE
			};
			writeSet.pImageInfo = imageInfo;
			writeSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			break;
	}
	writeQueueElement writeElement = {
		.frame = info.nextFrame ? NextFrame(engine->cur_frame) : engine->cur_frame,
		.updateLeft = info.applyCount == ENGINE_ATTACH_DATA_ALL_FRAMES ? FRAME_OVERLAP : info.applyCount,
		.writeSet = writeSet
	};
	if(engine->writeQueue.count > 0) {
		writeQueueElement *arr = engine->writeQueue.arr;
		writeQueueElement cur = arr[engine->writeQueue.count-1];
		if(NextFrame(writeElement.frame) == cur.frame && engine->cur_frame == writeElement.frame) {
			arr[engine->writeQueue.count-1] = writeElement;
			EngineHeapArrayEnqueue(&engine->writeQueue, &cur);
			return;
		}
	}
	EngineHeapArrayEnqueue(&engine->writeQueue, &writeElement);
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
	return ENGINE_RESULT_SUCCESS;
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
	return ENGINE_RESULT_SUCCESS;
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
		.usage = VMA_MEMORY_USAGE_AUTO,
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
	};
	res = vmaCreateBuffer(engine->allocator, &buffCI, &allocCI, &buffer->_buffer, &buffer->_allocation, NULL);
	ERR_CHECK(res == VK_SUCCESS, ENGINE_BUFFER_CREATION_FAILED, res);
	EngineBufferAccessUpdate(engine, buffer, buffer->isAccessible);
	return ENGINE_RESULT_SUCCESS;
}
void EngineBufferAccessUpdate(Engine *engine, EngineBuffer *buffer, bool setAccessVal) {
	if(setAccessVal) {
		vmaMapMemory(engine->allocator, buffer->_allocation, &buffer->data);
		buffer->isAccessible = true;
		return;
	}
	if(buffer->isAccessible) {
		vmaUnmapMemory(engine->allocator, buffer->_allocation);
		buffer->isAccessible = false;
	}
}

void EngineDestroyBuffer(Engine *engine, EngineBuffer buffer) {
	vkQueueWaitIdle(engine->compute.queue);
	if(buffer.isAccessible) {
		vmaUnmapMemory(engine->allocator, buffer._allocation);
	}
	vmaDestroyBuffer(engine->allocator, buffer._buffer, buffer._allocation);
}

void EngineDestroy(Engine *engine) {
	vkDeviceWaitIdle(engine->device);
	EngineDestroyHeapArray(&engine->writeQueue);

	if(engine->shaderModulesCount > 0) {
		vkDestroyPipelineLayout(engine->device, engine->pipelineLayout, NULL);
	}

	for(int i = 0; i < engine->shaderModulesCount; i++) {
		vkDestroyShaderModule(engine->device, engine->shaderModules[i], NULL);
		vkDestroyPipeline(engine->device, engine->pipelines[i], NULL);
	}
	free(engine->shaderModules);
	// if(engine->textureImage.imageView != NULL) {
	// 	vkDestroyImageView(engine->device, engine->textureImage.imageView, NULL);
	// 	vmaDestroyImage(engine->allocator, engine->textureImage.image, engine->textureImage.allocation);
	// }
	// if(engine->sphereBuffer._buffer != NULL)
	// 	vmaDestroyBuffer(engine->allocator, engine->sphereBuffer._buffer, engine->sphereBuffer._allocation);

	vmaDestroyAllocator(engine->allocator);
	for(size_t i = 0; i < FRAME_OVERLAP; i++) {
		vkDestroySemaphore(engine->device, engine->swapchainSemaphores[i], NULL);
		vkDestroySemaphore(engine->device, engine->frameReadySemaphores[i], NULL);
		vkDestroySemaphore(engine->device, engine->bufferCopySemaphores[i], NULL);
		vkDestroySemaphore(engine->device, engine->tmpSemaphores[i], NULL);
		vkDestroyFence(engine->device, engine->frameFence[i], NULL);
	}
	vkDestroyDescriptorPool(engine->device, engine->descriptorPool, NULL);
	vkDestroyDescriptorSetLayout(engine->device, engine->descriptorSetLayout, NULL);

	DestroyQueue(engine, &engine->graphics);
	DestroyQueue(engine, &engine->compute);
	DestroyQueue(engine, &engine->presentation);

	vkDestroyDevice(engine->device, NULL);
	vkDestroySurfaceKHR(engine->instance, engine->surface, NULL);
	vkDestroyInstance(engine->instance, NULL);
	free(engine);
}

EngineResult EngineCreateSphere(Engine *engine, EngineSphere **sphereArr, size_t *count, size_t *indexOut) {
	if(engine->sphereBuffer.data == NULL) {
		debug_msg("creating sphere buffer\n");
		engine->sphereBuffer = (EngineBuffer) {
			.isAccessible = true,
			.length = engine->limits.maxSphereCount,
			.elementByteSize = sizeof(EngineSphere),
			.count = 0
		};
		EngineCreateBuffer(engine, &engine->sphereBuffer, ENGINE_BUFFER_STORAGE);

		EngineAttachDataInfo attachInfo = {
			.applyCount = ENGINE_ATTACH_DATA_ALL_FRAMES,
			.binding = BINDING_SPHERE_BUFFER,
			.content = {.buffer = engine->sphereBuffer},
			.nextFrame = false,
			.type = ENGINE_BUFFER_STORAGE,
			.startingIndex = 0,
			.endIndex = 0,
		};
		EngineAttachData(engine, attachInfo);
	}
	debug_msg("sphere buffer created\n");
	size_t index = engine->sphereBuffer.count;
	if(index >= engine->limits.maxSphereCount) {
		debug_msg("peak reached\n");
		bool found = false;
		for(size_t i = 0; i < engine->limits.maxSphereCount; i++) {
			if(!(sphereArr[i]->flags & ENGINE_EXISTS_FLAG)) {
				index = i;
				found = true;
				break;
			}
		}
		if(!found) {
			return (EngineResult) {.EngineCode = ENGINE_OUT_OF_MEMORY, .VulkanCode = 0};
		}
	}
	debug_msg("if check passed\n");
	EngineSphere *sphereData = engine->sphereBuffer.data;
	sphereArr[index] = &sphereData[index];
	debug_msg("sphere address assigned\n");
	*indexOut = index;
	if(index == engine->sphereBuffer.count)
		engine->sphereBuffer.count += 1;
	debug_msg("Sphere created\n\t===\n\tcount: %zu\n\tindex: %zu\n\t===\n", engine->sphereBuffer.count, *indexOut);
	*count = engine->sphereBuffer.count;
	return ENGINE_RESULT_SUCCESS;
}

void EngineDestroySphere(Engine *engine, EngineSphere *sphere) {
	sphere->flags = 0;
	sphere = NULL;
}

void EngineDestroySphereBuffer(Engine *engine) {
	EngineDestroyBuffer(engine, engine->sphereBuffer);
}


EngineResult EngineLoadTextures(Engine *engine, size_t textureCount, char **texturePaths) {
	// if(textureCount == 0)
	// 	return ENGINE_RESULT_SUCCESS;

	// engine->textureImage.imageExtent = (VkExtent3D){
	// 	.width = 256,
	// 	.height = 256,
	// 	.depth = 1
	// };
	// size_t textureSize = 0;
	// uint8_t *rawTextures = malloc(sizeof(uint8_t) * textureCount);
	// int x, y = 0;
	// for(size_t i = 0; i < textureCount; i++) {
	// 	rawTextures[i] = stbi_load(texturePaths[i], &x, &y, NULL, 3);
	// 	if(x == y && x * y > textureSize) {
	// 		engine->textureImage.imageExtent.width = x;
	// 		engine->textureImage.imageExtent.height = y;
	// 		textureSize = x * y;
	// 	}
	// }

	// VkBufferCreateInfo bufferCI = {
	// 	.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	// 	.pNext = NULL,
	// 	.queueFamilyIndexCount = 1,
	// 	.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	// 	.size = textureSize*sizeof(uint8_t)*textureCount,
	// 	.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
	// };
	// VmaAllocationCreateInfo allocationCI = {
	// 	.usage = VMA_MEMORY_USAGE_AUTO,
	// 	.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
	// };
	// VmaAllocation bufferAlloc = NULL;
	// VkBuffer stagingBuffer = NULL;
	// vmaCreateBuffer(engine->allocator, &bufferCI, &allocationCI, &stagingBuffer, &bufferAlloc, NULL);
	// uint8_t *bufferMem;
	// vmaMapMemory(engine->allocator, bufferAlloc, &bufferMem);
	// memcpy(bufferMem, rawTextures, sizeof(uint8_t) * textureCount);
	// free(rawTextures);
	// vmaUnmapMemory(engine->allocator, engine->materialBuffer._allocation);

	// uint32_t queueFamilyIndices[2] = {engine->graphics.index, engine->compute.index};

	// engine->textureImage.imageFormat = VK_FORMAT_R8G8B8_SRGB;

	// VkImageCreateInfo textureImageCI = {
	// 	.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
	// 	.pNext = NULL,
	// 	.imageType = VK_IMAGE_TYPE_2D,
	// 	.arrayLayers = textureCount,
	// 	.format = engine->textureImage.imageFormat,
	// 	.extent = engine->textureImage.imageExtent,
	// 	.mipLevels = 1,
	// 	.samples = VK_SAMPLE_COUNT_1_BIT,
	// 	.tiling = VK_IMAGE_TILING_OPTIMAL,
	// 	.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
	// 	.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	// 	.queueFamilyIndexCount = 2,
	// 	.pQueueFamilyIndices = &queueFamilyIndices
	// };
 	// allocationCI = (VmaAllocationCreateInfo){
	// 	.usage = VMA_MEMORY_USAGE_AUTO,
	// 	.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
	// 	.priority = 1
	// };
	// res = vmaCreateImage(engine->allocator, &textureImageCI, &allocationCI, &engine->textureImage.image, &engine->textureImage.allocation, NULL);
	// ERR_CHECK(res == VK_SUCCESS, ENGINE_IMAGE_VIEW_FAILED, res);
	// VkImageViewCreateInfo imageViewCI = {
	// 	.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
	// 	.pNext = NULL,
	// 	.image = engine->textureImage.image,
	// 	.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
	// 	.format = engine->textureImage.imageFormat,
	// 	.components = {0},
	// 	.subresourceRange = (VkImageSubresourceRange) {
	// 		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	// 		.baseArrayLayer = 0,
	// 		.layerCount = VK_REMAINING_ARRAY_LAYERS,
	// 		.baseMipLevel = 0,
	// 		.levelCount = 1
	// 	}
	// };
	// vkCreateImageView(engine->device, &imageViewCI, NULL, &engine->textureImage.imageView);
	// ERR_CHECK(res == VK_SUCCESS, ENGINE_IMAGE_VIEW_FAILED, res);

	// VkCommandBufferAllocateInfo cmdAllocInfo = {
	// 	.commandBufferCount = 1,
	// 	.commandPool = engine->graphics.pool,
	// 	.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
	// 	.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
	// 	.pNext = NULL
	// };
	// VkCommandBuffer copyBuff;
	// vkAllocateCommandBuffers(engine->device, &cmdAllocInfo, &copyBuff);
	// VkCommandBufferBeginInfo beginInfo = {
	// 	.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	// 	.pNext = NULL,
	// 	.pInheritanceInfo = NULL,
	// 	.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	// };
	// vkBeginCommandBuffer(copyBuff, &beginInfo);

	// VkBufferImageCopy imageCopy = {
	// 	.bufferImageHeight = y,
	// 	.bufferRowLength = x,
	// 	.imageExtent = engine->textureImage.imageExtent,
	// 	.imageSubresource = (VkImageSubresourceLayers) {
	// 		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	// 		.baseArrayLayer = 0,
	// 		.layerCount = VK_REMAINING_ARRAY_LAYERS,
	// 		.mipLevel = 0
	// 	},
	// };
	// ChangeImageLayout(copyBuff, engine->textureImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
	// vkCmdCopyBufferToImage(copyBuff, stagingBuffer, engine->textureImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopy);
	// ChangeImageLayout(copyBuff, engine->textureImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
	// vkEndCommandBuffer(copyBuff);
	// VkSubmitInfo queueSubmitInfo = {
	// 	.commandBufferCount = 1,
	// 	.pCommandBuffers = &copyBuff,
	// 	.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	// 	.pNext = NULL,
	// 	.signalSemaphoreCount = 0,
	// 	.waitSemaphoreCount = 0
	// };
	// VkFenceCreateInfo fenceCI = {
	// 	.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
	// 	.pNext = NULL
	// };
	// VkFence fence;
	// vkCreateFence(engine->device, &fenceCI, NULL, &fence);
	// vkQueueSubmit(engine->graphics.queue, 1, &queueSubmitInfo, fence);

	// vkWaitForFences(engine->device, 1, &fence, true, 1000000000);
	// vkDestroyFence(engine->device, fence, NULL);
	// vmaDestroyBuffer(engine->allocator, stagingBuffer, bufferAlloc);

	// VkSamplerCreateInfo samplerCI = {
	// 	.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
	// 	.pNext = NULL,
	// 	.anisotropyEnable = false,
	// 	.magFilter = VK_FILTER_NEAREST,
	// 	.minFilter = VK_FILTER_NEAREST,
	// 	.compareEnable = false,
	// 	.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR
	// };
	// vkCreateSampler(engine->device, &samplerCI, NULL, &engine->sampler);

	// EngineAttachDataInfo attachInfo = {
	// 	.binding = 5,
	// 	.content = engine->textureImage.image,
	// 	.startingIndex = 0,
	// 	.endIndex = textureCount,
	// 	.type = ENGINE_SAMPLED_IMAGE_ARRAY
	// };
	// EngineAttachData(engine, attachInfo);
	// debug_msg("Textures were loaded successfully\n");
	// return ENGINE_RESULT_SUCCESS;
};
void EngineUnloadTextures(Engine *engine) {
	// vkDestroySampler(engine->device, engine->sampler, NULL);
	// vmaDestroyImage(engine->allocator, engine->textureImage.image, engine->textureImage.allocation);
}

void EngineLoadMaterials(Engine *engine, EngineMaterial *material, size_t materialCount) {
	engine->materialBuffer = (EngineBuffer){
		.length = materialCount,
		.elementByteSize = sizeof(EngineMaterial),
		.isAccessible = false
	};
	EngineCreateBuffer(engine, &engine->materialBuffer, ENGINE_BUFFER_UNIFORM);
	debug_msg("materialBuffer address: %llu\n", engine->materialBuffer._buffer);

	debug_msg("Material buffer created\n");
	EngineWriteMaterials(engine, material, NULL, materialCount);
	debug_msg("materialBuffer address: %llu\n", engine->materialBuffer._buffer);
	EngineAttachDataInfo attachInfo = {
		.binding = BINDING_MATERIAL_BUFFER,
		.content = {.buffer = engine->materialBuffer},
		.startingIndex = 0,
		.endIndex = 0,
		.type = ENGINE_BUFFER_UNIFORM,
		.applyCount = ENGINE_ATTACH_DATA_ALL_FRAMES,
		.nextFrame = false
	};
	EngineAttachData(engine, attachInfo);
	debug_msg("materials loaded\n");
};
void EngineWriteMaterials(Engine *engine, EngineMaterial *material, size_t *indices, size_t indexCount) {
	EngineBufferAccessUpdate(engine, &engine->materialBuffer, true);
	EngineMaterial *materialMem = engine->materialBuffer.data;
	if(!indices) {
		for(size_t i = 0; i < indexCount; i++) {
			materialMem[i] = material[i];
		}
	} else {
		for(size_t i = 0; i < indexCount; i++) {
			materialMem[indices[i]] = material[i];
		}
	}

	debug_msg("data copied\n");
	EngineBufferAccessUpdate(engine, &engine->materialBuffer, false);
}
void EngineUnloadMaterials(Engine *engine) {
	EngineDestroyBuffer(engine, engine->materialBuffer);
}

// typedef struct {
//     uint32_t type;
//     vec4 lightData;
//     vec4 color;
// } EngineLightSource;

void EngineLoadSunlight(Engine *engine, EngineSunlight sunlight) {
	engine->sunlightBuffer = (EngineBuffer){
		.length = 1,
		.elementByteSize = sizeof(EngineSunlight),
		.isAccessible = false
	};
	EngineCreateBuffer(engine, &engine->sunlightBuffer, ENGINE_BUFFER_UNIFORM);

	EngineWriteSunlight(engine, sunlight);
	EngineAttachDataInfo attachInfo = {
		.binding = BINDING_SUNLIGHT_BUFFER,
		.content = {.buffer = engine->sunlightBuffer},
		.startingIndex = 0,
		.endIndex = 0,
		.type = ENGINE_BUFFER_UNIFORM,
		.applyCount = ENGINE_ATTACH_DATA_ALL_FRAMES,
		.nextFrame = false
	};
	EngineAttachData(engine, attachInfo);
}
void EngineWriteSunlight(Engine *engine, EngineSunlight sunlight) {
	EngineBufferAccessUpdate(engine, &engine->sunlightBuffer, true);
	EngineSunlight *mem = engine->sunlightBuffer.data;
	*mem = sunlight;
	EngineBufferAccessUpdate(engine, &engine->sunlightBuffer, false);
}
void EngineUnloadSunlight(Engine *engine) {
	EngineDestroyBuffer(engine, engine->sunlightBuffer);
}

// typedef struct {
//     vec3 origin;
//     vec3 direction;
// } EngineCamera;

void EngineCreateCamera(Engine *engine, EngineCamera **camera) {
	engine->cameraBuffer = (EngineBuffer){
		.length = 1,
		.elementByteSize = sizeof(EngineCamera),
		.isAccessible = true
	};
	EngineCreateBuffer(engine, &engine->cameraBuffer, ENGINE_BUFFER_STORAGE);
	EngineAttachDataInfo attachInfo = {
		.binding = BINDING_CAMERA_BUFFER,
		.content = {.buffer = engine->cameraBuffer},
		.startingIndex = 0,
		.endIndex = 0,
		.type = ENGINE_BUFFER_STORAGE,
		.applyCount = ENGINE_ATTACH_DATA_ALL_FRAMES,
		.nextFrame = false
	};
	EngineAttachData(engine, attachInfo);
	*camera = engine->cameraBuffer.data;
}
void EngineDestroyCamera(Engine *engine) {
	EngineDestroyBuffer(engine, engine->cameraBuffer);
}