#include <Engine.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define WIDTH 800
#define HEIGHT 600
#define DISPLAY_NAME "mijn vulkan app"

EngineResult res;
Engine *engine_instance = NULL;
GLFWwindow *window = NULL;
EngineImage renderImages[2];

struct {
	uint32_t width, height;
} bufferSize;

void window_size_callback(GLFWwindow *window, int width, int height) {
	glfwGetFramebufferSize(window, &bufferSize.width, &bufferSize.height);
	EngineSwapchainDestroy(engine_instance);
	res = EngineSwapchainCreate(engine_instance, bufferSize.width, bufferSize.height, renderImages);
	if(res.EngineCode != ENGINE_SUCCESS) {
		printf("swapchain failed: %d\n", res.VulkanCode);
		exit(-1);
	}
}

int main() {
	#ifndef NDEBUG
		printf("DEBUG IS ON\n");
	#endif
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	GLFWwindow *window = glfwCreateWindow(WIDTH, HEIGHT, DISPLAY_NAME, NULL, NULL);
	glfwSetWindowSizeLimits(window, 200, 200, GLFW_DONT_CARE, GLFW_DONT_CARE);
	EngineCI engineCreateInfo = {
		.appName = "mammamia",
		.displayName = DISPLAY_NAME,
		.appVersion = MAKE_VERSION(0,0,1),
	};	
	engineCreateInfo.extensions = glfwGetRequiredInstanceExtensions(&engineCreateInfo.extensionsCount);
	
	uintptr_t surface = 0, vkInstance = 0;
	res = EngineInit(&engine_instance, engineCreateInfo, &vkInstance);
	if(res.EngineCode != ENGINE_SUCCESS) {
		exit(-1);
	}
	glfwCreateWindowSurface(vkInstance, window, NULL, &surface);
	res = EngineFinishSetup(engine_instance, surface);
	if(res.EngineCode != ENGINE_SUCCESS) {
		printf("Setup failed: %d %d\n", res.EngineCode, res.VulkanCode);
		exit(-1);
	}

	glfwGetFramebufferSize(window, &bufferSize.width, &bufferSize.height);
	glfwSetWindowSizeCallback(window, window_size_callback);
	res = EngineSwapchainCreate(engine_instance, bufferSize.width, bufferSize.height, renderImages);
	if(res.EngineCode != ENGINE_SUCCESS) {
		printf("swapchain failed\n %d", res.VulkanCode);
		exit(-1);
	}
	EngineDataTypeInfo dTypes[] = {
		{
			.bindingIndex = 0,
			.length = 1,
			.type = ENGINE_IMAGE
		},
		{
			.bindingIndex = 1,
			.length = 1,
			.type = ENGINE_BUFFER
		}
	};	
	
	res = EngineDeclareDataSet(engine_instance, dTypes, sizeof(dTypes)/sizeof(dTypes[0]));
	if(res.EngineCode != ENGINE_SUCCESS) {
		printf("Dataset declaration failed!: %d, %d\n", res.EngineCode, res.VulkanCode);
		exit(-1);
	}
	FILE *shader = fopen("C:/Users/akseg/Documents/Vulkan/src/shaders/gradient.spv", "rb");
	if(shader == NULL) {
		printf("womp womp bad path\n");
		exit(-1);
	}
	fseek(shader, 0, SEEK_END);
	size_t sz = ftell(shader);
	fseek(shader, 0, SEEK_SET);
	char *shaderCode = malloc(sizeof(char)*sz);
	fread(shaderCode, sizeof(char), sz, shader);
	fclose(shader);

	EngineShaderInfo shaderInfo = {
		.byteSize = sz,
		.code = shaderCode
	};

	res = EngineLoadShaders(engine_instance, &shaderInfo, 1);
	if(res.EngineCode != ENGINE_SUCCESS) {
		printf("Could not load shaders: %d %d\n", res.EngineCode, res.VulkanCode);
		exit(-1);
	}
	free(shaderCode);

	EngineSemaphore drawWaitSemaphore[2] = {0};
	EngineSemaphore commandDoneSemaphore[2] = {0};
	for(int i = 0; i < 2; i++) {
		EngineCreateSemaphore(engine_instance, &commandDoneSemaphore[i]);
	}

	EngineBuffer buffer = {
		.elementByteSize = sizeof(float),
		.length = 2,
	};
	res = EngineCreateBuffer(engine_instance, &buffer);
	if(res.EngineCode != ENGINE_SUCCESS) {
		printf("Could not create buffer: %d %d\n", res.EngineCode, res.VulkanCode);
		exit(-1);
	}

	glfwSetTime(0);

	float arr[2] = {100, 120};
	printf("loop begins!\n");
	while(!glfwWindowShouldClose(window)) {
		EngineColor Color = {0, 0, 1, 1};
		res = EngineDrawStart(engine_instance, Color, &drawWaitSemaphore[EngineGetFrame(engine_instance)]);
		if(res.EngineCode != ENGINE_SUCCESS) {
			printf("drawstart failed\n %d", res.VulkanCode);
			exit(-1);
		}
		EngineWriteDataInfo dataInfo = {
			.binding = 0,
			.startingIndex = 0,
			.endIndex = 0,
			.type = ENGINE_IMAGE,
			.content.image = renderImages[EngineGetFrame(engine_instance)],
		};
		EngineWriteData(engine_instance, &dataInfo);
		
		float tmp[2] = {arr[0], arr[1]};
		float time = glfwGetTime();
		tmp[0] += sinf(time) * 10;
		tmp[1] += cosf(time) * 20;
		memcpy(buffer.data, tmp, sizeof(float) * 2);
		dataInfo = (EngineWriteDataInfo) {
			.binding = 1,
			.startingIndex = 0,
			.endIndex = 0,
			.type = ENGINE_BUFFER,
			.content.buffer = buffer
		};
		EngineWriteData(engine_instance, &dataInfo);
		
		EngineCommand cmd = 0;
		EngineStartCommand(engine_instance, &cmd);
		EngineShaderRunInfo runInfo = {
			.groupSizeX = ceilf((float)bufferSize.width/16),
			.groupSizeY = ceilf((float)bufferSize.height/16),
			.groupSizeZ = 1
		};
		EngineRunShader(engine_instance, cmd, 0, runInfo);
		EngineEndCommand(engine_instance, cmd, &drawWaitSemaphore[EngineGetFrame(engine_instance)], &commandDoneSemaphore[EngineGetFrame(engine_instance)]);
		res = EngineDrawEnd(engine_instance, &commandDoneSemaphore[EngineGetFrame(engine_instance)]);
		if(res.EngineCode != ENGINE_SUCCESS) {
			printf("drawend failed\n %d", res.VulkanCode);
			exit(-1);
		}
		glfwPollEvents();
	}
	EngineDestroyBuffer(engine_instance, buffer);
	EngineSwapchainDestroy(engine_instance);
	for(int i = 0; i < 2; i++) {
		EngineDestroySemaphore(engine_instance, commandDoneSemaphore[i]);
	}
	EngineDestroy(engine_instance);
	glfwDestroyWindow(window);
	glfwTerminate();
}