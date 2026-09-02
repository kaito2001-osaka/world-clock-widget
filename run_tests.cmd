@echo off
REM Build and run the C++ unit tests (CTest).
setlocal
set VC=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat
set CMAKE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set CTEST=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe
set NINJA=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe

call "%VC%" >nul
if errorlevel 1 ( echo vcvars failed & exit /b 1 )

"%CMAKE%" -S "%~dp0src\clock" -B "%~dp0build\clock" -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" -DCMAKE_BUILD_TYPE=Release -DWORLDCLOCK_BUILD_TESTS=ON
if errorlevel 1 ( echo configure failed & exit /b 1 )

"%CMAKE%" --build "%~dp0build\clock" --target WorldClockTests
if errorlevel 1 ( echo build failed & exit /b 1 )

"%CTEST%" --test-dir "%~dp0build\clock" --output-on-failure
if errorlevel 1 ( echo TESTS FAILED & exit /b 1 )

echo.
echo TESTS OK
endlocal
