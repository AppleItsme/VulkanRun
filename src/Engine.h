#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t appVersion;
    char *appName, *displayName;
    uint32_t extensionsCount;
    char **extensions;
} EngineCI;


typedef struct Engine Engine;
typedef enum {
    ENGINE_BUFFER,
    ENGINE_IMAGE,
} EngineDataType;


typedef struct {
    EngineDataType type;
    uint32_t bindingIndex, length;
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
    float r,g,b,a;
} EngineColor;

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
    size_t length;
    uintptr_t _buffer, _allocation;
    size_t elementByteSize;
    void *data;
} EngineBuffer;

typedef struct {
    uint32_t startingIndex, 
            endIndex,
            binding;
    EngineDataType type;
    union {
        EngineImage image;
        EngineBuffer buffer;
    } content;
} EngineWriteDataInfo;

typedef uintptr_t EngineSemaphore;
typedef uintptr_t EngineCommand;

#define MAKE_VERSION(major, minor, patch) ((((uint32_t)(major)) << 22U) | (((uint32_t)(minor)) << 12U) | ((uint32_t)(patch)))
EngineResult EngineInit(Engine **engine, EngineCI engineCI, uintptr_t *vkInstance);
EngineResult EngineFinishSetup(Engine *engine, uintptr_t surface);
void EngineDestroy(Engine *engine);

EngineResult EngineSwapchainCreate(Engine *engine, uint32_t frameBufferWidth, uint32_t frameBufferHeight, EngineImage *renderImages);
void EngineSwapchainDestroy(Engine *engine);

uint32_t EngineGetFrame(Engine *engine);

EngineResult EngineDrawStart(Engine *engine, EngineColor background, EngineSemaphore *signalSemaphore);
EngineResult EngineDrawEnd(Engine *engine, EngineSemaphore *waitSemaphore);

EngineResult EngineLoadShaders(Engine *engine, EngineShaderInfo *shaders, size_t shaderCount);
EngineResult EngineStartCommand(Engine *engine, EngineCommand *cmd);
void EngineRunShader(Engine *engine, EngineCommand cmd, size_t index, EngineShaderRunInfo runInfo);
EngineResult EngineEndCommand(Engine *engine, EngineCommand cmd, EngineSemaphore *waitSemaphore, EngineSemaphore *signalSemaphore);

EngineResult EngineDeclareDataSet(Engine *engine, EngineDataTypeInfo *datatypes, size_t datatypeCount);
void EngineWriteData(Engine *engine, EngineWriteDataInfo *info);

EngineResult EngineCreateSemaphore(Engine *engine, EngineSemaphore *semaphore);
void EngineDestroySemaphore(Engine *engine, EngineSemaphore semaphore);

EngineResult EngineCreateBuffer(Engine *engine, EngineBuffer *engineBuffer);
void EngineDestroyBuffer(Engine *engine, EngineBuffer buffer);

/*
    1. Create VkDescriptorSetLayoutBindings for every type of object you want in descriptor set
    2. Create VkDescriptorSetLayout
    3. Create command pool
    4. create descriptor set
    5. Create shader module(s)
    6. Create pipeline layout
    7. create pipeline
*/
