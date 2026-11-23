// Trabajen sobre esta branchpara desarrollar el proyecto, no sobre main
// Descarguen la extensión de gitlens para VSCode, para poder ver los cambios 
// que hizo cada uno.


//si esta comentado no se compila el main_captura.cpp


#include <Arduino.h>
#include <ESP_I2S.h>
#include <reconocimiento_de_voz_robot_inferencing.h>
// Definición de pines 
#define I2S_WS 4
#define I2S_SD 5
#define I2S_SCK 6

// Crear el objeto I2S
I2SClass i2s;



//si se comenta esta linea no se compila la siguiente parte del programa hasta el else
#define MAIN_CAPTURA_H

#ifdef MAIN_CAPTURA_H

#define NUM_SAMPLES 128
int16_t sBuffer[NUM_SAMPLES];

void setup() {
  Serial.begin(921600);
  delay(1000);

  // 1. Asignar los pines (BCLK/SCK, WS/LRC, DOUT, DIN/SD, MCLK)
  i2s.setPins(I2S_SCK, I2S_WS, -1, I2S_SD);

  // 2. Iniciar el bus I2S en modo estándar (16kHz, 16 bits, Mono, Canal Izquierdo)
  if (!i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
    Serial.println("Error al iniciar el bus I2S");
    while (1); // Detener si hay fallo
  }
}

void loop() {
  
  
  // i2s.readBytes devuelve la cantidad de bytes leídos
  size_t bytesRead = i2s.readBytes((char *)sBuffer, sizeof(sBuffer));

  if (bytesRead > 0) {
    // Envía los bytes crudos directamente por USB
    Serial.write((uint8_t *)sBuffer, bytesRead);
  }
}

#else
// ================= MODO INFERENCIA (EDGE IMPULSE) =================
#define EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW 4
#define NUM_SAMPLES 128
#define HPF_CUTOFF_HZ 70.0f  // Frecuencia de corte para eliminar DC offset y ruido grave sin afectar la voz

typedef struct {
    int16_t *buffers[2];
    uint8_t buf_select;
    uint8_t buf_ready;
    uint32_t buf_count;
    uint32_t n_samples;
} inference_t;

static inference_t inference;
static bool record_status = true;
static bool debug_nn = false;

// Variables de estado del filtro pasa-altas IIR (1er orden)
static float hpf_alpha = 0.0f;
static float hpf_prev_x = 0.0f;
static float hpf_prev_y = 0.0f;

static void hpf_init(float cutoff_hz, float sample_rate_hz) {
    float dt = 1.0f / sample_rate_hz;
    float rc = 1.0f / (2.0f * PI * cutoff_hz);
    hpf_alpha = rc / (rc + dt);
    hpf_prev_x = 0.0f;
    hpf_prev_y = 0.0f;
}

static inline int16_t hpf_process(int16_t sample) {
    float x = (float)sample;
    float y = hpf_alpha * (hpf_prev_y + x - hpf_prev_x);
    hpf_prev_x = x;
    hpf_prev_y = y;

    // Saturación para mantener dentro del rango int16_t [-32768, 32767]
    if (y > 32767.0f) return 32767;
    if (y < -32768.0f) return -32768;
    return (int16_t)y;
}

static void audio_inference_callback(uint32_t n_bytes, int16_t *sampleBuffer) {
    for (uint32_t i = 0; i < (n_bytes >> 1); i++) {
        // Filtrar cada muestra antes de guardarla en el buffer de inferencia
        int16_t filtered_sample = hpf_process(sampleBuffer[i]);
        inference.buffers[inference.buf_select][inference.buf_count++] = filtered_sample;

        if (inference.buf_count >= inference.n_samples) {
            inference.buf_select ^= 1;
            inference.buf_count = 0;
            inference.buf_ready = 1;
        }
    }
}

static void capture_samples(void *arg) {
    int16_t sampleBuffer[NUM_SAMPLES];

    while (record_status) {
        size_t bytesRead = i2s.readBytes((char *)sampleBuffer, sizeof(sampleBuffer));

        if (bytesRead > 0) {
            audio_inference_callback(bytesRead, sampleBuffer);
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    vTaskDelete(NULL);
}

static bool microphone_inference_start(void) {
    inference.buffers[0] = (int16_t *)malloc(EI_CLASSIFIER_SLICE_SIZE * sizeof(int16_t));
    if (inference.buffers[0] == NULL) {
        return false;
    }

    inference.buffers[1] = (int16_t *)malloc(EI_CLASSIFIER_SLICE_SIZE * sizeof(int16_t));
    if (inference.buffers[1] == NULL) {
        free(inference.buffers[0]);
        return false;
    }

    inference.buf_select = 0;
    inference.buf_count = 0;
    inference.n_samples = EI_CLASSIFIER_SLICE_SIZE;
    inference.buf_ready = 0;
    record_status = true;

    BaseType_t res = xTaskCreatePinnedToCore(
        capture_samples,
        "CaptureSamples",
        4096,
        NULL,
        10,
        NULL,
        0
    );

    return (res == pdPASS);
}

static bool microphone_inference_record(void) {
    while (inference.buf_ready == 0) {
        delay(1);
    }
    inference.buf_ready = 0;
    return true;
}

static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    numpy::int16_to_float(&inference.buffers[inference.buf_select ^ 1][offset], out_ptr, length);
    return 0;
}

void setup() {
    Serial.begin(921600);
    delay(1000);

    // 1. Asignar los pines (BCLK/SCK, WS/LRC, DOUT, DIN/SD, MCLK)
    i2s.setPins(I2S_SCK, I2S_WS, -1, I2S_SD);

    // 2. Iniciar el bus I2S exactamente con la misma configuración de captura (16kHz, 16 bits, Mono, Izquierdo)
    if (!i2s.begin(I2S_MODE_STD, EI_CLASSIFIER_FREQUENCY, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
        Serial.println("Error al iniciar el bus I2S");
        while (1);
    }

    // 3. Inicializar el filtro pasa-altas antes de arrancar la captura de audio
    hpf_init(HPF_CUTOFF_HZ, (float)EI_CLASSIFIER_FREQUENCY);

    // 4. Inicializar el clasificador continuo y la tarea de captura en segundo plano
    run_classifier_init();

    if (!microphone_inference_start()) {
        Serial.println("Error al reservar memoria para la inferencia");
        while (1);
    }

    Serial.println("Inferencia de voz iniciada. Escuchando...");
}

void loop() {
    if (!microphone_inference_record()) {
        return;
    }

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
    signal.get_data = &microphone_audio_signal_get_data;
    ei_impulse_result_t result = { 0 };

    EI_IMPULSE_ERROR r = run_classifier_continuous(&signal, &result, debug_nn);
    if (r != EI_IMPULSE_OK) {
        Serial.printf("Error en el clasificador (%d)\n", r);
        return;
    }

    float max_val = 0.0f;
    const char *detected_label = "";

    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        if (result.classification[ix].value > max_val) {
            max_val = result.classification[ix].value;
            detected_label = result.classification[ix].label;
        }
    }

    // Filtra comandos con confianza >= 90% (ignora ruido o clase desconocida)
    if (max_val >= 0.85f && strcmp(detected_label, "noise") != 0 && strcmp(detected_label, "unknown") != 0) {
        Serial.printf("Comando detectado: %s (%.2f%%)\n", detected_label, max_val * 100.0f);
    }
}

#endif