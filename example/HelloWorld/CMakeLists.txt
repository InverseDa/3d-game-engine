set(HELLOWORLD_EXAMPLE_SRC)
set(HELLOWORLD_EXAMPLE_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB files ${HELLOWORLD_EXAMPLE_SRC_DIR}/*.cpp)
list(APPEND ${HELLOWORLD_EXAMPLE_SRC} ${files})

set(FMT_LIB ${GLOBAL_THIRD_PARTY_LIB_DIR}/fmtd.lib)

add_executable(HelloWorld main.cpp ${HELLOWORLD_EXAMPLE_SRC})

target_include_directories(HelloWorld PUBLIC ${ENGINE_INCLUDE_DIR})
target_include_directories(HelloWorld PUBLIC ${HELLOWORLD_EXAMPLE_SRC_DIR})
target_include_directories(HelloWorld PUBLIC ${GLOBAL_INCLUDE_DIR})
target_include_directories(HelloWorld PUBLIC ${GLOBAL_THIRD_PARTY_INCLUDE_DIR})

target_link_libraries(HelloWorld PUBLIC Core Vulkan::Vulkan)
