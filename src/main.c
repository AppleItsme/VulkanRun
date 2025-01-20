#include <Engine.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <cglm/cglm.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define WIDTH 800
#define HEIGHT 600
#define DISPLAY_NAME "mijn vulkan app"

EngineResult res;
Engine *engine_instance = NULL;
GLFWwindow *window = NULL;

EngineBuffer VSMatrices = {
	.elementByteSize = sizeof(float),
	.length = 32,
};

struct {
	uint32_t width, height;
} bufferSize;

bool isMinimised = false;
bool wasMinimised = false;

void sendValues() {
	float aspectRatio = bufferSize.width/bufferSize.height;
	mat4 vsMatrix = {
		{(float)bufferSize.height/2, 0, 0, (float)bufferSize.width/2},
		{0,-1 * (float)bufferSize.height/2, 0, (float)bufferSize.height/2},
		{0,0,1,0},
		{0,0,0,1}
	};
	glm_mat4_transpose(vsMatrix);
	mat4 svMatrix = {0};
	glm_mat4_inv(vsMatrix, svMatrix);
	memcpy(VSMatrices.data, vsMatrix, sizeof(float) * 16);
	memcpy((float*)VSMatrices.data + 16, svMatrix, sizeof(float) * 16);
}


void window_size_callback(GLFWwindow *window, int width, int height) {
	glfwGetFramebufferSize(window, &bufferSize.width, &bufferSize.height);
	if(bufferSize.width == 0 || bufferSize.height == 0) {
		isMinimised = true;
		if(!wasMinimised) {
			EngineSwapchainDestroy(engine_instance);
			wasMinimised = true;
		}
		return;
	}
	isMinimised = false;
	if(!isMinimised) {
		if(!wasMinimised) {
			EngineSwapchainDestroy(engine_instance);
		}
		res = EngineSwapchainCreate(engine_instance, bufferSize.width, bufferSize.height);
		wasMinimised = false;
	}
	sendValues();
}

int main() {
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
	EngineInit(&engine_instance, engineCreateInfo, &vkInstance);
	glfwCreateWindowSurface(vkInstance, window, NULL, &surface);
	EngineFinishSetup(engine_instance, surface);
	glfwGetFramebufferSize(window, &bufferSize.width, &bufferSize.height);
	glfwSetWindowSizeCallback(window, window_size_callback);
	EngineSwapchainCreate(engine_instance, bufferSize.width, bufferSize.height);
	EngineDataTypeInfo dTypes[] = {
		{
			.bindingIndex = 0,
			.count = 1,
			.type = ENGINE_IMAGE
		},
		{
			.bindingIndex = 1,
			.count = 1,
			.type = ENGINE_BUFFER_STORAGE
		}, 
		{
			.bindingIndex = 2,
			.count = 1,
			.type = ENGINE_BUFFER_UNIFORM
		}
	};	
	
	res = EngineDeclareDataSet(engine_instance, dTypes, sizeof(dTypes)/sizeof(dTypes[0]));
	FILE *shader = fopen("C:/Users/akseg/Documents/Vulkan/src/shaders/raytrace.spv", "rb");
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
	free(shaderCode);

	EngineSemaphore drawWaitSemaphore = {0};
	EngineSemaphore commandDoneSemaphore = {0};
	EngineCreateSemaphore(engine_instance, &commandDoneSemaphore);

	EngineBuffer buffer = {
		.elementByteSize = sizeof(float),
		.length = 4+4,
	};
	EngineCreateBuffer(engine_instance, &buffer, ENGINE_BUFFER_STORAGE);
	EngineCreateBuffer(engine_instance, &VSMatrices, ENGINE_BUFFER_UNIFORM);
	EngineWriteDataInfo dataInfo = {
		.binding = 2,
		.startingIndex = 0,
		.endIndex = 0,
		.type = ENGINE_BUFFER_UNIFORM,
		.content.buffer = VSMatrices
	};
	EngineWriteData(engine_instance, &dataInfo);
	dataInfo = (EngineWriteDataInfo) {
		.binding = 1,
		.startingIndex = 0,
		.endIndex = 0,
		.type = ENGINE_BUFFER_STORAGE,
		.content.buffer = buffer
	};
	EngineWriteData(engine_instance, &dataInfo);
	sendValues();
	vec4 arr = {0, 0, 3, 1};
	memcpy(buffer.data, arr, sizeof(float) * 4);

	vec4 rotatedSunlight = {0, 1, 0, 2};
	memcpy((float*)buffer.data+4, &rotatedSunlight, sizeof(float) * 4);

	glfwSetTime(0);

	uint32_t i = 100;

	while(!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		if(isMinimised) {
			continue;
		}
		float time = glfwGetTime();
		vec4 rotatedSunlight = {-sinf(time), cosf(time), sinf(time), 2};
		memcpy((float*)buffer.data+4, &rotatedSunlight, sizeof(float) * 4);
		
		EngineColor Color = {0, 0, 1, 1};
		res = EngineDrawStart(engine_instance, Color, &drawWaitSemaphore);
		EngineCommand cmd = 0;
		EngineCreateCommand(engine_instance, &cmd);
		EngineCommandRecordingStart(engine_instance, cmd, ENGINE_COMMAND_ONE_TIME);
		EngineShaderRunInfo runInfo = {
			.groupSizeX = ceilf((float)bufferSize.width/32.0f),
			.groupSizeY = ceilf((float)bufferSize.height/32.0f),
			.groupSizeZ = 1
		};
		EngineRunShader(engine_instance, cmd, 0, runInfo);
		EngineCommandRecordingEnd(engine_instance, cmd);
		EngineSubmitCommand(engine_instance, cmd, &drawWaitSemaphore, &commandDoneSemaphore);
		EngineDrawEnd(engine_instance, &commandDoneSemaphore);
	}
	EngineDestroyBuffer(engine_instance, VSMatrices);
	EngineDestroyBuffer(engine_instance, buffer);
	EngineSwapchainDestroy(engine_instance);
	EngineDestroySemaphore(engine_instance, commandDoneSemaphore);
	EngineDestroy(engine_instance);
	glfwDestroyWindow(window);
	glfwTerminate();
}