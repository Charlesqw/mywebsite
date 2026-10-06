@echo off
chcp 936 >nul
echo [BUILD] 正在编译 C++ 个人网站服务器...
g++ -std=c++17 -O2 server.cpp -o server.exe -lws2_32 -lwinmm
if %errorlevel%==0 (
    echo [BUILD] 编译成功 -^> server.exe
) else (
    echo [BUILD] 编译失败，请检查 g++ 是否安装并加入 PATH
)
pause
