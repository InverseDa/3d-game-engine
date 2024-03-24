set(VKLIB_SRCS)
set(VKLIB_DIR ${CMAKE_CURRENT_SOURCE_DIR})
file(GLOB VKLIB_SUBDIRS RELATIVE ${VKLIB_DIR} ${VKLIB_DIR}/*)
foreach (SUBDIR ${VKLIB_SUBDIRS})
    if (IS_DIRECTORY ${VKLIB_DIR}/${SUBDIR})
        file(GLOB_RECURSE SUBDIR_SRCS "${VKLIB_DIR}/${SUBDIR}/*.cpp")
        list(APPEND VKLIB_SRCS ${SUBDIR_SRCS})
    endif ()
endforeach ()

add_library(VulkanContext STATIC ${VKLIB_SRCS})

target_include_directories(VulkanContext PRIVATE ${VKLIB_DIR})

target_link_libraries(VulkanContext PRIVATE Vulkan::Vulkan)
target_link_libraries(VulkanContext PRIVATE glfw)
target_link_libraries(VulkanContext PRIVATE ${PROJECT_SOURCE_DIR}/thirdparty/lib/fmtd.lib)