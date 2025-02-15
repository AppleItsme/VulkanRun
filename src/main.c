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
	.length = 3,
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

double lockPosition[2] = {0, 0};

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
	lockPosition[0] = bufferSize.width/2;
	lockPosition[1] = bufferSize.height/2;
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
			.roughness = 1,
			.refraction = 0,
		},
		{
			.color = {0,1,0,0},
			.isNormalPresent = false,
			.isTexturePresent = false,
			.metallic = 0,
			.roughness = 0,
			.refraction = 1.57,
		},
		{
			.color = {1,1,1,0},
			.isNormalPresent = false,
			.isTexturePresent = false,
			.metallic = 0,
			.roughness = 0,
			.refraction = 1,
		},
		{
			.color = {0,0,1,0},
			.isNormalPresent = false,
			.isTexturePresent = false,
			.metallic = 1,
			.roughness = 0,
			.refraction = 0,
		}
	};
	EngineLoadMaterials(engine_instance, material, ARR_SIZE(material));
	printf("materials loaded\n");

	EngineSphere sphereData[] = {
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
				.translation = {0,-0.4,2.5},
				.rotation = {0,0,0},
				.scale = {0,0,0}
			},
			.flags = ENGINE_ISACTIVE_FLAG | ENGINE_EXISTS_FLAG
		},
		{
			.materialID = 2,
			.radius = 0.4,
			.transformation = {
				.translation = {0,-0.4,2.5},
				.rotation = {0,0,0},
				.scale = {0,0,0}
			},
			.flags = ENGINE_ISACTIVE_FLAG | ENGINE_EXISTS_FLAG
		},
		{
			.materialID = 1,
			.radius = 0.5,
			.transformation = {
				.translation = {2,-0.4,2.5},
				.rotation = {0,0,0},
				.scale = {0,0,0}
			},
			.flags = ENGINE_ISACTIVE_FLAG | ENGINE_EXISTS_FLAG
		},
		{
			.materialID = 3,
			.radius = 0.75,
			.transformation = {
				.translation = {2,0.25,5},
				.rotation = {0,0,0},
				.scale = {0,0,0}
			},
			.flags = ENGINE_ISACTIVE_FLAG | ENGINE_EXISTS_FLAG
		},
		{
			.materialID = 0,
			.radius = 0.25,
			.transformation = {
				.translation = {2,0.25,5},
				.rotation = {0,0,0},
				.scale = {0,0,0}
			},
			.flags = ENGINE_ISACTIVE_FLAG | ENGINE_EXISTS_FLAG
		},
		{
			.materialID = 0,
			.radius = 20,
			.transformation = {
				.translation = {0,0,-20},
				.rotation = {0,0,0},
				.scale = {0,0,0}
			},
			.flags = ENGINE_ISACTIVE_FLAG | ENGINE_EXISTS_FLAG
		},
	};
	EngineSphere *sphereArr[MAX_SPHERE_COUNT] = {0};
	size_t sphereCount = 0;
	size_t ID = 0;
	for(size_t i = 0; i < ARR_SIZE(sphereData); i++) {
		EngineCreateSphere(engine_instance, sphereArr, &sphereCount, &ID);
		*sphereArr[i] = sphereData[i];
	}
	EngineSunlight sunlight = {
			.color = {1,1,1,1},
			.lightData = {-1,-1,0,0.7},
	};
	EngineLoadSunlight(engine_instance, sunlight);

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
		.elementByteSize = sizeof(uint32_t),
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
	uint32_t maxRays = 6;

	glfwSetTime(0);
	float previousTime = 0, lastResetTime = 0;

	EngineCamera camera = {
		.origin = {0,0,0},
		.direction = {0,0,1},
	};
	EngineCamera *camHandle = NULL;
	EngineCreateCamera(engine_instance, &camHandle);

	const float velocity = 1;
	const float rotational_velocity = 0.01;

	float fpsCounter = 0;
	int frames = 0;
	const size_t maxFrames = 3;
	double angles[2] = {0,0};
	double previousAngles[2] = {0,0};
	lockPosition[0] = bufferSize.width/2;
	lockPosition[1] = bufferSize.height/2;

	glfwSetCursorPos(window, lockPosition[0], lockPosition[1]);

	bool cursorLock = true;
	bool beingLocked = false;

	while(!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		if(isMinimised) {
			continue;
		}
		EngineColor Color = {0.1, 0.5, 0.9, 1};
		res = EngineDrawStart(engine_instance, Color, &drawWaitSemaphore[EngineGetFrame(engine_instance)]);
		float time = glfwGetTime();
		float deltaTime = time - previousTime;
		frames++;
		fpsCounter += deltaTime/3;
		if(frames >= maxFrames) {
			printf("FPS: %.5f\n", 1/fpsCounter);
			frames = 0;
			fpsCounter = 0;
		}
		previousTime = time;
		if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !beingLocked) {
			if(!cursorLock) {
				previousAngles[0] = angles[0];
				previousAngles[1] = angles[1];
			}
			cursorLock = !cursorLock;
			beingLocked = true;
		} else if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_RELEASE) {
			beingLocked = false;
		}
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
		if(glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
			camera.origin[0] = 0;
			camera.origin[1] = 0;
			camera.origin[2] = 0;
			camera.direction[0] = 0;
			camera.direction[1] = 0;
			camera.direction[2] = 1;
			lastResetTime = glfwGetTime();
		}

		vec3 y_axis = {0,1,0};
		vec3 x_axis = {1,0,0};
		vec3 z_axis = {0,0,1};
		double dangles[2] = {0,0};
		glfwGetCursorPos(window, &dangles[0], &dangles[1]);
		dangles[0] -= lockPosition[0];
		dangles[1] -= lockPosition[1];

		dangles[0] *= rotational_velocity;
		dangles[1] *= rotational_velocity;
		if(cursorLock) {
			glfwSetCursorPos(window,lockPosition[0],lockPosition[1]);
			angles[0] += dangles[0];
			angles[1] += dangles[1];
		} else {
			angles[0] = previousAngles[0] + dangles[0];
			angles[1] = previousAngles[1] + dangles[1];
		}

		glm_vec3_rotate(z_axis, angles[1], x_axis);
		glm_vec3_rotate(z_axis, angles[0], y_axis);
		memcpy(camera.direction, z_axis, sizeof(vec3));

		vec3 delta = {0,0,0};
		delta[0] = (glfwGetKey(window, GLFW_KEY_A) - glfwGetKey(window, GLFW_KEY_D)) * velocity * deltaTime;
		delta[2] = (glfwGetKey(window, GLFW_KEY_W) - glfwGetKey(window, GLFW_KEY_S)) * velocity * deltaTime;
		delta[1] = (glfwGetKey(window, GLFW_KEY_SPACE) - glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)) * velocity * deltaTime;

		vec3 local_x = {0,0,0};
		glm_cross(z_axis, y_axis, local_x);
		glm_normalize(local_x);
		glm_vec3_scale(local_x, delta[0], local_x);
		glm_vec3_add(camera.origin, local_x, camera.origin);
		glm_vec3_scale(z_axis, delta[2], z_axis);
		glm_vec3_add(camera.origin, z_axis, camera.origin);
		// camera.origin[0] += dx;
		camera.origin[1] += delta[1];
		// camera.origin[2] += dz;
		
 
		memcpy(camHandle, &camera, sizeof(EngineCamera));
		((float*)randBuffer.data)[0] = time * 1000;
		((uint32_t*)randBuffer.data)[1] = maxRays;

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
	EngineDestroyCamera(engine_instance);

	EngineDestroySphereBuffer(engine_instance);
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