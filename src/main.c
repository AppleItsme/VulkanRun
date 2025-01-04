#include <vk.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>


#define WIDTH 800
#define HEIGHT 600
#define DISPLAY_NAME "mijn vulkan app"

EngineResult res;

int main() {
	#ifndef NDEBUG
		printf("DEBUG IS ON\n");
	#endif

	EngineCI engineCreateInfo = {
		.appName = "mammamia",
		.displayName = DISPLAY_NAME,
		.appVersion = MAKE_VERSION(0,0,1),
		.width = WIDTH,
		.height = HEIGHT
	};
	Engine *engine_instance = NULL;
	res = EngineInit(&engine_instance, engineCreateInfo);
	printf("(%d; %d)\n", res.EngineCode, res.VulkanCode);
	if(res.EngineCode != SUCCESS) {
		exit(-1);
	}
	res = EngineSwapchainCreate(engine_instance);
	printf("(%d; %d)\n", res.EngineCode, res.VulkanCode);
	if(res.EngineCode != SUCCESS) {
		exit(-1);
	}
	printf("loop begins!!\n");
	while(!EngineWindowShouldClose(engine_instance)) {
		res = EngineDraw(engine_instance, (EngineColor){0, 255, 0, 255});
		printf("(%d; %d)\n", res.EngineCode, res.VulkanCode);
	}
	EngineSwapchainDestroy(engine_instance);
	EngineDestroy(engine_instance);
}