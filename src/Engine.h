#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <cglm/cglm.h>

typedef struct {
    uint32_t appVersion;
    char *appName, *displayName;
    uint32_t extensionsCount;
    char **extensions;
} EngineCI;


typedef struct Engine Engine;
typedef enum {
    ENGINE_BUFFER_STORAGE,
    ENGINE_BUFFER_UNIFORM,
    ENGINE_IMAGE,
    ENGINE_SAMPLED_IMAGE_ARRAY,
} EngineDataType;


#define ENGINE_DATATYPE_INFO_LENGTH 5
typedef struct {
    EngineDataType type;
    uint32_t bindingIndex, count;
} EngineDataTypeInfo;

typedef struct {
    enum {
        ENGINE_SUCCESS,
        ENGINE_INSTANCE_CREATION_FAILED,
        ENGINE_GPU_NOT_FOUND,
        ENGINE_INAPPROPRIATE_GPU,
        ENGINE_DEVICE_CREATION_FAILED,
        ENGINE_INSUFFICIENT_VULKAN,
        ENGINE_SWAPCHAIN_FAILED,
        ENGINE_IMAGE_VIEW_FAILED,

        ENGINE_CANNOT_CREATE_SYNCHRONISING_VARIABLES,
        ENGINE_BUFFER_CREATION_FAILED,

        ENGINE_DEBUG_INFO_NOT_FOUND,
        ENGINE_DEBUG_CREATION_FAILED,
        ENGINE_OUT_OF_MEMORY,

        ENGINE_QUEUECOMMAND_CREATION_FAILED,
        ENGINE_QUEUECOMMAND_ALLOCATION_FAILED,

        ENGINE_FENCE_CREATION_FAILED,
        ENGINE_SEMAPHORE_CREATION_FAILED,

        ENGINE_FENCE_NOT_WORKING,
        ENGINE_CANNOT_START_COMMAND,
        ENGINE_CANNOT_PREPARE_FOR_SUBMISSION,
        ENGINE_CANNOT_SUBMIT_TO_GPU,
        ENGINE_CANNOT_DISPLAY,
        
        ENGINE_DATASET_DECLARATION_FAILED,
        ENGINE_SHADER_CREATION_FAILED,
    } EngineCode;
    size_t VulkanCode;
} EngineResult;

typedef struct {
    char *code;
    size_t byteSize;
} EngineShaderInfo;

typedef struct {
    uint32_t groupSizeX, groupSizeY, groupSizeZ;
} EngineShaderRunInfo;

typedef struct {
    uintptr_t image, view, layout;
} EngineImage;

typedef struct {
    size_t length, count;
    uintptr_t _buffer, _allocation;
    size_t elementByteSize;
    bool isAccessible;
    void *data;
} EngineBuffer;

typedef union {
    EngineImage image;
    EngineBuffer buffer;
} EngineDataContent;


typedef uintptr_t EngineSemaphore;
typedef uintptr_t EngineCommand;
typedef enum {
    ENGINE_COMMAND_REUSABLE,
    ENGINE_COMMAND_ONE_TIME
} EngineCommandRecordingType;

typedef vec4 EngineColor;

#define MAKE_VERSION(major, minor, patch) ((((uint32_t)(major)) << 22U) | (((uint32_t)(minor)) << 12U) | ((uint32_t)(patch)))

typedef struct {
    size_t maxSphereCount;
    size_t maxLightSourceCount;
    size_t maxTriangleCount;
} EngineObjectLimits;


EngineResult EngineInit(Engine **engine, EngineCI engineCI, uintptr_t *vkInstance);
EngineResult EngineFinishSetup(Engine *engine, uintptr_t surface, EngineObjectLimits limits);
void EngineDestroy(Engine *engine);

EngineResult EngineSwapchainCreate(Engine *engine, uint32_t frameBufferWidth, uint32_t frameBufferHeight);
EngineResult EngineSwapchainCreate(Engine *engine, uint32_t frameBufferWidth, uint32_t frameBufferHeight);
void EngineSwapchainDestroy(Engine *engine);

