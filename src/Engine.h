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

typedef struct {
    enum {
        SUCCESS,
        GLFW_CANNOT_INIT,
        INSTANCE_CREATION_FAILED,
        WINDOW_CREATION_FAILED,
        GPU_NOT_FOUND,
        INAPPROPRIATE_GPU,
        DEVICE_CREATION_FAILED,
        INSUFFICIENT_VULKAN,
        SWAPCHAIN_FAILED,
        IMAGE_VIEW_FAILED,

        CANNOT_CREATE_SYNCHRONISING_VARIABLES,

        DEBUG_INFO_NOT_FOUND,
        DEBUG_CREATION_FAILED,
        OUT_OF_MEMORY,

        QUEUECOMMAND_CREATION_FAILED,
        QUEUECOMMAND_ALLOCATION_FAILED,

        FENCE_CREATION_FAILED,
        SEMAPHORE_CREATION_FAILED,

        FENCE_NOT_WORKING,
        CANNOT_PREPARE_FOR_SUBMISSION,
        CANNOT_SUBMIT_TO_GPU,
        CANNOT_DISPLAY,
        
        BAD_PATH,
        DESCRIPTOR_SET_CREATION_FAILED,
    } EngineCode;
    size_t VulkanCode;
} EngineResult;

typedef struct {
    enum {
        RENDERING,
    } purpose;
    uint32_t size;
} EngineDescriptorCreateInfo;

typedef struct {
    uint8_t r,g,b,a;
} EngineColor;

#define MAKE_VERSION(major, minor, patch) ((((uint32_t)(major)) << 22U) | (((uint32_t)(minor)) << 12U) | ((uint32_t)(patch)))

EngineResult EngineInit(Engine **engine, EngineCI engineCI, uintptr_t *vkInstance);
EngineResult EngineFinishSetup(Engine *engine, uintptr_t surface);
void EngineDestroy(Engine *engine);
EngineResult EngineSwapchainCreate(Engine *engine, uint32_t frameBufferWidth, uint32_t frameBufferHeight);
void EngineSwapchainDestroy(Engine *engine);

EngineResult EngineDrawStart(Engine *engine, EngineColor background);
EngineResult EngineDrawEnd(Engine *engine);