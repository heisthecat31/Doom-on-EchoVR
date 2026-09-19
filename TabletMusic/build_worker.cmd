@echo off
setlocal
call "%~dp0..\vcvars.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /O2 /MT /EHsc /permissive- /std:c++20 /W4 /D_CRT_SECURE_NO_WARNINGS native\worker.cpp /Fo:build\ /Fe:build\MusicWorker.exe /link windowsapp.lib ole32.lib
exit /b %errorlevel%
