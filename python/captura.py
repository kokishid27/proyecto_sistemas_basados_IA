import serial
import wave
import time
import sys

PORT = '/dev/ttyACM0'             #se cambia esta variable dependiendo si el SO es Windows o Linux
BAUD = 921600
SAMPLE_RATE = 16000
TARGET_SAMPLES = 480000    # 1 minuto (16,000 * 60)
BYTES_PER_SAMPLE = 2
TOTAL_BYTES = TARGET_SAMPLES * BYTES_PER_SAMPLE  # 960,000 bytes

ser = serial.Serial(PORT, BAUD, timeout=5)
time.sleep(2)              # Esperar a que termine de reiniciar el ESP32
ser.reset_input_buffer()   # Descartar mensajes de arranque o muestras viejas

print("Grabando 30 seg exacto...")

audio_data = bytearray()
chunk_read_size = 4096     # Leemos bloques de 4 KB desde el búfer del SO

while len(audio_data) < TOTAL_BYTES:
    bytes_left = TOTAL_BYTES - len(audio_data)
    to_read = min(chunk_read_size, bytes_left)
    
    packet = ser.read(to_read)
    if not packet:
        print("\nError: Se agotó el tiempo de espera leyendo del puerto.")
        ser.close()
        sys.exit(1)
        
    audio_data.extend(packet)
    
    samples_now = len(audio_data) // BYTES_PER_SAMPLE
    percent = (samples_now / TARGET_SAMPLES) * 100
    print(f"\rMuestras: {samples_now:,} / {TARGET_SAMPLES:,} ({percent:.1f}%)", end="")

ser.close()
print("\n\n¡480,000 muestras capturadas!")

# Guardar el archivo WAV
output_file = "jovanni_cono_30seg.wav"
with wave.open(output_file, 'wb') as wf:
    wf.setnchannels(1)
    wf.setsampwidth(BYTES_PER_SAMPLE)
    wf.setframerate(SAMPLE_RATE)
    wf.writeframes(audio_data)

print(f"Guardado en: {output_file}")