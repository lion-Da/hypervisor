@echo off
setlocal enabledelayedexpansion

set DRIVER_PATH=%1
if "%DRIVER_PATH%"=="" (
    echo Error: Driver path not specified
    exit /b 1
)

echo Signing driver: %DRIVER_PATH%
signtool sign /a /v /s PrivateCertStore /n Contoso.com(Test) /fd SHA256 %DRIVER_PATH%

if !ERRORLEVEL! neq 0 (
    echo Error: Driver signing failed
    exit /b !ERRORLEVEL!
)

echo Driver signed successfully