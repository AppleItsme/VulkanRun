#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include <varargs.h>

#define WIDTH 800
#define HEIGHT 600
#define DISPLAY_NAME "mijn vulkan app"

inline void errCheck(bool condition, const char *msg, ...) {
	va_list arg;
	if(condition) {
		va_start(arg);
		fprintf(msg, arg);
		va_end(arg);
		exit(-1);
	}
}

inline uint32_t clampU32(uint32_t val, uint32_t min, uint32_t max) {
	val = val < min ? min : val;
	return val > max ? max : val;
}

VkResult res;

#ifdef DEBUG

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

void validationLayerInit(VkInstanceCreateInfo *createInfo) {
	uint32_t layerPropCount = 0;
	vkEnumerateInstanceLayerProperties(&layerPropCount, NULL);
	VkLayerProperties *layerProp = malloc(sizeof(VkLayerProperties) * layerPropCount);

	res = vkEnumerateInstanceLayerProperties(&layerPropCount, layerProp);
	errCheck(res == VK_SUCCESS, "Could not obtain instance layer properties (error code: %d)\n", res);

	uint32_t validationLayersFound = 0;

	for(int i = 0; i < layerPropCount; i++) {
		for(int j = 0; j < VALIDATION_LAYER_LENGTH; j++)
		if(!strcmp(layerProp[i].layerName, validationLayers[j])) {
			validationLayersFound++;
			if(validationLayersFound == VALIDATION_LAYER_LENGTH)
				break;
		}
	}
	errCheck(validationLayersFound < VALIDATION_LAYER_LENGTH, "The desired validation layers are not supported (%d/%d layers supported)\n",
			validationlayersFound,
			VALIDATION_LAYER_LENGTH);
	createInfo->enabledLayerCount = VALIDATION_LAYER_LENGTH;
	createInfo->ppEnabledLayerNames = validationLayers;
	free(layerProp);
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

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    PFN_vkCreateDebugUtilsMessengerEXT func = vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    PFN_vkDestroyDebugUtilsMessengerEXT func = vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

void setupDebugMessenger(VkDebugUtilsMessengerEXT *debugMessenger, VkInstance *instance) {
	VkDebugUtilsMessengerCreateInfoEXT createInfo = {0};
	createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	createInfo.pfnUserCallback = debugCallback;
	VkResult res = vkCreateDebugUtilsMessengerEXT(instance, &createInfo, NULL, debugMessenger);
	errCheck(res != VK_SUCCESS, "Could not create debug utils messenger. (error code: %d)", res);
}

#endif

const char desiredInstanceExtensions[][VK_MAX_EXTENSION_NAME_SIZE] = {
	#ifdef DEBUG
	VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
	#endif
	VK_KHR_SURFACE_EXTENSION_NAME,
	
};

const char desiredDeviceExtensions[][VK_MAX_EXTENSION_NAME_SIZE] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#define desiredDeviceExtensionsCount sizeof(desiredDeviceExtensions)/VK_MAX_EXTENSION_NAME_SIZE
#define desiredInstanceExtensionsCount  sizeof(desiredInstanceExtensions)/VK_MAX_EXTENSION_NAME_SIZE

int main() {
	errCheck(!glfwInit(), "GLFW cannot be initialised!!\n");
	VkApplicationInfo appInfo = {
		.apiVersion = VK_API_VERSION_1_3,
		.applicationVersion = VK_MAKE_VERSION(1,0,0),
		.engineVersion = VK_MAKE_VERSION(0,0,0),
		.pApplicationName = "e",
		.pEngineName = "e",
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO
	};
	VkInstanceCreateInfo createInfo = {0};
	createInfo.enabledLayerCount = 0;
	createInfo.flags = 0;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	
	const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&createInfo.enabledExtensionCount);

	createInfo.enabledExtensionCount += desiredInstanceExtensionsCount;
	char **extensions = malloc((createInfo.enabledExtensionCount) * VK_MAX_EXTENSION_NAME_SIZE);
	for(int i = 0; i < desiredInstanceExtensionsCount; i++) {
		strcpy(extensions[createInfo.enabledExtensionCount+i], desiredInstanceExtensions[i]);
	}
	for(int i = desiredInstanceExtensionsCount; i < createInfo.enabledExtensionCount; i++) {
		strcpy(extensions[i], glfwExtensions[i]);
	}
	
	#ifdef DEBUG
	validationLayerInit(&createInfo);
	#endif



	VkInstance instance = {0};
	res = vkCreateInstance(&createInfo, NULL, &instance);
	free(extensions);
	errCheck(res == VK_SUCCESS, "Could not create Vulkan Instance (error code: %d)\n", res);
	#ifdef DEBUG
	VkDebugUtilsMessengerEXT debugMessenger = {0};
	setupDebugMessenger(&debugMessenger, &instance);
	#endif

	GLFWwindow *window = glfwCreateWindow(WIDTH, HEIGHT, DISPLAY_NAME, NULL, NULL);
	errCheck(!window, "Could not create window!");
	VkSurfaceKHR surface;
	res = glfwCreateWindowSurface(instance, window, NULL, &surface);
	errCheck(res != VK_SUCCESS, "Could not create window surface! (error code: %d)\n", res); 



	VkPhysicalDevice chosenPhysicalDevice;

	uint32_t physicalDeviceCount = 0;
	vkEnumeratePhysicalDevices(&instance, &physicalDeviceCount, NULL);
	VkPhysicalDevice *physicalDevices = malloc(sizeof(VkPhysicalDevice) * physicalDeviceCount);
	vkEnumeratePhysicalDevices(&instance, &physicalDeviceCount, physicalDevices);

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
	errCheck(gpuIndex == -1, "Could not find a suitable gpu\n");
	FOUND_DISCRETE:
	chosenPhysicalDevice = physicalDevices[gpuIndex];
	free(physicalDevices);

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(chosenPhysicalDevice, &queueFamilyCount, NULL);
	VkQueueFamilyProperties *queueFamilies = malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties(chosenPhysicalDevice, &queueFamilyCount, &queueFamilies);

	struct {
		int32_t graphics;
		int32_t presentation;
	} queueIndices = {-1, -1};

	for(int i = 0; i < queueFamilyCount; i++) {
		if(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			queueIndices.graphics = i;
		}
		VkBool32 surfaceSupported;
		res = vkGetPhysicalDeviceSurfaceSupportKHR(chosenPhysicalDevice, i, surface, &surfaceSupported);
		if(surfaceSupported) {
			queueIndices.presentation = i;
		}
		if(queueIndices.graphics >= 0 && queueIndices.presentation >= 0) {
			break;
		}
	}
	errCheck(queueIndices.graphics == -1, "Could not find a graphics capable queue family!\n");
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
	vkEnumerateDeviceExtensionProperties(chosenPhysicalDevice, NULL, &deviceExtensionCount, NULL);
	VkExtensionProperties *deviceExtensions = malloc(sizeof(VkExtensionProperties) * deviceExtensionCount);
	vkEnumerateDeviceExtensionProperties(chosenPhysicalDevice, NULL, &deviceExtensionCount, &desiredDeviceExtensions);

	uint32_t desiredExtensionsFound = 0;
	for(int i = 0; i < deviceExtensionCount; i++) {
		for(int j = 0; j < desiredDeviceExtensionsCount; j++) {
			if(!strcmp(deviceExtensions[i].extensionName, desiredDeviceExtensions[j])) {
				desiredExtensionsFound++;
			}
		}
	}
	errCheck(desiredExtensionsFound < desiredDeviceExtensionsCount, "Your device does not support the required extensions (%d/%d extensions supported)\n", 
		desiredExtensionsFound, 
		desiredDeviceExtensionsCount);
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
	vkGetPhysicalDeviceFeatures(chosenPhysicalDevice, deviceCreateInfo.pEnabledFeatures);

	VkDevice device = {0};
	res = vkCreateDevice(chosenPhysicalDevice, &deviceCreateInfo, NULL, &device);
	errCheck(res != VK_SUCCESS, "Could not create device! (error code: %d)\n", res);
	VkQueue graphiscQueue;
	vkGetDeviceQueue(device, queueIndices.graphics, 0, &graphiscQueue);
	
	VkQueue presentationQueue;
	vkGetDeviceQueue(device, queueIndices.presentation, 0, &presentationQueue);
	
		struct {
		VkSurfaceCapabilitiesKHR capabilities;
		VkSurfaceFormatKHR *format;
		VkPresentModeKHR *presentMode;
	} swapchainSupport;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(chosenPhysicalDevice, surface, &swapchainSupport.capabilities);
	
	uint32_t formatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(chosenPhysicalDevice, surface, &formatCount, NULL);
	swapchainSupport.format = malloc(sizeof(VkSurfaceFormatKHR) * formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(chosenPhysicalDevice, surface, &formatCount, &swapchainSupport.format);

	uint32_t presentCount = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(chosenPhysicalDevice, surface, &presentCount, NULL);
	swapchainSupport.presentMode = malloc(sizeof(VkSurfaceFormatKHR) * presentCount);
	vkGetPhysicalDeviceSurfacePresentModesKHR(chosenPhysicalDevice, surface, &presentCount, NULL);
	errCheck(formatCount == 0, "Could not find adequate swapchain\n");

	uint32_t preferredFormat = 0;
	for(; preferredFormat < formatCount; preferredFormat++) {
		if(swapchainSupport.format[preferredFormat].format == VK_FORMAT_B8G8R8_SRGB && 
			swapchainSupport.format[preferredFormat].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				break;
		}
	}
	if(preferredFormat >= formatCount)
		preferredFormat = 0;

	uint32_t preferredPresentMode = 0;
	for(uint32_t i = 0; i < presentCount; i++) {
		switch(swapchainSupport.presentMode[i]) {
		case VK_PRESENT_MODE_MAILBOX_KHR:
			preferredPresentMode = i;
			goto FOUND_BEST_PRESENT_MODE;
		case VK_PRESENT_MODE_FIFO_KHR:
			preferredPresentMode = i;
		}
	}
	FOUND_BEST_PRESENT_MODE:

	if(swapchainSupport.capabilities.currentExtent.width != UINT32_MAX) {
		goto CURRENT_EXTENT_SELECTED;
	}
	VkExtent2D actualExtent = {0};
	glfwGetFramebufferSize(window, &actualExtent.width, &actualExtent.height);
	actualExtent.width = clampU32(actualExtent.width, swapchainSupport.capabilities.minImageExtent.width, 
													  swapchainSupport.capabilities.maxImageExtent.width);
	actualExtent.height = clampU32(actualExtent.height, swapchainSupport.capabilities.minImageExtent.height, 
													swapchainSupport.capabilities.maxImageExtent.height);

	CURRENT_EXTENT_SELECTED:
	uint32_t imageCount = swapchainSupport.capabilities.minImageCount + 1;
	if(swapchainSupport.capabilities.maxImageCount > 0 && imageCount > swapchainSupport.capabilities.maxImageCount)
		imageCount = swapchainSupport.capabilities.maxImageCount;
	VkSwapchainCreateInfoKHR swapchainCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = imageCount,
		.imageFormat = swapchainSupport.format[preferredFormat].format,
		.imageColorSpace = swapchainSupport.format[preferredFormat].colorSpace,
		.presentMode = swapchainSupport.presentMode[preferredPresentMode],
		.imageExtent = actualExtent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = swapchainSupport.capabilities.currentTransform,
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


	VkSwapchainKHR swapchain = {0};
	res = vkCreateSwapchainKHR(device, &swapchainCreateInfo, NULL, &swapchain);
	errCheck(!res, "Swapchain could not be created (error code: %d)\n", res);

	uint32_t swapchainImageCount = 0;
	vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, NULL);
	VkImage *swapchainImages = malloc(sizeof(VkImage) * swapchainImageCount);
	vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, &swapchainImages);

	vkDestroySwapchainKHR(device, swapchain, NULL);
	vkDestroyDevice(device, NULL);
	#ifdef DEBUG
	vkDestroyDebugUtilsMessengerEXT(&instance, debugMessenger, NULL);
	#endif
	vkDestroySurfaceKHR(instance, surface, NULL);
	vkDestroyInstance(instance, NULL);
	glfwTerminate();
}
