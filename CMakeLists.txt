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
add_library(utilities src/utils.c)

target_include_directories(vma_usage PRIVATE ThirdParty/VulkanMemoryAllocator/include)

add_subdirectory(ThirdParty/cglm)
add_subdirectory(ThirdParty/glfw)

include_directories(ThirdParty/glfw/include)
include_directories(ThirdParty/cglm/include)
target_include_directories(engine PRIVATE ThirdParty/VulkanMemoryAllocator/include)
include_directories(src/)

target_link_libraries(engine PRIVATE
	vma_usage
	${Vulkan_LIBRARIES} 
	cglm
	utilities
	)
target_link_libraries(${PROJECT_NAME}
	glfw
	engine
)
add_compile_options(-Wall -Wextra -Wpedantic -Werror)