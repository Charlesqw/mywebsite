@echo off
chcp 936 >nul
if not exist server.exe (
    echo 未找到 server.exe，请先运行 build.bat 进行编译
    pause
    exit /b 1
)
echo 启动个人网站服务器...
server.exe
pause
