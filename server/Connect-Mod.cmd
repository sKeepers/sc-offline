@echo off
chcp 65001 >nul
setlocal
set "LOCAL_FLEET_PY=%LocalAppData%\Programs\Python\Python311\python.exe"
if not exist "%LOCAL_FLEET_PY%" set "LOCAL_FLEET_PY=python"
"%LOCAL_FLEET_PY%" "%~dp0run_server.py" --link-mod
pause
