import os
import numpy as np
import matplotlib.pyplot as plt
from scipy.io import wavfile

# Rutas de los dos archivos WAV a comparar
ARCHIVO_ORIGINAL = "/home/kokshid27/Documents/proyecto_sistemas_basados/dataset/train/caja/diego_caja_30seg.wav"
ARCHIVO_FILTRADO = "/home/kokshid27/Documents/proyecto_sistemas_basados/train_filtrado/caja/diego_caja_30seg.wav"

def cargar_wav(ruta: str):
    if not os.path.exists(ruta):
        raise FileNotFoundError(f"No se encontró el archivo: {ruta}")
    fs, data = wavfile.read(ruta)
    if data.ndim > 1:
        data = data[:, 0]
    return fs, data.astype(np.float64)


def calcular_metricas(data: np.ndarray, fs: int):
    # 1. DC Offset (valor promedio de la señal; idealmente ~0)
    dc_offset = np.mean(data)
    
    # 2. Valor RMS (energía promedio de la señal)
    rms = np.sqrt(np.mean(data ** 2))
    
    # 3. Valores pico máximo y mínimo
    val_max = np.max(data)
    val_min = np.min(data)
    
    # 4. Espectro FFT y energía por debajo de 60 Hz (donde vive la deriva/offset)
    n = len(data)
    freqs = np.fft.rfftfreq(n, d=1.0 / fs)
    fft_mag = np.abs(np.fft.rfft(data)) / n
    
    # Energía espectral < 60 Hz vs > 60 Hz
    energia_baja = np.sum(fft_mag[freqs < 60.0] ** 2)
    energia_voz = np.sum(fft_mag[(freqs >= 60.0) & (freqs <= 4000.0)] ** 2)
    
    return {
        "dc_offset": dc_offset,
        "rms": rms,
        "max": val_max,
        "min": val_min,
        "energia_baja": energia_baja,
        "energia_voz": energia_voz,
        "freqs": freqs,
        "fft_mag": fft_mag
    }


def comparar_audios(ruta_orig: str, ruta_filt: str):
    fs_orig, orig = cargar_wav(ruta_orig)
    fs_filt, filt = cargar_wav(ruta_filt)

    if fs_orig != fs_filt:
        raise ValueError(f"Frecuencias de muestreo distintas: {fs_orig} Hz vs {fs_filt} Hz")

    m_orig = calcular_metricas(orig, fs_orig)
    m_filt = calcular_metricas(filt, fs_filt)

    # --- 1. IMPRIMIR TABLA DE MÉTRICAS EN CONSOLA ---
    print("=" * 68)
    print(f"COMPARACIÓN DE SEÑALES (Fs = {fs_orig} Hz | Muestras = {len(orig)})")
    print("=" * 68)
    print(f"{'Métrica':<28} | {'Original':>16} | {'Filtrado':>16}")
    print("-" * 68)
    print(f"{'DC Offset (Media)':<28} | {m_orig['dc_offset']:>16.2f} | {m_filt['dc_offset']:>16.2f}")
    print(f"{'Valor RMS':<28} | {m_orig['rms']:>16.2f} | {m_filt['rms']:>16.2f}")
    print(f"{'Pico Máximo (+)':<28} | {m_orig['max']:>16.0f} | {m_filt['max']:>16.0f}")
    print(f"{'Pico Mínimo (-)':<28} | {m_orig['min']:>16.0f} | {m_filt['min']:>16.0f}")
    print(f"{'Energía < 60 Hz (Deriva)':<28} | {m_orig['energia_baja']:>16.2e} | {m_filt['energia_baja']:>16.2e}")
    print(f"{'Energía 60-4000 Hz (Voz)':<28} | {m_orig['energia_voz']:>16.2e} | {m_filt['energia_voz']:>16.2e}")
    print("=" * 68)

    # --- 2. GRAFICAR TIEMPO Y FRECUENCIA ---
    t_orig = np.arange(len(orig)) / fs_orig * 1000.0  # en ms
    t_filt = np.arange(len(filt)) / fs_filt * 1000.0  # en ms

    fig, axes = plt.subplots(2, 2, figsize=(14, 8))
    fig.suptitle("Comparación Audio Original vs. Filtro Pasa-Altas (60 Hz)", fontsize=14, fontweight="bold")

    # Dominio del tiempo: Original
    axes[0, 0].plot(t_orig, orig, color="#e65100", linewidth=0.9)
    axes[0, 0].axhline(0, color="gray", linestyle="--", linewidth=0.8)
    axes[0, 0].set_title(f"Original - Tiempo (DC Offset: {m_orig['dc_offset']:.1f})")
    axes[0, 0].set_xlabel("Tiempo (ms)")
    axes[0, 0].set_ylabel("Amplitud (int16)")
    axes[0, 0].grid(True, alpha=0.3)

    # Dominio del tiempo: Filtrado
    axes[0, 1].plot(t_filt, filt, color="#1565c0", linewidth=0.9)
    axes[0, 1].axhline(0, color="gray", linestyle="--", linewidth=0.8)
    axes[0, 1].set_title(f"Filtrado - Tiempo (DC Offset: {m_filt['dc_offset']:.1f})")
    axes[0, 1].set_xlabel("Tiempo (ms)")
    axes[0, 1].set_ylabel("Amplitud (int16)")
    axes[0, 1].grid(True, alpha=0.3)

    # Dominio de la frecuencia (FFT): Superposición (Zoom 0 a 1000 Hz)
    axes[1, 0].plot(m_orig["freqs"], m_orig["fft_mag"], label="Original", color="#e65100", alpha=0.8)
    axes[1, 0].plot(m_filt["freqs"], m_filt["fft_mag"], label="Filtrado (HPF 60 Hz)", color="#1565c0", alpha=0.85)
    axes[1, 0].axvline(60.0, color="red", linestyle="--", linewidth=1.2, label="Corte 60 Hz")
    axes[1, 0].set_xlim(0, 1000)
    axes[1, 0].set_title("Espectro FFT (Zoom 0 - 1000 Hz)")
    axes[1, 0].set_xlabel("Frecuencia (Hz)")
    axes[1, 0].set_ylabel("Magnitud")
    axes[1, 0].legend()
    axes[1, 0].grid(True, alpha=0.3)

    # Dominio de la frecuencia (FFT) en escala dB (Todo el espectro)
    eps = 1e-12
    db_orig = 20 * np.log10(m_orig["fft_mag"] + eps)
    db_filt = 20 * np.log10(m_filt["fft_mag"] + eps)
    axes[1, 1].plot(m_orig["freqs"], db_orig, label="Original", color="#e65100", alpha=0.7)
    axes[1, 1].plot(m_filt["freqs"], db_filt, label="Filtrado (HPF 60 Hz)", color="#1565c0", alpha=0.85)
    axes[1, 1].axvline(60.0, color="red", linestyle="--", linewidth=1.2, label="Corte 60 Hz")
    axes[1, 1].set_title("Espectro Completo en Decibelios (dB)")
    axes[1, 1].set_xlabel("Frecuencia (Hz)")
    axes[1, 1].set_ylabel("Magnitud (dB)")
    axes[1, 1].legend()
    axes[1, 1].grid(True, alpha=0.3)

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    comparar_audios(ARCHIVO_ORIGINAL, ARCHIVO_FILTRADO)