@echo off
chcp 65001 >nul
cd /d "%~dp0nodered"
echo ====================================
echo   Node-RED - Radar Tracking Project
echo   Editor:    http://127.0.0.1:1880/
echo   Dashboard: http://127.0.0.1:1880/ui
echo   Stop:      press Ctrl+C in this window
echo ====================================
echo.
call node-red --flowFile flows.json
echo.
echo Node-RED stopped. Press any key to close this window.
pause >nul
