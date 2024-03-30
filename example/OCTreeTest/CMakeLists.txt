set(FMT_LIB ${GLOBAL_THIRD_PARTY_LIB_DIR}/fmtd.lib)

add_executable(OCTreeTest main.cpp)

target_include_directories(OCTreeTest PUBLIC ${ENGINE_INCLUDE_DIR})
target_include_directories(OCTreeTest PUBLIC ${HELLOWORLD_EXAMPLE_SRC_DIR})
target_include_directories(OCTreeTest PUBLIC ${GLOBAL_INCLUDE_DIR})
target_include_directories(OCTreeTest PUBLIC ${GLOBAL_THIRD_PARTY_INCLUDE_DIR})

target_link_libraries(OCTreeTest PUBLIC Core Vulkan::Vulkan)
