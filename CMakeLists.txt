# ---------------------------------------------------------------------------
# Thoth - CMake build (Linux / g++, Windows / MSVC)
#
# Dependencies (Debian/Ubuntu packages):
#   build-essential cmake libboost-all-dev libyaml-cpp-dev libcpp-httplib-dev
#
# Usage:
#   cmake -B build                 # configure
#   cmake --build build -j         # build ./build/thoth
#   cmake --build build --target run
#   rm -rf build                   # clean
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)

project(Thoth LANGUAGES CXX)

# --- Standard --------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit compile_commands.json so tooling (clang-tidy, IDEs) can analyse the build.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to a Release build when none is specified (e.g. plain `cmake -B build`).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- Optional GPU (CUDA) Monte-Carlo engine (pricing method "mcl_gpu") ------
# OFF by default so the CPU-only build (this devcontainer / CI) needs no CUDA
# toolkit; the engine then falls back to the CPU MCL engine at run time. Turn on
# with -DTHOTH_ENABLE_CUDA=ON on a machine with nvcc + an NVIDIA GPU (see the
# GPU build-args in Dockerfile / "run_docker_server.sh --gpu").
option(THOTH_ENABLE_CUDA "Build the CUDA GPU Monte-Carlo engine (needs nvcc + an NVIDIA GPU)" OFF)
if(THOTH_ENABLE_CUDA)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    # Ada Lovelace (RTX 40-series, e.g. the 4070 Ti) is sm_89; override with
    # -DCMAKE_CUDA_ARCHITECTURES=... for another GPU.
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES 89)
    endif()
endif()

# --- Dependencies ----------------------------------------------------------
# Boost: date_time is linked; Boost.Math (header-only) provides the gamma CDF
# and the Gauss-Kronrod quadrature used by the analytics.
find_package(Boost REQUIRED COMPONENTS date_time)

# yaml-cpp provides a CMake package config (target yaml-cpp::yaml-cpp).
find_package(yaml-cpp REQUIRED)

# cpp-httplib powers the HTTP pricing server/client (resolved via pkg-config); needs threads.
find_package(PkgConfig REQUIRED)
pkg_check_modules(HTTPLIB REQUIRED IMPORTED_TARGET cpp-httplib)
find_package(Threads REQUIRED)

# --- Core library ----------------------------------------------------------
# Everything except the main entry point, so tests can link the engine.
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/thoth.cpp")

# GPU backend: the CUDA kernel (mcl_gpu.cu) replaces the CPU stub when CUDA is on.
# The .cu is not picked up by the *.cpp glob, so it is only ever built here.
if(THOTH_ENABLE_CUDA)
    list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/tasks/mcl_gpu_stub.cpp")
    list(APPEND CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/tasks/mcl_gpu.cu")
endif()

add_library(thoth_core STATIC ${CORE_SOURCES})
target_include_directories(thoth_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/contracts"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/tasks"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/nodes"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/marketdata"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/underlyings"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/helpers"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vol_correl"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/fixings"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
)
target_link_libraries(thoth_core PUBLIC
    Boost::date_time
    yaml-cpp::yaml-cpp
    PkgConfig::HTTPLIB
    Threads::Threads
)

# Warnings: -Wall -Wextra catches the uninitialised-member / unused-result
# classes of bug. Kept non-fatal (no -Werror) so the build still succeeds even
# on a newer compiler. GSL deprecations are silenced as they are noise. Applied
# to every first-party target (the engine library and the executable); the test
# target adds them in tests/CMakeLists.txt.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(THOTH_WARNING_FLAGS -Wall -Wextra -Wno-deprecated-declarations)
    # CXX only: nvcc must not receive the host -W flags directly (it would choke).
    target_compile_options(thoth_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${THOTH_WARNING_FLAGS}>)
endif()

# Link the CUDA runtime + cuRAND and flag the engine as CUDA-built.
if(THOTH_ENABLE_CUDA)
    target_link_libraries(thoth_core PUBLIC CUDA::curand CUDA::cudart)
    target_compile_definitions(thoth_core PUBLIC THOTH_CUDA)
    set_target_properties(thoth_core PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
endif()

# --- Executable ------------------------------------------------------------
add_executable(thoth "${CMAKE_CURRENT_SOURCE_DIR}/src/thoth.cpp")
target_link_libraries(thoth PRIVATE thoth_core)
target_compile_options(thoth PRIVATE ${THOTH_WARNING_FLAGS})

# Build identifier (e.g. git commit) baked into the binary and printed at
# startup, so it is unambiguous which source a running / containerised server
# was built from. Override with -DTHOTH_BUILD_ID=... (run_serve.sh passes the
# git short SHA through the Docker build).
set(THOTH_BUILD_ID "dev" CACHE STRING "Build identifier shown at startup")
target_compile_definitions(thoth PRIVATE THOTH_BUILD_ID="${THOTH_BUILD_ID}")

# --- Tests (CTest) ---------------------------------------------------------
# ON for local development; the production Docker image builds with it OFF so
# the test sources are not required in the image build context.
option(THOTH_BUILD_TESTS "Build the unit tests" ON)
if(THOTH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Convenience -----------------------------------------------------------
add_custom_target(run
    COMMAND thoth
    DEPENDS thoth
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    USES_TERMINAL
)
