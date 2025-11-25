#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import ctypes as C
import os
import time
from datetime import datetime
import numpy as np
import matplotlib.pyplot as plt

# ------------------ CLI ------------------
def parse_args():
    p = argparse.ArgumentParser(description="Monitor gráfico de ocupación del parqueadero vía DLL")
    p.add_argument("--dll",  type=str, default="parking.dll",
                   help="Nombre de la DLL (por defecto: parking.dll). Usa parking.dll para modo solo lectura.")
    p.add_argument("--bin",  type=str, default="parking_events.bin",
                   help="Ruta del archivo de eventos (por defecto: parking_events.bin)")
    p.add_argument("--spots", type=int, default=50,
                   help="Número total de puestos (por defecto: 50)")
    p.add_argument("--rows", type=int, default=5,
                   help="Filas del tablero (por defecto: 5)")
    p.add_argument("--cols", type=int, default=10,
                   help="Columnas del tablero (por defecto: 10)")
    p.add_argument("--interval", type=float, default=1.0,
                   help="Intervalo de refresco en segundos (por defecto: 1.0)")
    return p.parse_args()

# ------------------ Interfaz C ------------------
class CarEvent(C.Structure):
    _pack_ = 1  # alineación de bytes
    _fields_ = [
        ("plate",        C.c_char * 16),
        ("spot",         C.c_int32),
        ("timestamp_ms", C.c_longlong),
        ("event_type",   C.c_int32),  # 0=entry, 1=exit
    ]

def load_library(dll_name: str):
    here = os.path.dirname(os.path.abspath(__file__))
    full = os.path.join(here, dll_name)
    if os.name == "nt" and hasattr(os, "add_dll_directory"):
        os.add_dll_directory(here)
    if not os.path.exists(full):
        raise FileNotFoundError(f"No se encontró la DLL: {full}")
    lib = C.CDLL(full)
    return lib, full

def init_library_readonly_or_fallback(lib, bin_path: str):

    ok = False
    if hasattr(lib, "pl_init_readonly"):
        lib.pl_init_readonly.argtypes = [C.c_char_p]
        lib.pl_init_readonly.restype  = C.c_int
        rc = lib.pl_init_readonly(bin_path.encode("utf-8"))
        if rc == 0:
            ok = True
            print("[monitor] pl_init_readonly OK")
        elif rc == -2:
            print("[monitor] El archivo binario no existe aún; esperando que el servidor lo cree...")
            while True:
                rc = lib.pl_init_readonly(bin_path.encode("utf-8"))
                if rc == 0:
                    ok = True
                    print("[monitor] pl_init_readonly OK (archivo apareció).")
                    break
                time.sleep(1.0)
        else:
            print(f"[monitor] pl_init_readonly fallo rc={rc} (se intentará pl_init)")
    if not ok:
        if hasattr(lib, "pl_init"):
            lib.pl_init.argtypes = [C.c_char_p]
            lib.pl_init.restype  = C.c_int
            rc = lib.pl_init(bin_path.encode("utf-8"))
            if rc != 0:
                raise RuntimeError(f"pl_init falló rc={rc} (bin={bin_path})")
            print("[monitor] pl_init OK (modo escritura/creación)")
        else:
            raise AttributeError("La DLL no provee pl_init_readonly ni pl_init.")

    lib.pl_count.argtypes = []
    lib.pl_count.restype  = C.c_ulonglong
    lib.pl_tail.argtypes  = [C.c_ulonglong, C.POINTER(CarEvent)]
    lib.pl_tail.restype   = C.c_ulonglong
    return lib

# ------------------ Estado actual ------------------
def rebuild_state(lib, total_spots: int):
    
    total = lib.pl_count()
    if total == 0:
        return set(), {}, None, 0

    buf = (CarEvent * total)()
    got = lib.pl_tail(total, buf)
    occupied_set = set()
    spot_to_plate = {}
    last_ts = None

    for i in range(int(got)):
        ev = buf[i]
        spot = int(ev.spot)
        plate = ev.plate.decode(errors="ignore")
        etype = int(ev.event_type)
        ts = int(ev.timestamp_ms)
        last_ts = ts

        if etype == 0:       # ENTRADA
            occupied_set.add(spot)
            spot_to_plate[spot] = plate
        else:                # SALIDA
            occupied_set.discard(spot)
            spot_to_plate.pop(spot, None)

    return occupied_set, spot_to_plate, last_ts, int(total)

def human_ts(ms):
    if ms is None: return "-"
    try:
        return datetime.fromtimestamp(ms/1000.0).strftime('%Y-%m-%d %H:%M:%S')
    except Exception:
        return f"<ts:{ms}>"

# ------------------ Mapa gráfico ------------------
def build_grid(occupied_set, spot_to_plate, rows, cols, total_spots):
    # Construye la grilla de ocupación y etiquetas.
    grid = np.zeros((rows, cols), dtype=np.int32)
    labels = np.empty((rows, cols), dtype=object)
    labels[:] = ""
    spot_nums = np.empty((rows, cols), dtype=object)
    spot_nums[:] = ""

    for s in range(1, total_spots+1):
        r = (s-1) // cols
        c = (s-1) % cols
        if r < rows:
            spot_nums[r, c] = str(s)  # número del puesto siempre visible
            if s in occupied_set:
                grid[r, c] = 1
                labels[r, c] = spot_to_plate.get(s, "")
            else:
                grid[r, c] = 0
                labels[r, c] = ""
    return grid, labels, spot_nums

