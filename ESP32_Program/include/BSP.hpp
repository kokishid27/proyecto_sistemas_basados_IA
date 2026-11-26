#pragma once
/****************************************************************************************************
 * FileName:     bsp.hpp
 * Company:      TecNM /IT Chihuahua
 * Description:  Archivo de definiciones de variables, tareas y funciones usadas en el proyecto
 * Authors:      Ana Cardona, Emiliano Pérez, Luis Anchondo
 * updated:      09/05/2026
 ***************************************************************************************************/
#include <Arduino.h>
#include <ESP32Servo.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <XboxSeriesXControllerESP32_asukiaaa.hpp>

// ============================================================
//  CONFIGURACIÓN DE HARDWARE — SERVOS
// ============================================================

#define NUM_SERVOS    6       // Número de servos del brazo
#define SERVO_MIN_US  500     // Pulso mínimo (0°)
#define SERVO_MAX_US  2500    // Pulso máximo (180°)
#define SERVO_STEP_US 10      // Paso de movimiento manual (~1°)
#define PLAY_STEP_US  60      // µs máximos que avanza cada servo por tick en playback
#define PLAY_TICK_MS  20      // ms entre ticks de reproducción

// Pines: S1=Base, S2=Hombro, S3=Codo, S4=Muñeca, S5=Rotación, S6=Garra
extern const uint8_t SERVO_PINS[NUM_SERVOS];

// ============================================================
//  CONFIGURACIÓN DE BOTONES
// ============================================================

#define BTN_SEL    18    // Seleccionar / confirmar / ciclar servo
#define BTN_PLUS   19    // Mover arriba / navegar arriba
#define BTN_MINUS   5    // Mover abajo / navegar abajo
#define BTN_ESC    17    // Escapar / volver al menú anterior
#define DEBOUNCE_MS  40  // Retardo anti-rebote en ms
#define REPEAT_MS   100  // Periodo de auto-repetición en ms

// ============================================================
//  CONFIGURACIÓN DE OLED
// ============================================================

#define OLED_SDA 21
#define OLED_SCL 22

// ============================================================
//  ARTICULACIONES DEL BRAZO ROBÓTICO
// ============================================================

struct ServoConfig {
    uint16_t    minUs;   // Límite mecánico mínimo (µs)
    uint16_t    maxUs;   // Límite mecánico máximo (µs)
    uint16_t    homeUs;  // Posición de reposo (µs)
    const char* name;    // Nombre corto para OLED y Serial
};

// Valores calibrados 
extern const ServoConfig SERVO_CFG[NUM_SERVOS];

// ============================================================
//  SECUENCIAS
// ============================================================

#define NUM_SEQUENCES     6   // Número de slots de secuencia disponibles
#define MAX_STEPS_PER_SEQ 64  // Máximo de poses por secuencia

struct Pose {
    uint16_t pos[NUM_SERVOS];  // Posiciones µs de los 6 servos en un instante
};

// ============================================================
//  COLA DE COMANDOS DE SERVO
// ============================================================

#define CMD_QUEUE_SIZE 32

enum CmdType : uint8_t {
    CMD_SELECT    = 0,  // Ciclar al siguiente servo activo
    CMD_STEP_UP   = 1,  // Incrementar posición del servo activo
    CMD_STEP_DOWN = 2,  // Decrementar posición del servo activo
    CMD_RESET_ALL = 3,  // Llevar todos los servos a posición home
};

struct ServoCmd {
    CmdType cmdType;
    uint8_t servoIdx;
};

// ============================================================
//  MÁQUINA DE ESTADOS
// ============================================================

enum AppState : uint8_t {
    STATE_SPLASH         = 0,  // Pantalla de bienvenida
    STATE_MAIN_MENU      = 1,  // Menú principal
    STATE_LIVE_CONTROL   = 2,  // Control manual (botones / Xbox)
    STATE_RECORD_MENU    = 3,  // Selección de slot para grabar
    STATE_RECORD_CAPTURE = 4,  // Captura de poses en NVS
    STATE_PLAY_MENU      = 5,  // Selección de slot para reproducir
    STATE_PLAY_RUNNING   = 6,  // Reproducción en loop
    STATE_HELP           = 7,  // Pantalla de ayuda
};

// ============================================================
//  MENÚ PRINCIPAL
// ============================================================

#define MAIN_MENU_ITEMS 4

extern const char* MAIN_MENU_LABELS[MAIN_MENU_ITEMS];

// ============================================================
//  NVS
// ============================================================

extern const char* NVS_NS;  // Namespace NVS 

// ============================================================
//  VARIABLES GLOBALES COMPARTIDAS
// ============================================================

// Servos
extern Servo servos[NUM_SERVOS]; // Objetos de los 6 servos
extern volatile uint16_t servoPos[NUM_SERVOS]; // Posición actual (µs)
extern volatile uint8_t  activeServo; // Índice del servo activo

// Sincronización FreeRTOS
extern SemaphoreHandle_t xMutex; // Protege servoPos[] y activeServo
extern QueueHandle_t xCmdQueue; // Cola de comandos → taskServoControl
extern SemaphoreHandle_t xUiSem; // Señal de refresco para la OLED

// Máquina de estados
extern volatile AppState appState; // Estado actual de la aplicación
extern volatile uint8_t menuCursor; // Cursor del menú principal
extern volatile uint8_t subCursor; // Cursor de los submenús
extern volatile uint8_t recSeqIdx; // Slot que se está grabando
extern volatile uint8_t playSeqIdx; // Slot que se está reproduciendo
extern volatile uint16_t playStepIdx; // Paso actual en playback (para OLED)
extern volatile uint16_t recStepDisplay; // Poses grabadas en sesión actual (para OLED)
extern volatile bool playStopReq; // Flag para detener taskPlayback

// Buffer de secuencias en RAM
extern Pose seqBuf[MAX_STEPS_PER_SEQ];  // Secuencia cargada/grabándose
extern uint16_t seqBufLen; // Número de poses válidas en seqBuf

// Handle de la tarea de reproducción
extern TaskHandle_t hPlayback;  // nullptr cuando no está activa

// Periféricos
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled;
extern XboxSeriesXControllerESP32_asukiaaa::Core xboxCtrl;
extern Preferences prefs;

// ============================================================
// HELPERS (helpers.cpp)
// ============================================================

float usToDeg(uint16_t us);
uint16_t clamp16(uint16_t v, uint16_t lo, uint16_t hi);
void getSnapshot(uint16_t posOut[NUM_SERVOS], uint8_t &activeOut);
void notifyUI();
void resetServosToHome();
bool processBtnGeneric(bool &stable, uint32_t &lastRepeat, bool rawPressed, uint32_t now);
void sendCmd(CmdType type, uint8_t idx);

// ============================================================
// NVS (nvs.cpp)
// ============================================================

void nvsClearSeq(uint8_t seqIdx);
void nvsWritePose(uint8_t seqIdx, uint16_t step, const Pose &pose);
void nvsCommitCount(uint8_t seqIdx, uint16_t count);
uint16_t nvsReadCount(uint8_t seqIdx);
uint16_t nvsLoadSeq(uint8_t seqIdx);

// ============================================================
// TAREAS DE CONTROL (tasks_control.cpp)
// ============================================================

void taskPlayback(void *pvParameters);
void taskButtonScan(void *pvParameters);
void taskXboxScan(void *pvParameters);
void taskServoControl(void *pvParameters);

// ============================================================
// TAREAS DE DISPLAY (tasks_display.cpp)
// ============================================================

void taskDisplayOLED(void *pvParameters);
void taskDisplaySerial(void *pvParameters);