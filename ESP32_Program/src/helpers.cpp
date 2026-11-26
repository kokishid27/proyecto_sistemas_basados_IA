/****************************************************************************************************
 * FileName:     helpers.cpp
 * Dependencies: bsp.hpp
 * Company:      TecNM /IT Chihuahua
 * Description:  Funciones utilizadas para conversiones y acceso a recursos compartidos
 * Authors:      Ana Cardona, Emiliano Pérez, Luis Anchondo
 * updated:      09/05/2026
 ***************************************************************************************************/
#include "bsp.hpp"

// ============================================================
//  CONVERSIONES
// ============================================================

// Convierte microsegundos de pulso servo a grados (0-180°)
float usToDeg(uint16_t us) {
    return (float)(us - SERVO_MIN_US) * 180.0f / (SERVO_MAX_US - SERVO_MIN_US);
}

// Evita que una posición de servo supere sus límites mecánicos.
uint16_t clamp16(uint16_t v, uint16_t lo, uint16_t hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

// ============================================================
//  ACCESO SEGURO A ESTADO COMPARTIDO
// ============================================================

// Toma el mutex, lee todas las posiciones y el servo activo, libera.
// Usar esta función en lugar de leer servoPos[] directamente evita
// leer valores parcialmente actualizados
void getSnapshot(uint16_t posOut[NUM_SERVOS], uint8_t &activeOut) {
    xSemaphoreTake(xMutex, portMAX_DELAY);
    for (int i = 0; i < NUM_SERVOS; i++) posOut[i] = servoPos[i];
    activeOut = activeServo;
    xSemaphoreGive(xMutex);
}

// Se llama cada vez que la UI cambia (cambio de estado, botón pulsado).
void notifyUI() { xSemaphoreGive(xUiSem); }

// ============================================================
//  CONTROL DE SERVOS
// ============================================================

// Mueve todos los servos a su posición home definida en SERVO_CFG.
void resetServosToHome() {
    xSemaphoreTake(xMutex, portMAX_DELAY);
    for (int i = 0; i < NUM_SERVOS; i++) {
        servoPos[i] = SERVO_CFG[i].homeUs;
        servos[i].writeMicroseconds(servoPos[i]);
    }
    activeServo = 0;
    xSemaphoreGive(xMutex);
}

// ============================================================
//  BOTONES
// ============================================================

// Debounce más repetición para un botón
//  stable: estado estable (activo/inactivo) del botón.
//  lastRepeat: timestamp del último evento generado (millis).
//  rawPressed: true si el botón está presionado ahora.
//  now: valor actual de millis().
//
//  Retorna true en el flanco de bajada (primer press) y cada
//  REPEAT_MS mientras el botón permanece presionado.
bool processBtnGeneric(bool &stable, uint32_t &lastRepeat,
                       bool rawPressed, uint32_t now) {
    // Primer press: activar y generar evento inmediato.
    if (rawPressed && !stable)  { stable = true;  lastRepeat = now; return true; }
    // Release: desactivar sin generar evento.
    if (!rawPressed && stable)  { stable = false; return false; }
    // Mantenido: generar evento cada REPEAT_MS para auto-repetición.
    if (rawPressed && stable && (now - lastRepeat) >= REPEAT_MS) {
        lastRepeat = now; return true;
    }
    return false;
}

// ============================================================
//  COLA DE COMANDOS
// ============================================================

// Encola un comando de servo sin bloquear
// Si la cola está llena (taskServoControl no ha drenado a tiempo)
void sendCmd(CmdType type, uint8_t idx) {
    ServoCmd c = { type, idx };
    xQueueSend(xCmdQueue, &c, 0);
}
