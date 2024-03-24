set(HELLOWORLD_EXAMPLE_SRC)
set(HELLOWORLD_EXAMPLE_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB files ${HELLOWORLD_EXAMPLE_SRC_DIR}/*.cpp)
list(APPEND ${HELLOWORLD_EXAMPLE_SRC} ${files})

add_executable(HelloWorld main.cpp ${HELLOWORLD_EXAMPLE_SRC})

target_include_directories(HelloWorld PUBLIC ${ENGINE_INCLUDE_DIR})
target_include_directories(HelloWorld PUBLIC ${HELLOWORLD_EXAMPLE_SRC_DIR})
