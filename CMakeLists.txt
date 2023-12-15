cmake_minimum_required(VERSION 3.27)
project(clox C)

set(CMAKE_C_STANDARD 23)

add_executable(clox main.c
        chunk.h
        chunk.c
        memory.h
        memory.c
        tools/debug.h
        tools/debug.c
        types/value.h
        types/value.c
        tools/debug.c
        vm/vm.h
        vm/vm.c
        compiler/compiler.h
        compiler/compiler.c
        compiler/scanner.h
        compiler/scanner.c
        types/object.h
        types/object.c
        memory.c
        types/table.h
        types/table.c
        vm/vm.c
        compiler/compiler.c
        common.h
        vm/vm.c
        vm/vm.c
        vm/vm.c)
