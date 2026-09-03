@echo off
set "D=%~dp0"
set "B=%D%..\src\vulpin.exe"
if not exist "%B%" (echo [vulpin] Building...&where gcc>nul 2>&1||(echo gcc not found&exit/b1)&pushd "%D%..\src"&gcc -O2 -o vulpin.exe vulpin.c vm.c -lm||(echo Build failed&popd&exit/b1)&popd&echo Done)
"%B%" %*
