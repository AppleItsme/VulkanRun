#include "vk.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

typedef struct engine_h {
	size_t testNumber;
    VkDevice device;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    GLFWwindow *window;

    VkSurfaceKHR surface;
    struct {
		uint32_t graphics;
		uint32_t presentation;
		uint32_t compute;
	} queue_indices;
	struct {
		VkQueue graphics;
		VkQueue presentation;
		VkQueue compute;
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

	#ifndef NDEBUG
	VkDebugUtilsMessengerEXT debugMessenger;
	#endif
} Engine;


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
	VkPresentModeKHR *presentMode = NULL;
	bool suitableDeviceFound = false;

	for(int i = 0; i < deviceCount; i++) {
		VkPhysicalDeviceProperties props = {0};
		vkGetPhysicalDeviceProperties(devices[i], &props);
		printf("Device Name: %s\n", props.deviceName);

		size_t graphicsI = -1, presentationI = -1, computeI = -1;
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
			VkBool32 presentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], j, engine->surface, &presentSupport);
			if(presentSupport) {
				presentationI = j;
			}
			if(graphicsI > -1 && presentationI > -1 && computeI > -1)
				break;
		}
		if(graphicsI == -1 || presentationI == -1 || computeI == -1) {
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
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[i], engine->surface, &engine->swapchainDetails.capabilities);

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


		uint32_t presentModeCount = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(devices[i], engine->surface, &presentModeCount, NULL);
		if(presentModeCount == 0)
			continue;
		if(presentModeCount > presentMemSize) {
			if(presentMode == NULL) {
				presentMode = malloc(sizeof(VkPresentModeKHR) * presentModeCount);
			} else {
				presentMode = realloc(presentMode, sizeof(VkPresentModeKHR) * presentModeCount);
			}
			presentMemSize = presentModeCount;
		}
		vkGetPhysicalDeviceSurfacePresentModesKHR(devices[i], engine->surface, &presentModeCount, presentMode);
		VkPresentModeKHR bestPresentMode = VK_PRESENT_MODE_FIFO_KHR;
		for(int i = 0; i < presentModeCount; i++) {
			if(presentMode[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
				bestPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
				break;
			}
		}
		if(desiredFeatures13.dynamicRendering && 
			desiredFeatures13.synchronization2 &&
			desiredFeatures12.bufferDeviceAddress &&
			desiredFeatures12.descriptorIndexing) {
			engine->physicalDevice = devices[i];
			engine->queue_indices.graphics = graphicsI;
			engine->queue_indices.compute = computeI;
			engine->queue_indices.presentation = presentationI;
			engine->swapchainDetails.format = formats[formatIndex];
			engine->swapchainDetails.presentMode = bestPresentMode;
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
	free(presentMode);
	free(formats);
	ERR_CHECK(suitableDeviceFound, INAPPROPRIATE_GPU, VK_SUCCESS);
	return ENGINE_RESULT_SUCCESS;
}

EngineResult EngineSwapchainCreate(Engine *engine) {
	VkExtent2D actualExtent = {0};
	glfwGetFramebufferSize(engine->window, &actualExtent.width, &actualExtent.height);
	engine->pixelResolution.width = clampU32(actualExtent.width, engine->swapchainDetails.capabilities.minImageExtent.width, 
													engine->swapchainDetails.capabilities.maxImageExtent.width);
	engine->pixelResolution.height = clampU32(actualExtent.height, engine->swapchainDetails.capabilities.minImageExtent.height, 
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
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, //IF YOU WANNA RENDER TO ANOTHER IMAGE FIRST, PUT VK_IMAGE_USAGE_TRANSFER_ATTACHMENT_BIT
		.preTransform = engine->swapchainDetails.capabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.clipped = VK_TRUE,
		.oldSwapchain = engine->oldSwapchain,
		.minImageCount = imageCount
	};
	
	//MIGHT change it to compute instead of graphics
	uint32_t queueFamilyIndices[2] = {engine->queue_indices.graphics, engine->queue_indices.presentation};
	if(engine->queue_indices.graphics == engine->queue_indices.presentation) {
		swapchainCI.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		swapchainCI.queueFamilyIndexCount = 2;
		swapchainCI.pQueueFamilyIndices = &queueFamilyIndices;
	} else {
		swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
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

EngineResult EngineInit(Engine **engine_instance, EngineCI engineCI) {
	Engine *engine = malloc(sizeof(Engine));
	engine->oldSwapchain = VK_NULL_HANDLE;
	size_t glfwExtensionsCount = 0;
	ERR_CHECK(glfwInit() == GLFW_TRUE, GLFW_CANNOT_INIT, VK_SUCCESS);
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	engine->window = glfwCreateWindow(engineCI.width, engineCI.height, engineCI.displayName, NULL, NULL);

	#ifndef NDEBUG
	ERR_CHECK(checkValidationSupport(), DEBUG_CREATION_FAILED, VK_SUCCESS);
	#endif

	const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);
	printf("===GLFW EXTENSIONS===\n");
	for(int i = 0; i < glfwExtensionsCount; i++) {
		printf("%s\n", glfwExtensions[i]);
	}

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
		.enabledExtensionCount = glfwExtensionsCount,
		.ppEnabledExtensionNames = glfwExtensions,
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

	res = glfwCreateWindowSurface(engine->instance, engine->window, NULL, &engine->surface);
	ERR_CHECK(res == VK_SUCCESS, WINDOW_CREATION_FAILED, res);


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

	*engine_instance = engine;
	return ENGINE_RESULT_SUCCESS;
};

void EngineDestroy(Engine *engine) {
	vkDestroyDevice(engine->device, NULL);
	vkDestroySurfaceKHR(engine->instance, engine->surface, NULL);
	vkDestroyInstance(engine->instance, NULL);
	glfwDestroyWindow(engine->window);
	glfwTerminate();
	free(engine);
}