cmake_minimum_required(VERSION 3.20)
project(WorldClockGadget LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(WORLDCLOCK_BUILD_TESTS "Build the unit tests" ON)
# Off by default: run_bench.cmd turns it on. It compiles Renderer.cpp a second
# time with WORLDCLOCK_BENCH, so the product exe never carries the hooks.
option(WORLDCLOCK_BUILD_BENCH "Build the render benchmark" OFF)

# Logic with no Direct2D / window dependency. Split into a library so the unit
# tests can link it; Renderer.cpp and GadgetWindow.cpp stay in the exe because
# they are bound to a render target and an HWND.
add_library(worldclock_core STATIC
    Config.cpp
    ConfigWatch.cpp
    TextFormat.cpp
    TimeEngine.cpp
    TimerCadence.cpp
    Visibility.cpp
    WindowPlacement.cpp
)
target_include_directories(worldclock_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(worldclock_core PUBLIC
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_link_libraries(worldclock_core PUBLIC shell32 ole32)

add_executable(WorldClockGadget WIN32
    main.cpp
    GadgetWindow.cpp
    Startup.cpp
    Renderer.cpp
    WorldClockGadget.rc   # app icon + version info
)

target_link_libraries(WorldClockGadget PRIVATE
    worldclock_core
    d2d1 dwrite user32 gdi32 shell32 ole32 advapi32 wtsapi32
)

if (MSVC)
    # Link the CRT STATICALLY. A dynamic CRT would require the VC++
    # redistributable on the target machine (VCRUNTIME140.dll, MSVCP140.dll,
    # MSVCP140_ATOMIC_WAIT.dll, ...), which is absent on a clean install and
    # whose installer needs admin rights -- incompatible with our per-user,
    # no-elevation setup. Static linking keeps the exe dependency-free.
    set_property(TARGET worldclock_core WorldClockGadget PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded")
    target_compile_options(worldclock_core PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WorldClockGadget PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_options(WorldClockGadget PRIVATE /OPT:REF /OPT:ICF)
endif()

if (WORLDCLOCK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if (WORLDCLOCK_BUILD_BENCH)
    add_subdirectory(bench)
endif()
