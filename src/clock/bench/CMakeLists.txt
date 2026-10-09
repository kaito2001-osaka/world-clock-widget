# Render benchmark. Console exe so it can print its table to stdout.
# Renderer.cpp is compiled again here with WORLDCLOCK_BENCH, which turns on its
# per-phase timing hooks; the define covers every source of this target, so
# all of them agree on the Renderer class layout.
add_executable(WorldClockBench
    bench_main.cpp
    BenchStats.cpp
    ../Renderer.cpp
)

target_compile_definitions(WorldClockBench PRIVATE WORLDCLOCK_BENCH)
target_link_libraries(WorldClockBench PRIVATE
    worldclock_core
    d2d1 dwrite windowscodecs user32 gdi32 ole32
)

if (MSVC)
    target_compile_options(WorldClockBench PRIVATE /W4 /permissive- /utf-8 /EHsc)
    set_property(TARGET WorldClockBench PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
