// Trabajen sobre esta branchpara desarrollar el proyecto, no sobre main
// Descarguen la extensión de gitlens para VSCode, para poder ver los cambios 
// que hizo cada uno.


//si esta comentado no se compila el main_captura.cpp


#include <Arduino.h>
#include <ESP_I2S.h>

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



void setup() {

}

void loop() {
  
}
#endif