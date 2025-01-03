cmake_minimum_required(VERSION 3.25)

Project (
	vulkanTest
	LANGUAGES C
	VERSION 0.1
	)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED TRUE)
set(BUILD_SHARED_LIBS OFF)


#set to YOUR vulkansdk path
set(ENV{VULKAN_SDK} "C:/VulkanSDK/1.3.296.0")
if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(Vulkan REQUIRED)

add_executable(${PROJECT_NAME} src/main.c)
add_library(engine src/vk.c)

#same here
include_directories($ENV{VULKAN_SDK}/Include)
include_directories(ThirdParty/glfw/include)
include_directories(ThirdParty/cglm/include)
include_directories(src/)

add_subdirectory(ThirdParty/cglm)
add_subdirectory(ThirdParty/glfw)

target_link_libraries(engine PRIVATE
	${Vulkan_LIBRARIES} 
	glfw
	cglm
	)
target_link_libraries(${PROJECT_NAME}
	engine
)
add_compile_options(-Wall -Wextra -Wpedantic -Werror)