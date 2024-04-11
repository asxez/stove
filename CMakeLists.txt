cmake_minimum_required(VERSION 3.27)

project(stove C)

add_executable(main main.c)

#add_subdirectory(lexicalParser)
#add_subdirectory(objectAndClass)
#add_subdirectory(vm)
#add_subdirectory(utils)
#add_subdirectory(compiler)

target_include_directories(main PUBLIC lexicalParser/include)
target_include_directories(main PUBLIC vm)
target_include_directories(main PUBLIC utils)
target_include_directories(main PUBLIC compiler)
target_include_directories(main PUBLIC objectAndClass/include)
target_include_directories(main PUBLIC gc)
