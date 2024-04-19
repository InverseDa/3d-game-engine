set(FMT_LIB ${GLOBAL_THIRD_PARTY_LIB_DIR}/fmtd.lib)
set(OC_TREE_TEST_SRC)
set(OC_TREE_TEST_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB files ${OC_TREE_TEST_SRC_DIR}/*.cpp)
list(APPEND OC_TREE_TEST_SRC ${files})

add_executable(OCTreeTest main.cpp ${OC_TREE_TEST_SRC})

target_include_directories(OCTreeTest PUBLIC ${ENGINE_INCLUDE_DIR})
target_include_directories(OCTreeTest PUBLIC ${HELLOWORLD_EXAMPLE_SRC_DIR})
target_include_directories(OCTreeTest PUBLIC ${GLOBAL_INCLUDE_DIR})
target_include_directories(OCTreeTest PUBLIC ${GLOBAL_THIRD_PARTY_INCLUDE_DIR})
target_include_directories(OCTreeTest PUBLIC ${GLFW_INCLUDE_DIR})
target_include_directories(OCTreeTest PUBLIC ${FMT_INCLUDE_DIR})

target_link_libraries(OCTreeTest PUBLIC Core Vulkan::Vulkan)
