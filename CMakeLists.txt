cmake_minimum_required(VERSION 3.27)

project(stove C)

add_executable(main main.c)

add_subdirectory(LexicalParser)
add_subdirectory(objectAndClass)

target_include_directories(main PRIVATE ../LexicalParser/include)
