cmake_minimum_required(VERSION 3.27)

project(stove C)

add_executable(main main.c)

add_subdirectory(LexicalParser)
add_subdirectory(objectAndClass)

target_include_directories(main PRIVATE ../LexicalParser/include)

find_package(Python3 COMPONENTS Development REQUIRED)
target_link_libraries(main PRIVATE Python3::Python)
