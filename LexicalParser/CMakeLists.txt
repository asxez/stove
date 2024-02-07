cmake_minimum_required(VERSION 3.27)

project(LexicalParser C)

include_directories(include)
#include_directories(../objectAndClass/include)

add_executable(LexParser cli/cli.c)

#target_include_directories(main PUBLIC ../objectAndClass/include)
#target_include_directories(LexParser PRIVATE ../objectAndClass/include)
