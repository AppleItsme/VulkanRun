#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include <varargs.h>

inline void errCheck(bool condition, const char *msg, ...) {
	va_list arg;
	if(condition) {
		va_start(arg);
		fprintf(msg, arg);
		va_end(arg);
		exit(-1);
	}
}

VkResult res;

#ifdef DEBUG

#define VALIDATION_LAYER_LENGTH 1

const char *validationLayers[VALIDATION_LAYER_LENGTH] = {
	"VK_LAYER_KHRONOS_validation"
};

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void validationLayerInit(VkInstanceCreateInfo *createInfo) {
	uint32_t layerPropCount = 0;
	vkEnumerateInstanceLayerProperties(&layerPropCount, NULL);
	VkLayerProperties *layerProp = malloc(sizeof(VkLayerProperties) * layerPropCount);

	res = vkEnumerateInstanceLayerProperties(&layerPropCount, layerProp);
	errCheck(res == VK_SUCCESS, "Could not obtain instance layer properties (error code: %d)", res);

	uint32_t validationLayersFound = 0;

	for(int i = 0; i < layerPropCount; i++) {
		for(int j = 0; j < VALIDATION_LAYER_LENGTH; j++)
		if(!strcmp(layerProp[i].layerName, validationLayers[j])) {
			validationLayersFound++;
			if(validationLayersFound == VALIDATION_LAYER_LENGTH)
				break;
		}
	}
	errCheck(validationLayersFound < VALIDATION_LAYER_LENGTH, "The desired validation layers are not supported");
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

const char EXTENSIONS[][VK_MAX_EXTENSION_NAME_SIZE] = {
	#ifdef DEBUG
	VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
	#endif
	VK_KHR_SURFACE_EXTENSION_NAME,
	
};

#define EXTENSIONS_SIZE  sizeof(EXTENSIONS)/VK_MAX_EXTENSION_NAME_SIZE

int main() {
	errCheck(!glfwInit(), "GLFW cannot be initialised!!");
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

	createInfo.enabledExtensionCount += EXTENSIONS_SIZE;
	char **extensions = malloc((createInfo.enabledExtensionCount) * VK_MAX_EXTENSION_NAME_SIZE);
	for(int i = 0; i < EXTENSIONS_SIZE; i++) {
		strcpy(extensions[createInfo.enabledExtensionCount+i], EXTENSIONS[i]);
	}
	for(int i = EXTENSIONS_SIZE; i < createInfo.enabledExtensionCount; i++) {
		strcpy(extensions[i], glfwExtensions[i]);
	}

	
	#ifdef DEBUG
	validationLayerInit(&createInfo);
	#endif



	VkInstance instance = {0};
	res = vkCreateInstance(&createInfo, NULL, &instance);
	free(extensions);
	errCheck(res == VK_SUCCESS, "Could not create Vulkan Instance (error code: %d)", res);
	#ifdef DEBUG
	VkDebugUtilsMessengerEXT debugMessenger = {0};
	setupDebugMessenger(&debugMessenger, &instance);
	#endif

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
	errCheck(gpuIndex == -1, "Could not find a suitable gpu");
	FOUND_DISCRETE:
	chosenPhysicalDevice = physicalDevices[gpuIndex];
	free(physicalDevices);

	#ifdef DEBUG
	vkDestroyDebugUtilsMessengerEXT(&instance, debugMessenger, NULL);
	#endif
	vkDestroyInstance(instance, NULL);
	glfwTerminate();
}
