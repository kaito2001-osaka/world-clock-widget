@echo off
REM One-click: build both components, stage a self-contained publish, and
REM compile a single per-user installer -> dist_installer\WorldClockGadget-Setup.exe
setlocal
set ROOT=%~dp0
set VC=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat
set CMAKE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set NINJA=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe
set DOTNET=C:\Program Files\dotnet\dotnet.exe
set ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe
set STAGE=%ROOT%build\stage

echo === [1/4] Building C++ gadget (Release) ===
call "%VC%" >nul || ( echo vcvars failed & exit /b 1 )
"%CMAKE%" -S "%ROOT%src\clock" -B "%ROOT%build\clock" -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" -DCMAKE_BUILD_TYPE=Release || exit /b 1
"%CMAKE%" --build "%ROOT%build\clock" --config Release || exit /b 1

echo === [2/4] Publishing settings app (self-contained, .NET bundled) ===
if exist "%STAGE%" rmdir /s /q "%STAGE%"
"%DOTNET%" publish "%ROOT%src\settings\WorldClockSettings.csproj" -c Release ^
  -r win-x64 --self-contained true -p:SelfContained=true -o "%STAGE%" --nologo || exit /b 1

echo === [3/4] Staging gadget exe ===
copy /y "%ROOT%build\clock\WorldClockGadget.exe" "%STAGE%\" >nul || exit /b 1

echo === [4/4] Compiling installer ===
"%ISCC%" "%ROOT%installer\WorldClockGadget.iss" || exit /b 1

echo.
echo INSTALLER OK: %ROOT%dist_installer\WorldClockGadget-Setup.exe
endlocal
