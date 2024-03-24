cmake_minimum_required(VERSION 3.15 FATAL_ERROR)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(GLOBAL_THIRD_PARTY_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/include)
set(GLOBAL_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(GLOBAL_THIRD_PARTY_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/library)

project(Engine
        VERSION 0.1.0
        LANGUAGES CXX
        DESCRIPTION "A simple game engine")

include(cmake/FindVulkan.cmake)
include(cmake/FindGLFW.cmake)
include(cmake/Copydll.cmake)
include(cmake/CompileShaders.cmake)

add_subdirectory(Engine)