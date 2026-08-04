cmake_minimum_required(VERSION 3.20)
project(WorldClockGadget LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(WorldClockGadget WIN32
    main.cpp
    GadgetWindow.cpp
    Startup.cpp
    Config.cpp
    Renderer.cpp
    TimeEngine.cpp
    WorldClockGadget.rc   # app icon + version info
)

target_compile_definitions(WorldClockGadget PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if (MSVC)
    target_compile_options(WorldClockGadget PRIVATE /W4 /permissive- /utf-8 /EHsc)
    # Link the CRT STATICALLY. A dynamic CRT would require the VC++
    # redistributable on the target machine (VCRUNTIME140.dll, MSVCP140.dll,
    # MSVCP140_ATOMIC_WAIT.dll, ...), which is absent on a clean install and
    # whose installer needs admin rights -- incompatible with our per-user,
    # no-elevation setup. Static linking keeps the exe dependency-free.
    set_property(TARGET WorldClockGadget PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded")
    target_link_options(WorldClockGadget PRIVATE /OPT:REF /OPT:ICF)
endif()

target_link_libraries(WorldClockGadget PRIVATE
    d2d1 dwrite user32 gdi32 shell32 ole32 advapi32
)
