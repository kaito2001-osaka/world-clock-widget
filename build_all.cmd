@echo off
REM Build BOTH components and assemble a runnable dist\ folder where the
REM gadget and the settings app sit side by side (so the gadget's right-click
REM "設定…" can launch WorldClockSettings.exe).
setlocal
set ROOT=%~dp0
set VC=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat
set CMAKE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set NINJA=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe

echo === [1/3] Building C++ gadget ===
call "%VC%" >nul || ( echo vcvars failed & exit /b 1 )
"%CMAKE%" -S "%ROOT%src\clock" -B "%ROOT%build\clock" -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" -DCMAKE_BUILD_TYPE=Release || exit /b 1
"%CMAKE%" --build "%ROOT%build\clock" --config Release || exit /b 1

echo === [2/3] Publishing C# settings app ===
dotnet publish "%ROOT%src\settings\WorldClockSettings.csproj" -c Release ^
  -o "%ROOT%dist" --nologo || exit /b 1

echo === [3/3] Copying gadget into dist\ ===
copy /Y "%ROOT%build\clock\WorldClockGadget.exe" "%ROOT%dist\" >nul || exit /b 1

echo.
echo BUILD OK. Run: %ROOT%dist\WorldClockGadget.exe
endlocal
