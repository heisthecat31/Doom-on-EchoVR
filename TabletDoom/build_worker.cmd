@echo off
setlocal
call "%~dp0..\vcvars.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
python -B build_worker.py
if errorlevel 1 exit /b 1
cl @build\worker.rsp
exit /b %errorlevel%
