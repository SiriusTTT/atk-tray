@echo off
setlocal

set SRC=src\main.cpp
set OUT=bin\atk-tray.exe

if not exist bin mkdir bin

where g++ >nul 2>nul
if %ERRORLEVEL%==0 (
    echo [Build] Using MinGW g++...
    g++ -o %OUT% %SRC% -static -mwindows -municode -lsetupapi -lhid -luser32 -lgdi32 -lshell32 -ladvapi32 -O2
    if %ERRORLEVEL%==0 (
        echo [Build] Success: %OUT%
    ) else (
        echo [Build] FAILED
    )
    goto :end
)

where cl >nul 2>nul
if %ERRORLEVEL%==0 (
    echo [Build] Using MSVC cl.exe...
    cl /Fe%OUT% /Fo bin\main.obj %SRC% /link setupapi.lib hid.lib user32.lib gdi32.lib shell32.lib advapi32.lib /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup
    if %ERRORLEVEL%==0 (
        echo [Build] Success: %OUT%
    ) else (
        echo [Build] FAILED
    )
    goto :end
)

echo [Build] ERROR: No compiler found. Install MinGW (g++) or run from VS Developer Command Prompt (cl.exe).

:end
endlocal
