# 🚗 Sistema Cliente-Servidor para Parqueadero (C++ y Python)

C++ + DLL (usable desde Python)
Este proyecto simula la gestión de un parqueadero con entradas y salidas en tiempo real, usando:

Cliente: Genera eventos aleatorios (placas y puestos).
Servidor: Recibe eventos y los persiste en un archivo binario mediante una DLL.
Monitor Python: Visualiza gráficamente la ocupación del parqueadero.


## ✅ Instrucciones de Ejecución

### Windows (Método Automático)
Haz doble clic en `ser_parking.bat`. 

O desde CMD ejecuta:
   ```
   ./ser_parking.bat
   ```


### Linux/macOS (Método Automático)

Da permisos y ejecuta:
   ```bat
   chmod +x ser_parkingl.sh
   ./ser_parking.sh

   ```
### Windows (Método Manual)
**g++ (MinGW-w64 base)**
1. Abrir la terminal (azul).
2. Compila la **DLL**:
   ```
   g++ -std=c++17 -O2 -shared parkinglib.cpp -o parking.dll -static -static-libgcc -static-libstdc++
   ```
3. Compila el **servidor** (Winsock) — no requiere enlazar con la DLL porque la carga en runtime:
   ```
   g++ -std=c++17 -O2 server.cpp -o server.exe -lws2_32
   ```
4. Compila el **cliente**:
   ```
   g++ -std=c++17 -O2 cliente.cpp -o cliente.exe -lws2_32
   ```

> Asegúrate de tener `parking_ro.dll` en la **misma carpeta** que `server.exe` y `test_python.py`.

**Ejecución en Windows**

En tres consolas distintas (en la misma carpeta donde se encuentran los archivos):

1. **Servidor**
   ```bat
   server.exe
   ```
2. **Cliente**
   ```bat
   cliente.exe
   ```
3. **Python (visor gráfico en tiempo real)**
   ```bat
   python Grafico_parqueadero.py
   ```

Verás las **ENTRADA/SALIDA** tanto en `server.exe` como gráficamente el ocupamiento de los puestos del parqueadero.

### Compilación en Linux/macOS (Método Manual)

```
# Librería
g++ -std=c++17 -O2 -fPIC -shared parkinglib.cpp -o libparking_ro.so
# Servidor (enlazado estático a la so)
g++ -std=c++17 server.cpp -L. -lparking -o server
# Cliente
g++ -std=c++17 cliente.cpp -o cliente
export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH
```


## ⚙️ Funcionamiento

**Cliente:** Genera placas únicas (formato AAA999).

Decide el evento entrada/salida dependiendo la ocupación en del parquadero (en cuanto más vacío esté más probable que hayan entradas, y visceversa).
Envía eventos al servidor vía TCP.

**Servidor:** Recibe eventos y los guarda en parking_events.bin usando la DLL.

**DLL:** Proporciona funciones para inicializar, agregar eventos y leer historial.

**Monitor Python:** Lee eventos desde la DLL y muestra un tablero gráfico con puestos ocupados y placas.


## 📋 Requerimientos

**Windows:**
*  MinGW-w64 (g++) en PATH.
* Python 3 con `matplotlib` y `numpy`.

**Linux/macOS:**
* g++ con soporte C++17.
* Python 3 con `matplotlib` y `numpy`.

Sin dependencias externas (solo WinAPI/libc y ctypes en Python).



## Notas
- El **servidor** en Windows carga `parking.dll` con `LoadLibraryA` y obtiene funciones con `GetProcAddress`, evitando la necesidad de un import library al compilar.
- `test_python.py` auto-detecta el sistema y carga `parking.dll` o `libparking.so` desde el directorio actual.
- No hay **dependencias externas** (solo WinAPI / libc). Para Python, únicamente `ctypes` de la biblioteca estándar.

### 👨‍💻 Créditos
Proyecto académico desarrollado por:
- Cristian Valdés Díaz
- Miguel Ángel Rendón Barrios
- Juan David Rodíguez Leguizamón