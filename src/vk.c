#include "vk.h"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include <varargs.h>

typedef struct engine_h {
    VkDevice device;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    GLFWwindow *window;

    VkSurfaceKHR surface;
    VkQueue graphicsQueue, presentationQueue;
    struct {
		VkSurfaceCapabilitiesKHR capabilities;
		VkSurfaceFormatKHR *format;
		VkPresentModeKHR *presentMode;
	} swapchainSupport;
    uint32_t format, presentMode;
	
	VkSwapchainKHR swapchain;
	VkImage *swapchainImages;
	VkImageView *swapchainImageViews;
	uint32_t swapchainImageCount;

	#ifndef NDEBUG
	VkDebugUtilsMessengerEXT debugMessenger;
	#endif
} Engine;

bool EngineWindowShouldClose(Engine *engine) {
	return glfwWindowShouldClose(engine->window);
}

void EngineRenderingDone(Engine *engine) {
	glfwPollEvents();
	glfwSwapBuffers(engine->window);
}

inline uint32_t clampU32(uint32_t val, uint32_t min, uint32_t max) {
	val = val < min ? min : val;
	return val > max ? max : val;
}

#define ERROR_CHECK(condition, EngineFailcode, VulkanFailcode) \
    if(condition) { \
        return (EngineResult){EngineFailcode, VulkanFailcode}; \
    } 

#define MALLOC(var_type, variable, size) \
	var_type variable = malloc(size);\
	if(variable == NULL) \
		return (EngineResult) {OUT_OF_MEMORY, VK_SUCCESS};

#ifndef NDEBUG

const char validationLayers[][VK_MAX_EXTENSION_NAME_SIZE] = {
	"VK_LAYER_KHRONOS_validation"
};

#define VALIDATION_LAYER_LENGTH sizeof(validationLayers)/VK_MAX_EXTENSION_NAME_SIZE

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    PFN_vkCreateDebugUtilsMessengerEXT func = vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != NULL) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

EngineResult validationLayerInit(VkInstanceCreateInfo *createInfo) {
	uint32_t layerPropCount = 0;
	vkEnumerateInstanceLayerProperties(&layerPropCount, NULL);
	MALLOC(VkLayerProperties*, layerProp, sizeof(VkLayerProperties) * layerPropCount);

	VkResult res = vkEnumerateInstanceLayerProperties(&layerPropCount, layerProp);
	ERROR_CHECK(res != VK_SUCCESS, DEBUG_INFO_NOT_FOUND, res);

	uint32_t validationLayersFound = 0;

	for(int i = 0; i < layerPropCount; i++) {
		for(int j = 0; j < VALIDATION_LAYER_LENGTH; j++)
		if(!strcmp(layerProp[i].layerName, validationLayers[j])) {
			validationLayersFound++;
			if(validationLayersFound == VALIDATION_LAYER_LENGTH)
				break;
		}
	}
	ERROR_CHECK(validationLayersFound < VALIDATION_LAYER_LENGTH, DEBUG_INFO_NOT_FOUND, VK_SUCCESS);
	createInfo->enabledLayerCount = VALIDATION_LAYER_LENGTH;
	createInfo->ppEnabledLayerNames = validationLayers;
	free(layerProp);
	return (EngineResult) {SUCCESS, VK_SUCCESS};
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

	if(messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    fprintf(stderr, "validation layer: %s\n", pCallbackData->pMessage);

    return VK_FALSE;
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    PFN_vkDestroyDebugUtilsMessengerEXT func = vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != NULL) {
        func(instance, debugMessenger, pAllocator);
    }
}

EngineResult setupDebugMessenger(VkDebugUtilsMessengerEXT *debugMessenger, VkInstance *instance) {
	VkDebugUtilsMessengerCreateInfoEXT createInfo = {0};
	createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	createInfo.pfnUserCallback = debugCallback;
	VkResult res = CreateDebugUtilsMessengerEXT(instance, &createInfo, NULL, debugMessenger);
	ERROR_CHECK(res != VK_SUCCESS, DEBUG_CREATION_FAILED, res);
	return (EngineResult) {SUCCESS, VK_SUCCESS};
}

#endif

const char desiredInstanceExtensions[][VK_MAX_EXTENSION_NAME_SIZE] = {
	#ifndef NDEBUG
	VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
	#endif
	VK_KHR_SURFACE_EXTENSION_NAME,
	
};

const char desiredDeviceExtensions[][VK_MAX_EXTENSION_NAME_SIZE] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

char* readFile(char *path) {
	FILE *file = fopen(path, "r");
	fseek(file, 0, SEEK_END);
	size_t len = ftell(file);
	fseek(file, 0, SEEK_SET);

	char *txt = malloc(len); 
	fread(file, sizeof(char), len, file);
	fclose(file);
	return txt;
}

