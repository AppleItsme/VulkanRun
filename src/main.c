#include <vk.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define WIDTH 800
#define HEIGHT 600
#define DISPLAY_NAME "mijn vulkan app"

EngineResult res;

int main() {
	#ifndef NDEBUG
		printf("DEBUG IS ON\n");
	#endif
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	GLFWwindow *window = glfwCreateWindow(WIDTH, HEIGHT, DISPLAY_NAME, NULL, NULL);
	EngineCI engineCreateInfo = {
		.appName = "mammamia",
		.displayName = DISPLAY_NAME,
		.appVersion = MAKE_VERSION(0,0,1),
	};	
	engineCreateInfo.extensions = glfwGetRequiredInstanceExtensions(&engineCreateInfo.extensionsCount);
	
	Engine *engine_instance = NULL;
	uintptr_t vkInstance = 0, surface = 0;
	res = EngineInit(&engine_instance, engineCreateInfo, &vkInstance);
	printf("(%d; %d)\n", res.EngineCode, res.VulkanCode);
	if(res.EngineCode != SUCCESS) {
		exit(-1);
	}
	glfwCreateWindowSurface(vkInstance, window, NULL, &surface);
	EngineFinishSetup(engine_instance, surface);
	
	uint32_t frameBufferWidth = 0, frameBufferHeight = 0;
	glfwGetFramebufferSize(window, &frameBufferWidth, &frameBufferHeight);
	res = EngineSwapchainCreate(engine_instance, frameBufferWidth, frameBufferHeight);
	printf("(%d; %d)\n", res.EngineCode, res.VulkanCode);
	if(res.EngineCode != SUCCESS) {
		exit(-1);
	}
	printf("loop begins!!\n");
	while(!glfwWindowShouldClose(window)) {
		res = EngineDraw(engine_instance, (EngineColor){1, 0, 1, 1});
		printf("(%d; %d)\n", res.EngineCode, res.VulkanCode);
		glfwPollEvents();
	}
	EngineSwapchainDestroy(engine_instance);
	EngineDestroy(engine_instance);
	glfwDestroyWindow(window);
	glfwTerminate();
}