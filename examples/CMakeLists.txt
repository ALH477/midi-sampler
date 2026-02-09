# Examples CMakeLists.txt

# Simple example
add_executable(simple_example simple_example.c)
target_link_libraries(simple_example midi_sampler)

# MIDI player example (only with standard build, not RT)
if(NOT ENABLE_RT_OPTIMIZATIONS)
    add_executable(midi_player ../src/midi/midi_player.c)
    target_link_libraries(midi_player midi_sampler)
endif()

# Real-time example (only if RT optimizations enabled)
if(ENABLE_RT_OPTIMIZATIONS)
    add_executable(rt_example rt_example.c)
    target_link_libraries(rt_example midi_sampler)
    target_compile_definitions(rt_example PRIVATE ENABLE_RT_OPTIMIZATIONS)
endif()

# Set working directory for examples to help find sample files
set_target_properties(simple_example PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

if(NOT ENABLE_RT_OPTIMIZATIONS)
    set_target_properties(midi_player PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

# Installation for examples
install(TARGETS simple_example
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples
)

if(NOT ENABLE_RT_OPTIMIZATIONS)
    install(TARGETS midi_player
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples
    )
endif()

if(ENABLE_RT_OPTIMIZATIONS)
    install(TARGETS rt_example
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples
    )
endif()