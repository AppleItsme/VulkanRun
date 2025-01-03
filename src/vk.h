#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t appVersion;
    char *appName, *displayName;
    uint32_t width, height;
} EngineCI;


typedef struct engine_h Engine;

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

        DEBUG_INFO_NOT_FOUND,
        DEBUG_CREATION_FAILED,
        OUT_OF_MEMORY,

        SHADER_CREATION_FAILED
    } EngineCode;
    size_t VulkanCode;
} EngineResult;



#define MAKE_VERSION(major, minor, patch) ((((uint32_t)(major)) << 22U) | (((uint32_t)(minor)) << 12U) | ((uint32_t)(patch)))

EngineResult EngineInit(Engine **engine, EngineCI engineCI);
void EngineDestroy(Engine *engine);

EngineResult EngineSwapchainCreate(Engine *engine);
void EngineSwapchainDestroy(Engine *engine);

bool EngineWindowShouldClose(Engine *engine);
void EngineRenderingDone(Engine *engine);