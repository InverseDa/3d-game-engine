add_subdirectory(thirdparty/src/glfw-3.4)
add_subdirectory(thirdparty/src/spdlog-1.13.0)

set_target_properties(glfw PROPERTIES FOLDER "ThirdParty/glfw")
set_target_properties(uninstall PROPERTIES FOLDER "ThirdParty/glfw")
set_target_properties(update_mappings PROPERTIES FOLDER "ThirdParty/glfw")

set_target_properties(spdlog PROPERTIES FOLDER "ThirdParty/spdlog")