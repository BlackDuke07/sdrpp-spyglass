@echo off
setlocal

set "VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
set "ROOT=D:\GitHub\sdrpp-spyglass"
set "SRC=%ROOT%\src\main.cpp"
set "BUILD=%ROOT%\build"
set "CORE_SRC=D:\SDRpp_source\core\src"
set "LOCAL_INC=%ROOT%\include"

call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul || exit /b 1

if not exist "%BUILD%" mkdir "%BUILD%"

cl /std:c++17 /EHsc /LD ^
  /I"%LOCAL_INC%" ^
  /I"%CORE_SRC%" ^
  /I"%CORE_SRC%\imgui" ^
  /Fo"%BUILD%\\" ^
  /Fe"%BUILD%\spyglass.dll" ^
  "%SRC%" ^
  /link /LIBPATH:"%BUILD%" sdrpp_core.lib fftw3f.lib opengl32.lib || exit /b 1

endlocal
