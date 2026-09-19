@echo off
setlocal
call "%~dp0..\vcvars.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\music mkdir build\music
copy /y build\MusicWorker.exe build\music\MusicWorker.exe >nul
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++17 /W4 native\test_client.cpp /Fo:build\test_client.obj /Fe:build\test_client.exe
if errorlevel 1 exit /b 1
build\test_client.exe
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /permissive- /std:c++20 /W4 /I native native\test_session.cpp /Fo:build\test_session.obj /Fe:build\test_session.exe /link windowsapp.lib user32.lib
if errorlevel 1 exit /b 1
build\test_session.exe
exit /b %errorlevel%
