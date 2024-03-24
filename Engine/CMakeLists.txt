set(FMT_LIB ${GLOBAL_THIRD_PARTY_LIB_DIR}/fmtd.lib)

set(ENGINE_SRCS)
set(ENGINE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
file(GLOB ENGINE_SUB_DIRS RELATIVE ${ENGINE_SOURCE_DIR} ${ENGINE_SOURCE_DIR}/*)
foreach (SUBDIR ${ENGINE_SUB_DIRS})
    if (IS_DIRECTORY ${ENGINE_SOURCE_DIR}/${SUBDIR})
        file(GLOB_RECURSE SUBDIR_SRCS "${ENGINE_SOURCE_DIR}/${SUBDIR}/*.cpp")
        list(APPEND ENGINE_SRCS ${SUBDIR_SRCS})
    endif ()
endforeach ()

add_library(Core STATIC ${ENGINE_SRCS})

target_include_directories(Core PRIVATE ${ENGINE_SOURCE_DIR})
target_include_directories(Core PRIVATE ${GLOBAL_INCLUDE_DIR})
target_include_directories(Core PRIVATE ${GLOBAL_THIRD_PARTY_INCLUDE_DIR})

target_link_libraries(Core PRIVATE Vulkan::Vulkan)
target_link_libraries(Core PRIVATE glfw)
target_link_libraries(Core PRIVATE ${FMT_DIR})