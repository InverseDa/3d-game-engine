add_subdirectory(thirdparty/src/fmt-10.2.1)
add_subdirectory(thirdparty/src/glfw-3.4)

set_target_properties(glfw PROPERTIES FOLDER "ThirdParty/glfw")
set_target_properties(uninstall PROPERTIES FOLDER "ThirdParty/glfw")
set_target_properties(update_mappings PROPERTIES FOLDER "ThirdParty/glfw")

set_target_properties(fmt PROPERTIES FOLDER "ThirdParty/fmt")