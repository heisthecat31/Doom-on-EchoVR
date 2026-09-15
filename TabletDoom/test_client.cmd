@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\doom mkdir build\doom
copy /y build\DoomWorker.exe build\doom\DoomWorker.exe >nul
if errorlevel 1 exit /b 1
copy /y data\doom1.wad build\doom\doom1.wad >nul
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++17 native\test_client.cpp /Fo:build\test_client.obj /Fe:build\test_client.exe
if errorlevel 1 exit /b 1
build\test_client.exe
exit /b %errorlevel%
