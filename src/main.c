#include <vk.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>


#define WIDTH 800
#define HEIGHT 600
#define DISPLAY_NAME "mijn vulkan app"


int main() {
	EngineCI engineCreateInfo = {
		.appName = "mammamia",
		.displayName = DISPLAY_NAME,
		.appVersion = MAKE_VERSION(0,0,1),
		.width = WIDTH,
		.height = HEIGHT
	};
	Engine *engine_instance = NULL;
	EngineResult res = engine_init(engine_instance, engineCreateInfo);
	if(res.EngineCode != SUCCESS) {
		printf("whee whee\n");
	}
	while(!EngineWindowShouldClose(engine_instance)) {
		EngineRenderingDone(engine_instance);
	}
	printf("all's well that ends well\n");
	engine_destroy(engine_instance);
}