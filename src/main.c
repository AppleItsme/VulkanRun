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
		.scale = {2/(float)bufferSize.height, -2/(float)bufferSize.height, 1},
		.rotation = {0,0,0},
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

#define MAX_SPHERE_COUNT 10
#define MAX_LIGHT_SOURCE 1

#define ARR_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))


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

	const EngineObjectLimits limits = {
		.maxSphereCount = MAX_SPHERE_COUNT,
		.maxLightSourceCount = MAX_LIGHT_SOURCE
	};

	EngineFinishSetup(engine_instance, surface, limits);

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
	EngineMaterial material[] = {
		{
			.color = {1,0,0,0},
			.isNormalPresent = false,
			.isTexturePresent = false,
			.metallic = 0,
			.roughness = 0.9,
			.refraction = 0,
		},
		{
			.color = {0,1,0,0},
			.isNormalPresent = false,
			.isTexturePresent = false,
			.metallic = 0,
			.roughness = 0.5,
			.refraction = 0,
		},
		{
			.color = {0,0,1,0},
			.isNormalPresent = false,
			.isTexturePresent = false,
			.metallic = 0,
			.roughness = 0,
			.refraction = 0,
		}
	};
	EngineLoadMaterials(engine_instance, material, ARR_SIZE(material));
	printf("materials loaded\n");

	EngineSphere sphereData[4] = {
		{
			.materialID = 0,
			.radius = 0.5,
			.transformation = {
				.translation = {-1,-0.5,3},
				.rotation = {0,0,0},
				.scale = {0,0,0}
			},
			.flags = ENGINE_ISACTIVE_FLAG | ENGINE_EXISTS_FLAG
		},
		{
			.materialID = 1,
			.radius = 0.5,
			.transformation = {
				.translation = {0,-0.5,5},
				.rotation = {0,0,0},
				.scale = {0,0,0}
			},
			.flags = ENGINE_ISACTIVE_FLAG | ENGINE_EXISTS_FLAG
		},
		{
			.materialID = 2,
			.radius = 0.75,
			.transformation = {
				.translation = {2,0.25,5},
				.rotation = {0,0,0},
				.scale = {0,0,0}
			},
			.flags = ENGINE_ISACTIVE_FLAG | ENGINE_EXISTS_FLAG
		},
		{
			.materialID = 2,
			.radius = 0.75,
			.transformation = {
				.translation = {0,2,5},
				.rotation = {0,0,0},
				.scale = {0,0,0}
			},
			.flags = ENGINE_ISACTIVE_FLAG | ENGINE_EXISTS_FLAG
		}
	};

	EngineSphere *sphereArr[MAX_SPHERE_COUNT] = {0};
	size_t sphereCount = 0;
	size_t ID = 0;
	EngineCreateSphere(engine_instance, sphereArr, &sphereCount, &ID);
	*sphereArr[0] = sphereData[0];
	EngineCreateSphere(engine_instance, sphereArr, &sphereCount, &ID);
	*sphereArr[1] = sphereData[1];
	EngineCreateSphere(engine_instance, sphereArr, &sphereCount, &ID);
	*sphereArr[2] = sphereData[2];
	EngineCreateSphere(engine_instance, sphereArr, &sphereCount, &ID);
	*sphereArr[3] = sphereData[3];
	EngineSunlight sunlight = {
			.color = {1,1,1,1},
			.lightData = {-1,-1,0,0.7},
	};
	EngineLoadLightSources(engine_instance, sunlight);

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

	EngineBuffer randBuffer = {
		.elementByteSize = sizeof(float),
		.length = 2,
		.isAccessible = true
	};
	EngineCreateBuffer(engine_instance, &randBuffer, ENGINE_BUFFER_UNIFORM);
	EngineAttachDataInfo attachInfo = {
		.applyCount = ENGINE_ATTACH_DATA_ALL_FRAMES,
		.binding = 5,
		.content = {.buffer = randBuffer},
		.startingIndex = 0,
		.endIndex = 0,
		.nextFrame = false,
		.type = ENGINE_BUFFER_UNIFORM
	};
	EngineAttachData(engine_instance, attachInfo);

	res = EngineLoadShaders(engine_instance, &shaderInfo, 1);
	free(shaderCode);
	bool beingPressed[2] = {0,0};
	uint32_t maxRays = 20;

	glfwSetTime(0);
	float previousTime = 0;

	// glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

	const float velocity = 1;
	const float rotational_velocity = 0.1;
	while(!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		if(isMinimised) {
			continue;
		}
		EngineColor Color = {0.5, 0.5, 0.5, 1};
		res = EngineDrawStart(engine_instance, Color, &drawWaitSemaphore[EngineGetFrame(engine_instance)]);
		float time = glfwGetTime();
		float deltaTime = time - previousTime;
		previousTime = time;
		if(glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !beingPressed[0]) {
			beingPressed[0] = true;
			if(maxRays < 20) {
				maxRays++;
				printf("maxRays: %zu\n", maxRays);
			}
		} else if(glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
			beingPressed[0] = false;
		}
		if(glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !beingPressed[1]) {
			beingPressed[1] = true;
			if(maxRays > 1) {
				maxRays--;
				printf("maxRays: %zu\n", maxRays);
			}
		} else if(glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
			beingPressed[1] = false;
		}

		float dx = (glfwGetKey(window, GLFW_KEY_D) - glfwGetKey(window, GLFW_KEY_A)) * velocity * deltaTime;
		float dz = (glfwGetKey(window, GLFW_KEY_W) - glfwGetKey(window, GLFW_KEY_S)) * velocity * deltaTime;
		float dy = (glfwGetKey(window, GLFW_KEY_SPACE) - glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)) * velocity * deltaTime;


		for(size_t i = 0; i < sphereCount; i++) {
			sphereData[i].transformation.translation[0] += -dx;
			sphereData[i].transformation.translation[2] += -dz;
			sphereData[i].transformation.translation[1] += -dy;
			
			sphereArr[i]->transformation.translation[0] = sphereData[i].transformation.translation[0];
			sphereArr[i]->transformation.translation[2] = sphereData[i].transformation.translation[2];
			sphereArr[i]->transformation.translation[1] = sphereData[i].transformation.translation[1];
		}

		((float*)randBuffer.data)[0] = time;
		((uint32_t*)randBuffer.data)[1] = maxRays;
		// EngineSunlight curSunlight = sunlight;
		// curSunlight.lightData[0] = sunlight.lightData[0] * cosf(time);
		// curSunlight.lightData[1] = sunlight.lightData[1] * sinf(time);
		// EngineWriteSunlight(engine_instance, curSunlight);

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
		EngineDrawEnd(engine_instance, &commandDoneSemaphore[EngineGetFrame(engine_instance)]);
	}
	EngineDestroyBuffer(engine_instance, randBuffer);
	EngineDestroySphereBuffer(engine_instance);
	// EngineDestroyCamera(engine_instance);
	printf("destroyed sphere\n");
	EngineUnloadMaterials(engine_instance);
	printf("destroyed materials\n");
	EngineUnloadSunlight(engine_instance);
	printf("destroyed light sources\n");
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