import os
import math
import numpy as np
from scipy.io import wavfile
from scipy.signal import lfilter

# Parámetros idénticos a tu firmware
HPF_CUTOFF_HZ = 60.0

# Rutas de entrada y salida (ajusta según tu estructura de carpetas)
INPUT_DIR = "/home/kokshid27/Documents/dataset/datos_nuevos"
OUTPUT_DIR = "/home/kokshid27/Documents/dataset/julio"


def aplicar_hpf_identico_c(samples: np.ndarray, sample_rate: int, cutoff_hz: float = 60.0) -> np.ndarray:
    """
    Replica exactamente la función hpf_process() en C:
    y[n] = alpha * (y[n-1] + x[n] - x[n-1])
    """
    dt = 1.0 / float(sample_rate)
    rc = 1.0 / (2.0 * math.pi * float(cutoff_hz))
    alpha = rc / (rc + dt)

    # Coeficientes del filtro IIR de 1er orden:
    # Numerador b = [alpha, -alpha]
    # Denominador a = [1.0, -alpha]
    b = np.array([alpha, -alpha], dtype=np.float64)
    a = np.array([1.0, -alpha], dtype=np.float64)

    # Convertir a flotante para procesar sin desbordamiento
    x_float = samples.astype(np.float64)

    # Aplicar filtro causal (equivalente al lazo muestra por muestra con prev_x=0, prev_y=0)
    y_float = lfilter(b, a, x_float)

    # Saturación (clipping) a rango int16_t [-32768, 32767] y truncamiento
    y_clipped = np.clip(y_float, -32768.0, 32767.0)
    return y_clipped.astype(np.int16)


def procesar_dataset(input_dir: str, output_dir: str, cutoff_hz: float):
    archivos_procesados = 0

    for root, _, files in os.walk(input_dir):
        for file in files:
            if not file.lower().endswith(".wav"):
                continue

            ruta_entrada = os.path.join(root, file)
            
            # Mantener la misma estructura de subcarpetas (ej. /caja, /noise, /unknown)
            ruta_relativa = os.path.relpath(root, input_dir)
            carpeta_destino = os.path.join(output_dir, ruta_relativa)
            os.makedirs(carpeta_destino, exist_ok=True)
            ruta_salida = os.path.join(carpeta_destino, file)

            # Leer archivo WAV
            fs, data = wavfile.read(ruta_entrada)

            # Si por alguna razón el audio es estéreo, tomar solo un canal
            if data.ndim > 1:
                data = data[:, 0]

            # Asegurar formato PCM 16-bit
            if data.dtype != np.int16:
                if np.issubdtype(data.dtype, np.floating):
                    data = np.clip(data * 32767.0, -32768, 32767).astype(np.int16)
                else:
                    raise ValueError(f"Formato no soportado en {file}: {data.dtype}")

            # Aplicar filtro pasa-altas
            data_filtrada = aplicar_hpf_identico_c(data, fs, cutoff_hz)

            # Guardar WAV filtrado
            wavfile.write(ruta_salida, fs, data_filtrada)
            archivos_procesados += 1

    print(f"Proceso completado. Se filtraron {archivos_procesados} archivos en '{output_dir}'.")


if __name__ == "__main__":
    if not os.path.exists(INPUT_DIR):
        print(f"Error: No se encontró la carpeta '{INPUT_DIR}'. Créala y coloca tus audios WAV ahí.")
    else:
        procesar_dataset(INPUT_DIR, OUTPUT_DIR, HPF_CUTOFF_HZ)