def compute_font_sizes(rows, cols):
    base = 12.0
    scale = min(1.0, 10.0 / max(rows, cols))
    fs_plate = max(7.0, base * scale)      #Placa centrada
    fs_spotnum = max(6.0, base * scale * 0.8)  #Puesto esquina
    return fs_plate, fs_spotnum

def setup_text_layers(ax, rows, cols, fs_plate, fs_spotnum):
    # Crea las capas de texto para números de puesto y placas.
    spot_text = [[
        ax.text(c - 0.45, r - 0.45, "", ha="left", va="top",
                color="dimgray", fontsize=fs_spotnum, fontweight="bold")
        for c in range(cols)] for r in range(rows)
    ]

    plate_text = [[
        ax.text(c, r, "", ha="center", va="center",
                color="black", fontsize=fs_plate,
                bbox=dict(boxstyle="round,pad=0.2",
                          facecolor="white", alpha=0.7, edgecolor="none"))
        for c in range(cols)] for r in range(rows)
    ]
    return spot_text, plate_text

def draw_or_update(ax, im, spot_text, plate_text, grid, labels, spot_nums, rows, cols):
    # Actualiza la imagen y textos.
    im.set_data(grid)
    for r in range(rows):
        for c in range(cols):
            # número de puesto
            spot_text[r][c].set_text(spot_nums[r, c] or "")
            # placa (solo si ocupada y hay texto)
            plate = labels[r, c]
            if grid[r, c] == 1 and plate:
                plate_text[r][c].set_text(plate)
                plate_text[r][c].set_visible(True)
            else:
                plate_text[r][c].set_text("")
                plate_text[r][c].set_visible(False)
    ax.figure.canvas.draw_idle()

# ------------------ Main ------------------
def main():
    args = parse_args()
    here = os.path.dirname(os.path.abspath(__file__))

    # Validación de filas/columnas
    if args.rows * args.cols < args.spots:
        raise ValueError(f"La grilla {args.rows}x{args.cols} no alcanza para {args.spots} puestos.")

    # Cargar DLL e inicializar
    try:
        lib, full = load_library(args.dll)
        print(f"[monitor] DLL: {full}")
        lib = init_library_readonly_or_fallback(lib, os.path.join(here, args.bin))
    except Exception as e:
        print("[monitor] Error:", e)
        return

    # Setup gráfico
    from matplotlib.colors import ListedColormap
    cmap = ListedColormap(["lightgray", "limegreen"])
    # Tamaño de figura adaptativo: más celdas => figura más grande
    fig_w = max(8, args.cols * 0.7)
    fig_h = max(4, args.rows * 0.7)
    fig, ax = plt.subplots(figsize=(fig_w, fig_h))

    grid = np.zeros((args.rows, args.cols), dtype=np.int32)
    im = ax.imshow(grid, cmap=cmap, vmin=0, vmax=1)


    # ticks y rejilla

    # Quitar etiquetas y nombres
    ax.set_xticks([])
    ax.set_yticks([])
    ax.set_xlabel("")
    ax.set_ylabel("")

    # Líneas negras para separar casillas
    ax.set_xticks(np.arange(-0.5, args.cols, 1), minor=True)
    ax.set_yticks(np.arange(-0.5, args.rows, 1), minor=True)
    ax.grid(which="minor", color="black", linewidth=1)

    # Fijar límites para evitar movimiento y recorte
    ax.set_xlim(-0.5, args.cols - 0.5)
    ax.set_ylim(args.rows - 0.5, -0.5)



    # calcular tamaños de fuente y crear capas de texto
    fs_plate, fs_spotnum = compute_font_sizes(args.rows, args.cols)
    spot_text, plate_text = setup_text_layers(ax, args.rows, args.cols, fs_plate, fs_spotnum)

    plt.tight_layout()
    plt.ion()
    plt.rcParams['toolbar'] = 'None'
    plt.show(block=False)

    print("[monitor] Gráfico iniciado. Actualizando cada", args.interval, "s. Ctrl+C para salir.")
    try:
        while True:
            occupied_set, spot_to_plate, last_ts, total_events = rebuild_state(lib, args.spots)
            grid, labels, spot_nums = build_grid(occupied_set, spot_to_plate, args.rows, args.cols, args.spots)

            title = f"Ocupación: {len(occupied_set)}/{args.spots} | Eventos: {total_events} | Último: {human_ts(last_ts)}"
            ax.set_title(title, fontsize=12)
            draw_or_update(ax, im, spot_text, plate_text, grid, labels, spot_nums, args.rows, args.cols)
            plt.pause(args.interval)
    except KeyboardInterrupt:
        print("\n[monitor] Finalizado por el usuario.")

if __name__ == "__main__":
    main()