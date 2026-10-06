@echo off
chcp 65001 >nul
if not exist server.exe (
    echo δ�ҵ� server.exe���������� build.bat ���б���
    pause
    exit /b 1
)
echo ����������վ������...
server.exe
pause
