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
Engine *engine_instance = NULL;
GLFWwindow *window = NULL;


void window_size_callback(GLFWwindow *window, int width, int height) {
	int fBuffwidth = 0, fBuffheight = 0;
	glfwGetFramebufferSize(window, &fBuffwidth, &fBuffheight);
	EngineSwapchainCreate(engine_instance, fBuffwidth, fBuffheight);
}

int main() {
	#ifndef NDEBUG
		printf("DEBUG IS ON\n");
	#endif
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	GLFWwindow *window = glfwCreateWindow(WIDTH, HEIGHT, DISPLAY_NAME, NULL, NULL);
	EngineCI engineCreateInfo = {
		.appName = "mammamia",
		.displayName = DISPLAY_NAME,
		.appVersion = MAKE_VERSION(0,0,1),
	};	
	engineCreateInfo.extensions = glfwGetRequiredInstanceExtensions(&engineCreateInfo.extensionsCount);
	
	uintptr_t surface = 0, vkInstance = 0;
	res = EngineInit(&engine_instance, engineCreateInfo, &vkInstance);
	if(res.EngineCode != SUCCESS) {
		exit(-1);
	}
	glfwCreateWindowSurface(vkInstance, window, NULL, &surface);
	EngineFinishSetup(engine_instance, surface);
	
	uint32_t frameBufferWidth = 0, frameBufferHeight = 0;
	glfwGetFramebufferSize(window, &frameBufferWidth, &frameBufferHeight);
	glfwSetWindowSizeCallback(window, window_size_callback);
	res = EngineSwapchainCreate(engine_instance, frameBufferWidth, frameBufferHeight);
	if(res.EngineCode != SUCCESS) {
		exit(-1);
	}
	while(!glfwWindowShouldClose(window)) {
		EngineColor Color = {1, 0, 1, 1};
		EngineDrawStart(engine_instance, Color);


		EngineDrawEnd(engine_instance);
		glfwPollEvents();
	}
	EngineSwapchainDestroy(engine_instance);
	EngineDestroy(engine_instance);
	glfwDestroyWindow(window);
	glfwTerminate();
}