EngineResult EngineDrawStart(Engine *engine, EngineColor background, EngineSemaphore *signalSemaphore);
EngineResult EngineDrawEnd(Engine *engine, EngineSemaphore *waitSemaphore);

EngineResult EngineLoadShaders(Engine *engine, EngineShaderInfo *shaders, size_t shaderCount);
EngineResult EngineCreateCommand(Engine *engine, EngineCommand *cmd);
EngineResult EngineCommandRecordingStart(Engine *engine, EngineCommand cmd, EngineCommandRecordingType type);
EngineResult EngineCommandRecordingEnd(Engine *engine, EngineCommand cmd);
EngineResult EngineSubmitCommand(Engine *engine, EngineCommand cmd, EngineSemaphore *waitSemaphore, EngineSemaphore *signalSemaphore);
void EngineDestroyCommand(Engine *engine, EngineCommand cmd);

void EngineRunShader(Engine *engine, EngineCommand cmd, size_t index, EngineShaderRunInfo runInfo);

extern inline void EngineGenerateDataTypeInfo(EngineDataTypeInfo *dataTypeInfo);
EngineResult EngineDeclareDataSet(Engine *engine, EngineDataTypeInfo *datatypes, size_t datatypeCount);

typedef struct {
    uint32_t startingIndex, 
            endIndex,
            binding;
    EngineDataType type;
    union {
        EngineImage image;
        EngineBuffer buffer;
    } content;
    bool nextFrame;
    size_t applyCount;
} EngineAttachDataInfo;
#define ENGINE_ATTACH_DATA_ALL_FRAMES 0
void EngineAttachData(Engine *engine, EngineAttachDataInfo info);

EngineResult EngineCreateSemaphore(Engine *engine, EngineSemaphore *semaphore);
void EngineDestroySemaphore(Engine *engine, EngineSemaphore semaphore);
uint32_t EngineGetFrame(Engine *engine);

EngineResult EngineCreateBuffer(Engine *engine, EngineBuffer *engineBuffer, EngineDataType type);
void EngineBufferAccessUpdate(Engine *engine, EngineBuffer *buffer, bool setAccessVal);
void EngineDestroyBuffer(Engine *engine, EngineBuffer buffer);

EngineResult EngineCreateImage(Engine *engine, EngineImage *engineImage);
void EngineDestroyImage(Engine *engine, EngineImage engineImage);

typedef struct {
	float roughness;
    float refraction;
    float luminosity;
    EngineColor color;
	bool isTexturePresent;
    uint32_t textureIndex;
    bool isNormalPresent;
    uint32_t normalIndex;
} EngineMaterial;

typedef struct {
    vec3 translation;
    vec3 scale;
    vec3 rotation;
} EngineTransformation;

typedef enum {
    ENGINE_EXISTS_FLAG = 1,
    ENGINE_ISACTIVE_FLAG = 2,
} EngineSphereDataFlags;

typedef struct {
	EngineTransformation transformation;
    float radius;
    uint32_t materialID;
	uint32_t flags;
} EngineSphere;

EngineResult EngineCreateSphere(Engine *engine, EngineSphere **sphereArr, size_t *count, size_t *ID);
void EngineDestroySphere(Engine *engine, EngineSphere *sphereInstance);
void EngineDestroySphereBuffer(Engine *engine);

void EngineLoadMaterials(Engine *engine, EngineMaterial *material, size_t materialCount);
//if indices == NULL, then it starts from 0 and goes to count-1
void EngineWriteMaterials(Engine *engine, EngineMaterial *material, size_t *indices, size_t count);
void EngineUnloadMaterials(Engine *engine);

typedef struct {
    uint32_t type;
    vec4 lightData;
    vec4 color;
} EngineLightSource;

void EngineLoadLightSources(Engine *engine, EngineLightSource *lightSources, size_t lightSourceCount);
//if indices == NULL, then it starts from 0 and goes to count-1
void EngineWriteLightSources(Engine *engine, EngineLightSource *lightSources, size_t *indices, size_t count);
void EngineUnloadLightSources(Engine *engine);