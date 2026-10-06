@echo off
chcp 65001 >nul
echo [BUILD] ���ڱ��� C++ ������վ������...
g++ -std=c++17 -O2 server.cpp -o server.exe -lws2_32 -lwinmm
if %errorlevel%==0 (
    echo [BUILD] ����ɹ� -^> server.exe
) else (
    echo [BUILD] ����ʧ�ܣ����� g++ �Ƿ�װ������ PATH
)
pause
