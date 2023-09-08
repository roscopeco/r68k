cmake_minimum_required(VERSION 3.6)
project(r68k)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS_DEBUG "-g")

link_directories(machine musashi rosco-m68k-glue)

add_executable(
        ${PROJECT_NAME}

        main.cpp
        musashi/m68k.h
        musashi/m68kconf.h
        musashi/m68kcpu.c
        musashi/m68kcpu.h
        musashi/m68kdasm.c
        musashi/m68kops.c
        musashi/softfloat/softfloat.c
        rosco-m68k-glue/memoryglue.cpp
        rosco-m68k-glue/cpuglue.cpp
        machine/Memory.cpp
        machine/AddressDecoder.cpp
        include/Memory.h
        include/AddressDecoder.h
)

add_compile_options(-Wimplicit-function-declaration)

target_include_directories(${PROJECT_NAME} PUBLIC include)

