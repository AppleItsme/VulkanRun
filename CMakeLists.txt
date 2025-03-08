cmake_minimum_required(VERSION 3.25)

Project (
	vulkanTest
	LANGUAGES C CXX
	VERSION 0.1
	)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED TRUE)
set(BUILD_SHARED_LIBS OFF)



if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(Vulkan REQUIRED)
include_directories(${Vulkan_INCLUDE_DIRS})

add_executable(${PROJECT_NAME} src/main.c)
add_library(engine src/Engine.c)
add_library(vma_usage src/vma.cpp)
add_library(stb_usage src/stb.c)
add_library(utilities src/utils.c)

target_include_directories(vma_usage PRIVATE ThirdParty/VulkanMemoryAllocator/include)

add_subdirectory(ThirdParty/cglm)
add_subdirectory(ThirdParty/glfw)

include_directories(ThirdParty/glfw/include)
include_directories(ThirdParty/cglm/include)
include_directories(ThirdParty/stb)
target_include_directories(engine PRIVATE ThirdParty/VulkanMemoryAllocator/include)
target_include_directories(stb_usage PRIVATE ThirdPrty/stb)
target_compile_definitions(${PROJECT_NAME} PUBLIC PROJECT_PATH="${CMAKE_SOURCE_DIR}")

include_directories(src/)

target_link_libraries(engine PRIVATE
	utilities
	vma_usage
	stb_usage
	cglm
	${Vulkan_LIBRARIES}
)
target_link_libraries(${PROJECT_NAME}
	glfw
	engine
	cglm
)
add_compile_options(-Wall -Wextra -Wpedantic -Werror)