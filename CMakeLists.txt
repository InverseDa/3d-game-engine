cmake_minimum_required(VERSION 3.15 FATAL_ERROR)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ENGINE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/engine)
set(GLOBAL_THIRD_PARTY_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/include)
set(GLOBAL_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(GLOBAL_THIRD_PARTY_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/library)
set(GLFW_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/src/glfw-3.4/include)
set(SPDLOG_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/src/spdlog-1.13.0/include)

if(APPLE)
    add_compile_definitions(IDA_OS_MACOS)
elseif(WIN32)
    add_compile_definitions(IDA_OS_WINDOWS)
elseif(UNIX AND NOT APPLE)
    add_compile_definitions(IDA_OS_LINUX)
endif()

project(Engine
        VERSION 0.1.0
        LANGUAGES CXX
        DESCRIPTION "A simple game engine")

include(cmake/FindVulkan.cmake)
include(cmake/Copydll.cmake)
include(cmake/CompileShaders.cmake)
include(cmake/CompileThirdPartyLibrary.cmake)

#add_subdirectory(engine)
#add_subdirectory(example)