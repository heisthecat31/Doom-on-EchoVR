@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
if not exist dist mkdir dist
if not exist dist\scripts mkdir dist\scripts
cl /nologo /O2 /MT /W4 /c /TC vendor\minhook\src\buffer.c vendor\minhook\src\hook.c vendor\minhook\src\trampoline.c vendor\minhook\src\hde\hde64.c /Fo:build\
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++17 /W4 /LD runtime.cpp build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj /Fo:build\runtime.obj /Fe:dist\EchoTabletTrainer.dll /link /IMPLIB:build\EchoTabletTrainer.lib
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++17 /LD /DSCRIPT_KIND=0 /DSCRIPT_NAME=L\"351c49438bd38225\" wrapper.cpp build\EchoTabletTrainer.lib /Fo:build\wrapper0.obj /Fe:dist\scripts\351c49438bd38225.dll
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++17 /LD /DSCRIPT_KIND=1 /DSCRIPT_NAME=L\"38965d90a823f03f\" wrapper.cpp build\EchoTabletTrainer.lib /Fo:build\wrapper1.obj /Fe:dist\scripts\38965d90a823f03f.dll
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++17 /LD /DSCRIPT_KIND=2 /DSCRIPT_NAME=L\"156208a7bf6bcfec\" wrapper.cpp build\EchoTabletTrainer.lib /Fo:build\wrapper2.obj /Fe:dist\scripts\156208a7bf6bcfec.dll
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++17 tests.cpp /Fo:build\tests.obj /Fe:build\tests.exe
if errorlevel 1 exit /b 1
build\tests.exe
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /TC /c generated\zstd.c /Fo:build\zstd.obj
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++17 manifest_merge.cpp build\zstd.obj /Fo:build\merge.obj /Fe:dist\manifest_merge.exe
exit /b %errorlevel%
