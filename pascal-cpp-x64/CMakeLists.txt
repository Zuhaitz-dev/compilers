cmake_minimum_required(VERSION 3.20)
project(PascalCompiler VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find LLVM package
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS} include)
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")
add_definitions(${LLVM_DEFINITIONS_LIST})

file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(pascalc ${SOURCES})

# Map clean native LLVM components
llvm_map_components_to_libnames(llvm_libs
    Core
    Support
    Analysis
    Target
    TargetParser
    MC
    native
    nativecodegen
)

target_link_libraries(pascalc PRIVATE ${llvm_libs})

if(MSVC)
    target_compile_options(pascalc PRIVATE /W4 /permissive-)
else()
    target_compile_options(pascalc PRIVATE -Wall -Wextra -Wpedantic)
endif()