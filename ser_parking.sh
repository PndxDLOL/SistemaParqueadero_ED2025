
#!/bin/bash
echo "Compilando librería dinámica..."
g++ -std=c++17 -O2 -shared parkinglib.cpp -o libparking.so -fPIC
echo "Compilando servidor..."
g++ -std=c++17 -O2 server.cpp -o server
echo "Compilando cliente..."
g++ -std=c++17 -O2 cliente.cpp -o cliente

echo "Iniciando procesos..."
gnome-terminal -- bash -c "./server; exec bash"
gnome-terminal -- bash -c "python3 test_python.py; exec bash"
gnome-terminal -- bash -c "./cliente; exec bash"
