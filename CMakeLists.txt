cmake_minimum_required(VERSION 3.24)

Project (
	vulkanTest
	LANGUAGES C
	VERSION 0.1
	)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED TRUE)
set(CMAKE_BUILD_TYPE Debug)


set(ENV{VULKAN_SDK} "C:/VulkanSDK/1.3.296.0")
set(glfw3_PATH "C:/GLFW")

find_package(Vulkan REQUIRED)
add_executable(${PROJECT_NAME} main.c)

include_directories("C:/VulkanSDK/1.3.296.0/Include")
include_directories("${glfw3_PATH}/include")

add_subdirectory(cglm/cglm-master/ EXCLUDE_FROM_ALL)

target_link_libraries(${PROJECT_NAME} 
	${Vulkan_LIBRARIES} 
	${glfw3_PATH}/lib-vc2022/glfw3.lib
	cglm
	)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)
if(CMAKE_BUILD_TYPE EQUAL Debug)
	add_compile_options(-g -ggdb -DDebug)
endif()