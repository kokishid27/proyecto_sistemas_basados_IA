/****************************************************************************************************
 * FileName:     main.cpp
 * Dependencies: bsp.hpp
 * Company:      TecNM /IT Chihuahua
 * Description:  Definición de todas las variables globales compartidas
 * Authors:      Ana Cardona, Emiliano Pérez, Luis Anchondo
 * updated:      09/05/2026
 ***************************************************************************************************/
#include "bsp.hpp"

// ============================================================
//  SERVOS
// ============================================================

const uint8_t SERVO_PINS[NUM_SERVOS] = { 13, 12, 14, 26, 33, 32 };
const ServoConfig SERVO_CFG[NUM_SERVOS] = {
    {  500, 2400, 1278, "Base  " },
    {  500, 2300,  1911, "Hombro" },
    {  500, 2400, 2400, "Codo  " },
    {  500, 2500,  1267, "Muneca" },
    {  500, 2500, 500, "Rotac." },
    {  500, 2500, 1055, "Garra " }
};

Servo servos[NUM_SERVOS]; // Objetos físicos de los motores
volatile uint16_t servoPos[NUM_SERVOS]; // Arreglo que guarda la posición actual en µs
volatile uint8_t  activeServo = 0; // Índice del servo que responden los botones PLUS/MINUS

// ============================================================
//  SINCRONIZACIÓN FREERTOS
// ============================================================

/* Mutex para acceso a servoPos[] y activeServo.
   Cualquier tarea que lea o escriba estas variables debe tomar el mutex
   primero para evitar condiciones de carrera. */
SemaphoreHandle_t xMutex    = nullptr;
// Cola de comandos del servo
QueueHandle_t     xCmdQueue = nullptr;
// Para el manejo de la pantalla (necesita refrescarse)
SemaphoreHandle_t xUiSem        = nullptr;

// ============================================================
//  MÁQUINA DE ESTADOS
// ============================================================

volatile AppState appState      = STATE_SPLASH; // Estado actual de la aplicación
volatile uint8_t  menuCursor    = 0;   // Cursor para menú principal
volatile uint8_t  subCursor     = 0;   // Cursor para submenús
volatile uint8_t  recSeqIdx     = 0;   // slot que se está grabando
volatile uint8_t  playSeqIdx    = 0;   // slot que se está reproduciendo
volatile uint16_t playStepIdx   = 0;   // paso actual en playback (para OLED)
volatile uint16_t recStepDisplay = 0;  // número de poses grabadas (para OLED)
// Flag que taskPlayback monitorea para saber si debe detenerse
volatile bool playStopReq = false;

const char* MAIN_MENU_LABELS[MAIN_MENU_ITEMS] = {
    "Live Control",
    "Record Seq.",
    "Play Seq.",
    "Help"
};

// ============================================================
//  NAMESPACE
// ============================================================
const char* NVS_NS = "jaibarmsq";

// ============================================================
//  BUFFER DE SECUENCIAS
// ============================================================

// Buffer RAM con la secuencia que se está grabando / reproduciendo.
// Solo una secuencia cabe en RAM a la vez (suficiente para ambos casos).
/* El código permite gestionar los datos en RAM mientras se capturan las poses. 
   Solo se realiza la escritura definitiva en la NVS cuando la secuencia está lista, 
   lo que prolonga la vida útil del hardware del ESP32*/
Pose   seqBuf[MAX_STEPS_PER_SEQ];
uint16_t seqBufLen = 0; // Número de poses válidas en seqBuf

// ============================================================
//  HANDLE DE TAREA DE REPRODUCCIÓN
// ============================================================

TaskHandle_t hPlayback = nullptr;

// ============================================================
//  PERIFÉRICOS
// ============================================================

// OLED 128x64 por I2C hardware (pines SDA=21, SCL=22 definidos en config.h)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

// Mando Xbox Series X por BLE (sin dirección MAC fija)
XboxSeriesXControllerESP32_asukiaaa::Core xboxCtrl("");

// Almacenamiento no volátil (NVS)
Preferences prefs;
