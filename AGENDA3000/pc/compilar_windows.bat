@echo off
rem ====== EDITA ESTAS DOS RUTAS (carpetas "x86_64-w64-mingw32" de cada librería) ======
set SDL2=C:\SDL2\SDL2-2.32.8\x86_64-w64-mingw32
set SDL2IMG=C:\SDL2\SDL2_image-2.8.8\x86_64-w64-mingw32
rem =====================================================================================

rem -mwindows = el programa se abre SIN ventana de consola (cmd)
g++ -std=c++17 -O2 agenda_pipboy.cpp -o agenda_pipboy.exe -I "%SDL2%\include" -I "%SDL2IMG%\include" -L "%SDL2%\lib" -L "%SDL2IMG%\lib" -lmingw32 -lSDL2main -lSDL2 -lSDL2_image -lws2_32 -lshell32 -mwindows
if errorlevel 1 ( echo. & echo ERROR DE COMPILACION & pause & exit /b 1 )

copy /Y "%SDL2%\bin\SDL2.dll" . >nul
copy /Y "%SDL2IMG%\bin\*.dll" . >nul
echo.
echo Listo: haz doble clic en agenda_pipboy.exe (se abre sin consola)
pause
