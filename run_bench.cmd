@echo off
REM Build and run the render benchmark (WorldClockBench).
REM   run_bench.cmd                  per-phase timing table
REM   run_bench.cmd --frames 300     more frames per row
REM   run_bench.cmd --write-golden   save reference frames to build\golden
REM   run_bench.cmd --check-golden   compare against them byte for byte
setlocal
set VC=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat
set CMAKE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set NINJA=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe

call "%VC%" >nul
if errorlevel 1 ( echo vcvars failed & exit /b 1 )

"%CMAKE%" -S "%~dp0src\clock" -B "%~dp0build\clock" -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" -DCMAKE_BUILD_TYPE=Release -DWORLDCLOCK_BUILD_BENCH=ON
if errorlevel 1 ( echo configure failed & exit /b 1 )

"%CMAKE%" --build "%~dp0build\clock" --target WorldClockBench
if errorlevel 1 ( echo build failed & exit /b 1 )

REM Goldens live outside build\clock so wiping the build tree keeps them.
REM A later --golden-dir on the command line overrides this one.
"%~dp0build\clock\bench\WorldClockBench.exe" --golden-dir "%~dp0build\golden" %*
if errorlevel 1 ( echo BENCH FAILED & exit /b 1 )
endlocal
