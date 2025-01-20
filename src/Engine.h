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
    ENGINE_BUFFER_STORAGE,
    ENGINE_BUFFER_UNIFORM,
    ENGINE_IMAGE,
} EngineDataType;


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
typedef enum {
    ENGINE_COMMAND_REUSABLE,
    ENGINE_COMMAND_ONE_TIME
} EngineCommandRecordingType;

#define MAKE_VERSION(major, minor, patch) ((((uint32_t)(major)) << 22U) | (((uint32_t)(minor)) << 12U) | ((uint32_t)(patch)))
EngineResult EngineInit(Engine **engine, EngineCI engineCI, uintptr_t *vkInstance);
EngineResult EngineFinishSetup(Engine *engine, uintptr_t surface);
void EngineDestroy(Engine *engine);

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

EngineResult EngineDeclareDataSet(Engine *engine, EngineDataTypeInfo *datatypes, size_t datatypeCount);
void EngineWriteData(Engine *engine, EngineWriteDataInfo *info);

EngineResult EngineCreateSemaphore(Engine *engine, EngineSemaphore *semaphore);
void EngineDestroySemaphore(Engine *engine, EngineSemaphore semaphore);

EngineResult EngineCreateBuffer(Engine *engine, EngineBuffer *engineBuffer, EngineDataType type);
void EngineDestroyBuffer(Engine *engine, EngineBuffer buffer);

EngineResult EngineCreateImage(Engine *engine, EngineImage *engineImage);
void EngineDestroyImage(Engine *engine, EngineImage engineImage);