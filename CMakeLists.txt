cmake_minimum_required(VERSION 3.15)
project(midi_sampler VERSION 1.0.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Options
option(BUILD_EXAMPLES "Build example programs" ON)
option(BUILD_TESTS "Build tests" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(ENABLE_RT_OPTIMIZATIONS "Enable real-time optimizations for BORE/RT Linux" ON)

# Compiler warnings
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
    
    # Real-time optimizations
    if(ENABLE_RT_OPTIMIZATIONS)
        add_compile_options(
            -O3
            -march=native
            -mtune=native
            -ffast-math
            -funroll-loops
            -fomit-frame-pointer
            -pipe
        )
        add_compile_definitions(ENABLE_RT_OPTIMIZATIONS)
        message(STATUS "Real-time optimizations enabled")
    endif()
endif()

# Find required libraries
find_package(Threads REQUIRED)

# Library sources - use RT-optimized versions if enabled
if(ENABLE_RT_OPTIMIZATIONS)
    set(SOURCES
        src/sampler_rt.c
        src/envelope_rt.c
        src/voice_rt.c
        src/sample_loader.c
        src/midi_parser.c
    )
    set(INTERNAL_HEADER src/internal_rt.h)
else()
    set(SOURCES
        src/sampler.c
        src/envelope.c
        src/voice.c
        src/sample_loader.c
        src/midi_parser.c
    )
    set(INTERNAL_HEADER src/internal.h)
endif()

# Create library
add_library(midi_sampler ${SOURCES})

target_include_directories(midi_sampler
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(midi_sampler
    PUBLIC
        Threads::Threads
        m
)

# Set library properties
set_target_properties(midi_sampler PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/midi_sampler.h
)

# Examples
if(BUILD_EXAMPLES)
    add_executable(simple_example examples/simple_example.c)
    target_link_libraries(simple_example midi_sampler)
    
    add_executable(midi_player examples/midi_player.c)
    target_link_libraries(midi_player midi_sampler)
    
    if(ENABLE_RT_OPTIMIZATIONS)
        add_executable(rt_example examples/rt_example.c)
        target_link_libraries(rt_example midi_sampler)
    endif()
endif()

# Installation
include(GNUInstallDirs)

install(TARGETS midi_sampler
    EXPORT midi_samplerTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT midi_samplerTargets
    FILE midi_samplerTargets.cmake
    NAMESPACE MIDISampler::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/midi_sampler
)

# Generate package config
include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/midi_samplerConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/midi_samplerConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/midi_sampler
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/midi_samplerConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/midi_samplerConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/midi_samplerConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/midi_sampler
)

# pkg-config
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/midi_sampler.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/midi_sampler.pc
    @ONLY
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/midi_sampler.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# Print configuration summary
message(STATUS "")
message(STATUS "MIDI Sampler Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build examples: ${BUILD_EXAMPLES}")
message(STATUS "  Build tests: ${BUILD_TESTS}")
message(STATUS "  Build shared libs: ${BUILD_SHARED_LIBS}")
message(STATUS "  RT optimizations: ${ENABLE_RT_OPTIMIZATIONS}")
if(ENABLE_RT_OPTIMIZATIONS)
    message(STATUS "  Optimized for: BORE scheduler / RT Linux kernel")
endif()
message(STATUS "")
