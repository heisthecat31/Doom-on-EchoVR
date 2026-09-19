@echo off
rem Locate the x64 Visual Studio build environment. Set VCVARS yourself to override.
if defined VCVARS goto :call
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :fallback
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
if defined VCVARS if exist "%VCVARS%" goto :call
:fallback
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
:call
if not exist "%VCVARS%" (
    echo ERROR: no vcvars64.bat found. Install the Visual Studio C++ tools, or set VCVARS to its path.
    exit /b 1
)
rem Some installs let vcvars64.bat chatter on stderr; errorlevel below is the real check.
call "%VCVARS%" >nul 2>nul
exit /b %errorlevel%
