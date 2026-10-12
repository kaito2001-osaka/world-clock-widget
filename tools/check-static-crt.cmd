@echo off
REM Fail if an exe imports the dynamic CRT (VCRUNTIME*, MSVCP*, the UCRT).
REM
REM The gadget links the CRT statically (MSVC_RUNTIME_LIBRARY in
REM src\clock\CMakeLists.txt). The VC++ redistributable is missing on clean
REM machines and installing it needs admin rights, which the per-user install
REM cannot ask for. Dev machines have it, so a dynamic-CRT build runs fine
REM there; this check is what notices.
REM
REM Usage: check-static-crt.cmd path\to\exe
REM Needs dumpbin on PATH, so call it after vcvars64.bat.
setlocal
set "EXE=%~1"
if "%EXE%"=="" ( echo usage: check-static-crt.cmd ^<exe^> & exit /b 1 )

REM Paths are echoed only outside ( ) blocks: a ")" in them, as in
REM "Program Files (x86)", would close the block early.
echo Checking CRT imports of %EXE%
if not exist "%EXE%" ( echo FAILED: file not found & exit /b 1 )
where dumpbin >nul 2>nul || ( echo FAILED: dumpbin is not on PATH; run vcvars64.bat first & exit /b 1 )

REM Not next to the exe: make_installer.cmd packages everything in build\stage.
set "DEPS=%TEMP%\check-static-crt-%RANDOM%.txt"
dumpbin /nologo /dependents "%EXE%" >"%DEPS%" 2>&1
if errorlevel 1 (
    type "%DEPS%"
    del "%DEPS%" 2>nul
    echo FAILED: dumpbin could not read the exe
    exit /b 1
)

REM Without the import list a match below would prove nothing, so a change in
REM dumpbin's output fails here instead of passing silently.
findstr /c:"Image has the following dependencies" "%DEPS%" >nul
if errorlevel 1 (
    type "%DEPS%"
    del "%DEPS%" 2>nul
    echo FAILED: no import list in the dumpbin output above
    exit /b 1
)

REM Import names are the indented lines, so anchor at the line start; that keeps
REM the "Dump of file" line out. Delay-load imports are listed the same
REM way and count too. /r because findstr can miss matches with several
REM literal strings.
findstr /r /i /c:"^ *vcruntime" /c:"^ *msvcp" /c:"^ *api-ms-win-crt-" /c:"^ *ucrtbase" "%DEPS%"
if errorlevel 2 (
    del "%DEPS%" 2>nul
    echo FAILED: findstr could not read the dumpbin output
    exit /b 1
)
if not errorlevel 1 (
    del "%DEPS%" 2>nul
    echo FAILED: the exe imports the dynamic CRT DLLs listed above.
    echo It must link the CRT statically: the VC++ redistributable is missing on
    echo clean machines and installing it needs admin rights. Check
    echo MSVC_RUNTIME_LIBRARY in src\clock\CMakeLists.txt.
    exit /b 1
)

del "%DEPS%" 2>nul
echo Static CRT OK
REM Explicit 0: errorlevel is still 1 from the findstr that found nothing, and
REM neither del nor echo resets it.
exit /b 0
