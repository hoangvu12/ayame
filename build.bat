@echo off
REM =============================================================================
REM ayame build — MSVC x64
REM =============================================================================
setlocal

if not defined VSCMD_VER (
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else (
        echo [!] vcvars64.bat not found - run this from a VS developer prompt
        exit /b 1
    )
)

cd /d "%~dp0"
if not exist build mkdir build
if not exist dist mkdir dist

echo.
echo  --- [1/2] EOSSDK-Win64-Shipping.dll (EOS proxy) ---
echo.
ml64 /nologo /c /Fo"build\eos_fwd.obj" src\generated\fwd_stub.asm || exit /b 1
cl /nologo /LD /O2 /MT /W3 /Isrc src\eos\eos_proxy.c src\common\ay_log.c src\common\ay_config.c build\eos_fwd.obj /Fe"dist\EOSSDK-Win64-Shipping.dll" /Fobuild\ /link /DEF:src\generated\exports.def /MACHINE:X64 /SUBSYSTEM:WINDOWS || exit /b 1

echo.
echo  --- [2/2] winmm.dll (loader + steam hooks) ---
echo.
if exist src\generated\winmm_fwd_stub.asm (
    ml64 /nologo /c /Fo"build\winmm_fwd.obj" src\generated\winmm_fwd_stub.asm || exit /b 1
    cl /nologo /LD /O2 /MT /W3 /Isrc src\loader\winmm_proxy.c src\common\ay_log.c src\common\ay_config.c src\common\ay_hook.c build\winmm_fwd.obj /Fe"dist\winmm.dll" /Fobuild\ /link user32.lib advapi32.lib /DEF:src\generated\winmm_exports.def /MACHINE:X64 /SUBSYSTEM:WINDOWS || exit /b 1
) else (
    echo [i] winmm forwarder not generated yet - run genexports on system32\winmm.dll
)

echo.
echo  --- [3/4] eossmoke.exe (dev smoke test) ---
echo.
cl /nologo /O2 /MT /W3 tools\eossmoke.c /Fe"dist\eossmoke.exe" /Fobuild\ || exit /b 1

echo.
echo  --- [4/4] ayame.exe (CLI with embedded DLLs) ---
echo.
copy /y dist\winmm.dll internal\payloads\winmm.dll >nul || exit /b 1
copy /y dist\EOSSDK-Win64-Shipping.dll internal\payloads\EOSSDK-Win64-Shipping.dll >nul || exit /b 1
go build -trimpath -ldflags "-s -w" -o dist\ayame.exe .\cmd\ayame || exit /b 1

echo.
echo  BUILD COMPLETE
dir /b dist\*.dll dist\ayame.exe
endlocal
