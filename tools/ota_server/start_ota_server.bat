@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"

where python >nul 2>nul
if errorlevel 1 (
    echo Python не найден в PATH. Установите Python 3 или добавьте его в PATH.
    pause
    exit /b 1
)

python ota_server.py %*
echo.
pause
