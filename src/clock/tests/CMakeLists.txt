# Unit tests. Console exe (not WIN32) so the harness can print to stdout.
add_executable(WorldClockTests
    test_framework.cpp
    test_benchstats.cpp
    test_config.cpp
    test_configwatch.cpp
    test_json.cpp
    test_textformat.cpp
    test_timeengine.cpp
    test_timercadence.cpp
    test_visibility.cpp
    test_windowplacement.cpp
    # Pure helpers of the render benchmark; compiled here so they are tested
    # whether or not the benchmark itself is built.
    ../bench/BenchStats.cpp
)

target_link_libraries(WorldClockTests PRIVATE worldclock_core)

if (MSVC)
    target_compile_options(WorldClockTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    set_property(TARGET WorldClockTests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

add_test(NAME unit COMMAND WorldClockTests)
