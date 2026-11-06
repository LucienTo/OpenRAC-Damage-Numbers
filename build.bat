@echo off
rem Builds the mod with MSVC alone (no CMake), as openrac_add_mod would, and writes its package
rem folder: build\package\damage-numbers\ (the manifest with its plugin entry, the DLL, the digit
rem props in assets\, README and LICENSE). Copy that folder to
rem %%USERPROFILE%%\OpenRAC\active\rac1\data\plugins\ to try it. The SDK: OPENRAC_SDK (an OpenRAC
rem checkout's sdk\ folder, interface 1.12), else .sdk\openrac-mod-sdk-1.12.0.
setlocal
cd /d "%~dp0"

set "SDK=%OPENRAC_SDK%"
if "%SDK%"=="" set "SDK=%~dp0.sdk\openrac-mod-sdk-1.12.0"
if not exist "%SDK%\include\openrac\mod.hpp" goto nosdk

where cl >nul 2>nul
if errorlevel 1 call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl >nul 2>nul
if errorlevel 1 goto nocl

if not exist build mkdir build
set "PKG=build\package\damage-numbers"
if not exist "%PKG%\lib\windows-x86_64" mkdir "%PKG%\lib\windows-x86_64"

rem C++17 and the DLL runtime (/MD), as the port has it; the manifest's api 1.12 is the minor the
rem library reports (OPENRAC_MOD_TARGET_MINOR).
cl /nologo /std:c++17 /O2 /MD /EHsc /W4 /permissive- /utf-8 ^
   /DOPENRAC_MOD_TARGET_MINOR=12 /I "%SDK%\include" ^
   src\mod.cpp /LD /Fo:build\ /Fe:build\damage_numbers.dll
if errorlevel 1 exit /b 1

copy /y build\damage_numbers.dll "%PKG%\lib\windows-x86_64\" >nul
rem The glyph props, a folder per style (tools\make_digits.py makes them).
if exist "%PKG%\assets" rmdir /s /q "%PKG%\assets"
xcopy /e /i /q /y assets "%PKG%\assets" >nul
if errorlevel 1 exit /b 1
copy /y README.md "%PKG%\" >nul
copy /y LICENSE "%PKG%\" >nul
powershell -NoProfile -Command ^
  "$m = Get-Content -Raw openrac-mod.json | ConvertFrom-Json;" ^
  "$m | Add-Member -NotePropertyName plugin -NotePropertyValue ([pscustomobject]@{'windows-x86_64'='lib/windows-x86_64/damage_numbers.dll'}) -Force;" ^
  "[IO.File]::WriteAllText('%PKG%\openrac-mod.json', ($m | ConvertTo-Json -Depth 8))"
if errorlevel 1 exit /b 1
echo Packaged in %PKG%
exit /b 0

:nosdk
echo No OpenRAC mod SDK in %SDK%: set OPENRAC_SDK to an OpenRAC checkout's sdk folder (interface 1.12).
exit /b 1

:nocl
echo No MSVC compiler found (Visual Studio 18's vcvars64.bat).
exit /b 1
