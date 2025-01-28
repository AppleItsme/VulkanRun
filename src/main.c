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
	.elementByteSize = sizeof(EngineTransformation),
	.length = 2,
	.isAccessible = true
};

struct {
	uint32_t width, height;
} bufferSize;

bool isMinimised = false;
bool wasMinimised = false;

void sendValues() {
	float aspectRatio = bufferSize.width/bufferSize.height;
	// mat4 vsMatrix = {
	// 	{(float)bufferSize.height/2, 0, 0, (float)bufferSize.width/2},
	// 	{0,-1 * (float)bufferSize.height/2, 0, (float)bufferSize.height/2},
	// 	{0,0,1,0},
	// 	{0,0,0,1}
	// };
	EngineTransformation matrices[2] = {{
		.translation = {(float)bufferSize.width/2, (float)bufferSize.height/2, 0},
		.scale = {(float)bufferSize.height/2, -(float)bufferSize.height/2, 1},
		.rotation = {0,0,0}
	}, {
		.translation = {-(float)bufferSize.width/(float)bufferSize.height, 1, 0},
		.rotation = {0,0,0},
		.scale = {2/(float)bufferSize.height, -2/(float)bufferSize.height, 1}
	}};
	memcpy(VSMatrices.data, matrices, sizeof(EngineTransformation)*2);
	
	EngineTransformation t = ((EngineTransformation*)VSMatrices.data)[0];
	vec3 s = {sin(t.rotation[0]), sin(t.rotation[1]), sin(t.rotation[2])};
    vec3 c = {cos(t.rotation[0]), cos(t.rotation[1]), cos(t.rotation[2])};

	mat4 e = {
		{t.scale[0]*c[1]*c[2], c[1]*s[2], s[1], 0},
		{-s[0]*s[1]*c[2]-c[0]*s[2], t.scale[1]*c[0]*c[2]-s[0]*s[1]*s[2], s[0]*c[1], 0},
		{s[0]*s[2]-c[0]*s[1]*c[2], -s[0]*c[2]-c[0]*s[1]*s[2], t.scale[2]*c[0]*c[1], 0},
		{t.translation[0], t.translation[1], t.translation[2],1}
	};
	printf("matrix:\n");
	for(size_t i = 0; i < 4; i++) {
		for(size_t j = 0; j < 4; j++) {
			printf("%.10f ", e[j][i]);
		}
		printf("\n");
	}
	vec4 testVec = {1,-1,1,1};
	vec4 resultVec = {0};
	glm_mat4_mulv(e, testVec, resultVec);
	printf("result vector:\n");
	for(size_t i = 0; i < 4; i++) {
		printf("|%.10f|\n", resultVec[i]);
	}
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

	EngineDataTypeInfo infos[ENGINE_DATATYPE_INFO_LENGTH] = {0};
	EngineGenerateDataTypeInfo(infos);
	res = EngineDeclareDataSet(engine_instance, infos, ENGINE_DATATYPE_INFO_LENGTH);
	if(res.EngineCode != ENGINE_SUCCESS) {
		printf("bad descriptor set\n");
		return;
	}

	glfwGetFramebufferSize(window, &bufferSize.width, &bufferSize.height);
	glfwSetWindowSizeCallback(window, window_size_callback);
	EngineSwapchainCreate(engine_instance, bufferSize.width, bufferSize.height);

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

	EngineSemaphore drawWaitSemaphore[2] = {0};
	EngineSemaphore commandDoneSemaphore[2] = {0};
	for(int i = 0; i < 2; i++) {
		EngineCreateSemaphore(engine_instance, &commandDoneSemaphore[i]);
	}

	EngineCreateBuffer(engine_instance, &VSMatrices, ENGINE_BUFFER_STORAGE);
	sendValues();

	EngineAttachDataInfo matricesAttachInfo = {
		.applyCount = ENGINE_ATTACH_DATA_ALL_FRAMES,
		.binding = 2,
		.content = {.buffer = VSMatrices},
		.startingIndex = 0,
		.endIndex = 0,
		.nextFrame = false,
		.type = ENGINE_BUFFER_STORAGE
	};
	EngineAttachData(engine_instance, matricesAttachInfo);

	printf("matrices sent\n");

	glfwSetTime(0);

	EngineMaterial material = {
		.color = {1,0,1,1},
		.isNormalPresent = false,
		.isTexturePresent = false,
		.luminosity = 0,
		.roughness = 0,
		.refraction = 0,
	};
	EngineLoadMaterials(engine_instance, &material, 1);
	printf("materials loaded\n");

	EngineSphere sphere = {
		.isActive = true,
		.materialID = material.ID,
		.radius = 1,
		.transformation = {
			.translation = {0,0,2},
			.rotation = {0,0,0},
			.scale = {2,0,0}
		}
	};
	EngineCreateSphere(engine_instance, &sphere, &sphere);
	printf("sphere created\n");

	// uint32_t i = 100;
	while(!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		if(isMinimised) {
			continue;
		}
		EngineColor Color = {0, 0, 0, 1};
		res = EngineDrawStart(engine_instance, Color, &drawWaitSemaphore[EngineGetFrame(engine_instance)]);
		// printf("drawing start\n");

		float time = glfwGetTime();
		vec4 arr = {0, 0, 3 + sinf(time), 1};
		sphere.transformation.translation[3] = 2 + sinf(time);
		
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
		EngineSubmitCommand(engine_instance, cmd, &drawWaitSemaphore[EngineGetFrame(engine_instance)], &commandDoneSemaphore[EngineGetFrame(engine_instance)]);
		// printf("custom command submitted\n");
		EngineDrawEnd(engine_instance, &commandDoneSemaphore[EngineGetFrame(engine_instance)]);
		// i--;
		// if(i == 0) {
		// 	break;
		// }
	}
	EngineDestroySphere(engine_instance, &sphere);
	// EngineDestroyCamera(engine_instance);
	printf("destroyed sphere\n");
	EngineUnloadMaterials(engine_instance);
	printf("destroyed materials\n");
	EngineDestroyBuffer(engine_instance, VSMatrices);
	printf("destroyed matrices\n");
	EngineSwapchainDestroy(engine_instance);
	printf("destroyed swapchain\n");
	for(size_t i = 0; i < 2; i++)
		EngineDestroySemaphore(engine_instance, commandDoneSemaphore[i]);
	printf("destroyed semaphore\n");
	EngineDestroy(engine_instance);
	glfwDestroyWindow(window);
	glfwTerminate();
}