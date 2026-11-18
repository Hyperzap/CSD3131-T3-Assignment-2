cmake_minimum_required(VERSION 3.16)
project(SeamCarvingAPI CXX)

# Builds the seam carving engine for the web:
#   emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-web
# Output: web/dist/seam.js, seam.wasm, seam.d.ts

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# seamcore: the algorithms (energy, DP, Greedy, Graph Cut, step engine). Plain C++17, no dependencies.
add_library(seamcore STATIC
    src/energy.cpp
    src/carve.cpp
    src/carver.cpp
    src/seam_dp.cpp
    src/seam_dp_variants.cpp
    src/seam_greedy.cpp
    src/seam_graphcut.cpp
)
target_include_directories(seamcore PUBLIC src)
if(MSVC)
    target_compile_options(seamcore PRIVATE /W4 /permissive-)
else()
    target_compile_options(seamcore PRIVATE -Wall -Wextra)
endif()

if(NOT EMSCRIPTEN)
    # Native build: the benchmark suite (bench/bench.cpp). OpenMP is optional; without it the
    # parallel DP runs on one thread and the benchmark still works.
    find_package(OpenMP COMPONENTS CXX)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(seamcore PUBLIC OpenMP::OpenMP_CXX)
    endif()
    add_executable(seambench bench/bench.cpp)
    target_link_libraries(seambench PRIVATE seamcore)
    if(MSVC)
        target_compile_options(seambench PRIVATE /W4 /permissive-)
    else()
        target_compile_options(seambench PRIVATE -Wall -Wextra)
    endif()
    message(STATUS "Native build: seamcore + seambench (benchmark). "
                   "For seam.js / seam.wasm use: emcmake cmake -S . -B build-web")
    return()
endif()

# seam.js + seam.wasm (ES module, factory createSeamModule)
add_executable(seamweb web/bindings.cpp)
target_link_libraries(seamweb PRIVATE seamcore)
set_target_properties(seamweb PROPERTIES OUTPUT_NAME "seam" SUFFIX ".js")
target_compile_options(seamweb PRIVATE -Wall -Wextra)
target_link_options(seamweb PRIVATE
    --bind
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createSeamModule
    -sENVIRONMENT=web,worker
    -sALLOW_MEMORY_GROWTH=1
)
add_custom_command(TARGET seamweb POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/web/dist
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE_DIR:seamweb>/seam.js $<TARGET_FILE_DIR:seamweb>/seam.wasm
            ${CMAKE_SOURCE_DIR}/web/seam.d.ts ${CMAKE_SOURCE_DIR}/web/dist/
    COMMENT "Copying seam.js / seam.wasm / seam.d.ts to web/dist")
