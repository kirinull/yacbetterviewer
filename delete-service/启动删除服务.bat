@echo off
chcp 65001 >nul
title Pineapple 删除服务
cd /d "%~dp0"
echo.
echo   Pineapple 删除服务
echo   端口 8894
echo   删除的文件会移到 F:\manga\.deleted\ 保留，不会直接销毁
echo.
echo   关闭此窗口 = 停止服务（客户端将无法删除）
echo.
python delete_service.py
pause
