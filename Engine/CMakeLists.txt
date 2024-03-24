function(recursive_glob_cpp dir result)
    file(GLOB_RECURSE files "${dir}/*.cpp")
    list(APPEND ${result} ${files})

    file(GLOB subdirs RELATIVE "${dir}" "${dir}/*")
    foreach(subdir ${subdirs})
        if(IS_DIRECTORY ${dir}/${subdir})
            recursive_glob_cpp(${dir}/${subdir} ${result})
        endif()
    endforeach()

    set(${result} ${${result}} PARENT_SCOPE)
endfunction()

set(FMT_LIB ${GLOBAL_THIRD_PARTY_LIB_DIR}/fmtd.lib)
set(ENGINE_SRCS)
set(ENGINE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
recursive_glob_cpp(${ENGINE_SOURCE_DIR} ENGINE_SRCS)

add_library(Core STATIC ${ENGINE_SRCS})

target_include_directories(Core PRIVATE ${ENGINE_SOURCE_DIR})
target_include_directories(Core PRIVATE ${GLOBAL_INCLUDE_DIR})
target_include_directories(Core PRIVATE ${GLOBAL_THIRD_PARTY_INCLUDE_DIR})

target_link_libraries(Core PRIVATE Vulkan::Vulkan)
target_link_libraries(Core PRIVATE glfw)
target_link_libraries(Core PRIVATE ${FMT_DIR})