#define desiredDeviceExtensionsCount sizeof(desiredDeviceExtensions)/VK_MAX_EXTENSION_NAME_SIZE
#define desiredInstanceExtensionsCount  sizeof(desiredInstanceExtensions)/VK_MAX_EXTENSION_NAME_SIZE

EngineResult engine_init(Engine *engine, EngineCI engineCI) {
    VkResult res;
	ERROR_CHECK(!glfwInit(), GLFW_CANNOT_INIT, VK_SUCCESS);

	VkApplicationInfo appInfo = {
		.apiVersion = VK_API_VERSION_1_3,
		.applicationVersion = engineCI.appVersion,
		.engineVersion = VK_MAKE_VERSION(0,0,1),
		.pApplicationName = engineCI.appName,
		.pEngineName = "vulkanik",
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO
	};
	VkInstanceCreateInfo createInfo = {0};
	createInfo.enabledLayerCount = 0;
	createInfo.flags = 0;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	
	const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&createInfo.enabledExtensionCount);

	createInfo.enabledExtensionCount += desiredInstanceExtensionsCount;
	MALLOC(char**, extensions, ((createInfo.enabledExtensionCount) * VK_MAX_EXTENSION_NAME_SIZE));
	for(int i = 0; i < desiredInstanceExtensionsCount; i++) {
		strcpy(extensions[createInfo.enabledExtensionCount+i], desiredInstanceExtensions[i]);
	}
	for(int i = desiredInstanceExtensionsCount; i < createInfo.enabledExtensionCount; i++) {
		strcpy(extensions[i], glfwExtensions[i]);
	}
	
	#ifndef NDEBUG
	EngineResult debug_res = validationLayerInit(&createInfo);
	if(debug_res.EngineCode != SUCCESS)
		return debug_res;
	#endif

	MALLOC(,engine, sizeof(Engine));
	res = vkCreateInstance(&createInfo, NULL, &engine->instance);
	free(extensions);
	ERROR_CHECK(res != VK_SUCCESS, INSTANCE_CREATION, res);
	#ifndef NDEBUG
	debug_res = setupDebugMessenger(&engine->debugMessenger, &engine->instance);
	if(debug_res.EngineCode != SUCCESS)
		return debug_res;
	#endif

	engine->window = glfwCreateWindow(engineCI.width, engineCI.height, engineCI.displayName, NULL, NULL);
	ERROR_CHECK(!engine->window, WINDOW_CREATION, VK_SUCCESS);
    res = glfwCreateWindowSurface(engine->instance, engine->window, NULL, &engine->surface) != VK_SUCCESS;
	ERROR_CHECK(res != VK_SUCCESS, WINDOW_CREATION, res);

	uint32_t physicalDeviceCount = 0;
	vkEnumeratePhysicalDevices(&engine->instance, &physicalDeviceCount, NULL);
	MALLOC(VkPhysicalDevice*, physicalDevices, sizeof(VkPhysicalDevice) * physicalDeviceCount);
	vkEnumeratePhysicalDevices(&engine->instance, &physicalDeviceCount, physicalDevices);

	uint32_t gpuIndex = -1;

	for(int i = 0; i < physicalDeviceCount; i++) {
		VkPhysicalDeviceProperties prop = {0};
		vkGetPhysicalDeviceProperties(physicalDevices[i], &prop);
		switch(prop.deviceType) {
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: 
			goto FOUND_DISCRETE;
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
			gpuIndex = i;
		default:
			continue;
		}
	}
	ERROR_CHECK(gpuIndex == -1, GPU_NOT_FOUND, VK_SUCCESS);
	FOUND_DISCRETE:
	engine->physicalDevice = physicalDevices[gpuIndex];
	free(physicalDevices);

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(engine->physicalDevice, &queueFamilyCount, NULL);
	MALLOC(VkQueueFamilyProperties*, queueFamilies, queueFamilyCount * sizeof(VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties(engine->physicalDevice, &queueFamilyCount, &queueFamilies);

	struct {
		int32_t graphics;
		int32_t presentation;
	} queueIndices = {-1, -1};

	for(int i = 0; i < queueFamilyCount; i++) {
		if(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			queueIndices.graphics = i;
		}
		VkBool32 surfaceSupported;
		res = vkGetPhysicalDeviceSurfaceSupportKHR(engine->physicalDevice, i, engine->surface, &surfaceSupported);
		if(surfaceSupported) {
			queueIndices.presentation = i;
		}
		if(queueIndices.graphics >= 0 && queueIndices.presentation >= 0) {
			break;
		}
	}
	ERROR_CHECK(queueIndices.graphics == -1, INAPPROPRIATE_GPU, VK_SUCCESS);
	free(queueFamilies);

	float queuePriority = 1;

	VkDeviceQueueCreateInfo queueCreateInfo[] = {
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = queueIndices.graphics,
			.queueCount = 1,
			.pQueuePriorities = &queuePriority
		},
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = queueIndices.presentation,
			.queueCount = 1,
			.pQueuePriorities = &queuePriority
		}
	};

	uint32_t deviceExtensionCount = 0;
	vkEnumerateDeviceExtensionProperties(engine->physicalDevice, NULL, &deviceExtensionCount, NULL);
	MALLOC(VkExtensionProperties*, deviceExtensions, sizeof(VkExtensionProperties) * deviceExtensionCount);
	vkEnumerateDeviceExtensionProperties(engine->physicalDevice, NULL, &deviceExtensionCount, &desiredDeviceExtensions);

	uint32_t desiredExtensionsFound = 0;
	for(int i = 0; i < deviceExtensionCount; i++) {
		for(int j = 0; j < desiredDeviceExtensionsCount; j++) {
			if(!strcmp(deviceExtensions[i].extensionName, desiredDeviceExtensions[j])) {
				desiredExtensionsFound++;
			}
		}
	}
	ERROR_CHECK(desiredExtensionsFound < desiredDeviceExtensionsCount, INSUFFICIENT_VULKAN, VK_SUCCESS);
	free(deviceExtensions);
	#ifdef DEBUG
	deviceCreateInfo.ppEnabledLayerNames = validationLayers;
	deviceCreateInfo.enabledLayerCount = VALIDATION_LAYER_LENGTH;
	#endif

	VkDeviceCreateInfo deviceCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.enabledExtensionCount = desiredDeviceExtensionsCount,
		.ppEnabledExtensionNames = desiredDeviceExtensions,
		.pQueueCreateInfos = queueCreateInfo,
		.queueCreateInfoCount = sizeof(queueCreateInfo)/sizeof(VkDeviceQueueCreateInfo),
	};
	vkGetPhysicalDeviceFeatures(engine->physicalDevice, deviceCreateInfo.pEnabledFeatures);

	res = vkCreateDevice(engine->physicalDevice, &deviceCreateInfo, NULL, &engine->device);
	ERROR_CHECK(res != VK_SUCCESS, DEVICE_CREATION_FAILED, res);
	VkQueue graphiscQueue;
	vkGetDeviceQueue(engine->device, queueIndices.graphics, 0, &graphiscQueue);
	
	VkQueue presentationQueue;
	vkGetDeviceQueue(engine->device, queueIndices.presentation, 0, &presentationQueue);
	
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(engine->physicalDevice, engine->surface, &engine->swapchainSupport.capabilities);
	
	uint32_t formatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(engine->physicalDevice, engine->surface, &formatCount, NULL);
	MALLOC(,engine->swapchainSupport.format, sizeof(VkSurfaceFormatKHR) * formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(engine->physicalDevice, engine->surface, &formatCount, &engine->swapchainSupport.format);

	uint32_t presentCount = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(engine->physicalDevice, engine->surface, &presentCount, NULL);
	MALLOC(,engine->swapchainSupport.presentMode, sizeof(VkSurfaceFormatKHR) * presentCount);
	vkGetPhysicalDeviceSurfacePresentModesKHR(engine->physicalDevice, engine->surface, &presentCount, NULL);

	uint32_t preferredFormat = 0;
	for(; preferredFormat < formatCount; preferredFormat++) {
		if(engine->swapchainSupport.format[preferredFormat].format == VK_FORMAT_B8G8R8_SRGB && 
			engine->swapchainSupport.format[preferredFormat].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				break;
		}
	}
	if(preferredFormat >= formatCount)
		preferredFormat = 0;

	uint32_t preferredPresentMode = 0;
	for(uint32_t i = 0; i < presentCount; i++) {
		switch(engine->swapchainSupport.presentMode[i]) {
		case VK_PRESENT_MODE_MAILBOX_KHR:
			preferredPresentMode = i;
			goto FOUND_BEST_PRESENT_MODE;
		case VK_PRESENT_MODE_FIFO_KHR:
			preferredPresentMode = i;
		}
	}
	FOUND_BEST_PRESENT_MODE:

	if(engine->swapchainSupport.capabilities.currentExtent.width != UINT32_MAX) {
		goto CURRENT_EXTENT_SELECTED;
	}
	VkExtent2D actualExtent = {0};
	glfwGetFramebufferSize(engine->window, &actualExtent.width, &actualExtent.height);
	actualExtent.width = clampU32(actualExtent.width, engine->swapchainSupport.capabilities.minImageExtent.width, 
													  engine->swapchainSupport.capabilities.maxImageExtent.width);
	actualExtent.height = clampU32(actualExtent.height, engine->swapchainSupport.capabilities.minImageExtent.height, 
													engine->swapchainSupport.capabilities.maxImageExtent.height);

	CURRENT_EXTENT_SELECTED:
	uint32_t imageCount = engine->swapchainSupport.capabilities.minImageCount + 1;
	if(engine->swapchainSupport.capabilities.maxImageCount > 0 && imageCount > engine->swapchainSupport.capabilities.maxImageCount)
		imageCount = engine->swapchainSupport.capabilities.maxImageCount;
	VkSwapchainCreateInfoKHR swapchainCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = engine->surface,
		.minImageCount = imageCount,
		.imageFormat = engine->swapchainSupport.format[preferredFormat].format,
		.imageColorSpace = engine->swapchainSupport.format[preferredFormat].colorSpace,
		.presentMode = engine->swapchainSupport.presentMode[preferredPresentMode],
		.imageExtent = actualExtent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = engine->swapchainSupport.capabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.clipped = VK_TRUE, //VULKAN IGNORES THE PIXELS THAT ARE HIDDEN BY OTHER THINGS
		.oldSwapchain = VK_NULL_HANDLE
	};
	
	uint32_t _queueIndicesArr[] = {queueIndices.graphics, queueIndices.presentation};
	if (queueIndices.graphics != queueIndices.presentation) {
		swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		swapchainCreateInfo.queueFamilyIndexCount = 2;
		swapchainCreateInfo.pQueueFamilyIndices = _queueIndicesArr;
	} else {
		swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		swapchainCreateInfo.queueFamilyIndexCount = 0; // Optional
		swapchainCreateInfo.pQueueFamilyIndices = NULL; // Optional
	}

	res = vkCreateSwapchainKHR(engine->device, &swapchainCreateInfo, NULL, &engine->swapchain);
	ERROR_CHECK(res != VK_SUCCESS, SWAPCHAIN_FAILED, res);

	vkGetSwapchainImagesKHR(engine->device, engine->swapchain, &engine->swapchainImageCount, NULL);
	MALLOC(,engine->swapchainImages, sizeof(VkImage) * engine->swapchainImageCount);
	vkGetSwapchainImagesKHR(engine->device, engine->swapchain, &engine->swapchainImageCount, &engine->swapchainImages);

	
	MALLOC(VkImageView*, swapchainImageViews, engine->swapchainImageCount * sizeof(VkImageView));

	for(int i = 0; i < engine->swapchainImageCount; i++) {
		VkImageViewCreateInfo imageViewCI = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = engine->swapchainImages[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = engine->swapchainSupport.format[preferredFormat].format,
			.components = {
				.r = VK_COMPONENT_SWIZZLE_IDENTITY,
				.g = VK_COMPONENT_SWIZZLE_IDENTITY,
				.b = VK_COMPONENT_SWIZZLE_IDENTITY,
				.a = VK_COMPONENT_SWIZZLE_IDENTITY
			},
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1 
			}
		};
        res = vkCreateImageView(engine->device, &imageViewCI, NULL, engine->swapchainImageViews[i]);
		ERROR_CHECK(res != VK_SUCCESS, IMAGE_VIEW_FAILED, res);
	}

    //return (EngineResult) {SUCCESS, VK_SUCCESS}; (MEANT TO BE HERE)
    return (EngineResult) {SUCCESS, VK_SUCCESS};
}


void createShader(Engine *engine) {
	char *vert_shader = readFile("shaders/vert.spv");
	char *frag_shader = readFile("shaders/frag.spv");

	/*VkShaderModuleCreateInfo shaderModuleCI = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = strlen(vert_shader),
		.
	};*/

	free(vert_shader);
	free(frag_shader);
}

void engine_destroy(Engine *engine) {
    for(int i = 0; i < engine->swapchainImageCount; i++) {
		vkDestroyImageView(engine->device, engine->swapchainImageViews[i], NULL);
	}
	free(engine->swapchainImages);
	vkDestroySwapchainKHR(engine->device, engine->swapchain, NULL);
	vkDestroyDevice(engine->device, NULL);
	#ifdef DEBUG
	vkDestroyDebugUtilsMessengerEXT(&engine->instance, engine->debugMessenger, NULL);
	#endif
	vkDestroySurfaceKHR(engine->instance, engine->surface, NULL);
	vkDestroyInstance(engine->instance, NULL);
	glfwTerminate();
	free(engine);
}