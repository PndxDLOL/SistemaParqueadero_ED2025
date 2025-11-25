@echo off
echo Compilando DLL, servidor y cliente...
g++ -std=c++17 -O2 -shared parkinglib.cpp -o parking.dll -static -static-libgcc -static-libstdc++
g++ -std=c++17 -O2 server.cpp -o server.exe -lws2_32
g++ -std=c++17 -O2 cliente.cpp -o cliente.exe -lws2_32

echo Iniciando procesos...
start cmd /k "server.exe"
start cmd /k "python Grafico_parqueadero.py"
start cmd /k "cliente.exe"

echo Todo listo.
pause
