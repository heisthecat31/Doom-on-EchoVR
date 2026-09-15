@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
python -B build_worker.py
if errorlevel 1 exit /b 1
cl @build\worker.rsp
exit /b %errorlevel%
