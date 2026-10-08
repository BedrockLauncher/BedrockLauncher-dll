@echo off
rem Builds src\bin\gamelaunchhelper.dll with the Visual Studio toolchain (x64), without the C runtime.
rem Run from a "x64 Native Tools Command Prompt", or let this script locate vcvars64.bat.
setlocal
cd /d "%~dp0src"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
where cl.exe >nul 2>nul
if not errorlevel 1 goto build
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSROOT=%%i"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

:build
if exist "bin" rd /q /s "bin"
md "bin"
if exist "obj" rd /q /s "obj"
md "obj"

rc.exe /nologo /fo "obj\Library.res" "Resources\Library.rc" || exit /b 1
cl.exe /nologo /c /O1 /GS- /Gy /W4 /WX /Zl /Fo"obj\Library.obj" "Library.c" || exit /b 1
link.exe /nologo /DLL /NODEFAULTLIB /ENTRY:DllMain /OPT:REF /OPT:ICF /MACHINE:X64 "obj\Library.obj" "obj\Library.res" kernel32.lib user32.lib wtsapi32.lib /OUT:"bin\gamelaunchhelper.dll" || exit /b 1

echo Built %CD%\bin\gamelaunchhelper.dll
