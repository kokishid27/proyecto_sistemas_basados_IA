/****************************************************************************************************
 * FileName:     nvs.cpp
 * Dependencies: bsp.hpp, Preferences.h
 * Company:      TecNM /IT Chihuahua
 * Description:  Implementación de persistencia en NVS
 * Authors:      Ana Cardona, Emiliano Pérez, Luis Anchondo
 * updated:      09/05/2026
 ***************************************************************************************************/
#include "bsp.hpp"

// ============================================================
//  HELPERS INTERNOS CONSTRUCCIÓN DE CLAVES NVS
// ============================================================

// Genera la clave del contador de un slot.  Ej: "s3_cnt" para seqIdx=2
void nvsKeyCount(uint8_t seqIdx, char *buf, size_t len) {
    snprintf(buf, len, "s%u_cnt", seqIdx + 1);
}

// Genera la clave de una pose concreta.  Ej: "s2_p5_v" para seqIdx=1, step=5
void nvsKeyPos(uint8_t seqIdx, uint16_t step, char *buf, size_t len) {
    snprintf(buf, len, "s%u_p%u_v", seqIdx + 1, step);
}

// ============================================================
//  BORRAR
// ============================================================

// Borra completamente un slot: primero lee cuántas poses tenía
// para saber cuántas claves de pose eliminar, luego borra el contador.
// Se llama al iniciar una nueva grabación en un slot ya utilizado.
void nvsClearSeq(uint8_t seqIdx) {
    char key[16];
    prefs.begin(NVS_NS, false); // false = modo lectura/escritura
    nvsKeyCount(seqIdx, key, sizeof(key));
    uint16_t cnt = prefs.getUShort(key, 0);   // leer cuántas poses hay
    prefs.remove(key); // borrar el contador
    for (uint16_t k = 0; k < cnt; k++) {  // borrar cada pose individualmente
        nvsKeyPos(seqIdx, k, key, sizeof(key));
        prefs.remove(key);
    }
    prefs.end();
    printf("[NVS] Seq %u borrada\n", seqIdx + 1);
}

// ============================================================
//  ESCRIBIR
// ============================================================

// Escribe una única pose en NVS durante la grabación.
// Se llama al presionar SEL+PLUS en RECORD_CAPTURE.
// La escritura directa (sin buffer en RAM) garantiza que los datos
// se preservan incluso si se corta la alimentación antes de confirmar.
void nvsWritePose(uint8_t seqIdx, uint16_t step, const Pose &pose) {
    char key[16];
    prefs.begin(NVS_NS, false);
    nvsKeyPos(seqIdx, step, key, sizeof(key));
    prefs.putBytes(key, pose.pos, sizeof(pose.pos));
    prefs.end();
}


// Escribe el contador de poses para confirmar la secuencia como válida.
// Se llama al presionar ESC en RECORD_CAPTURE (si hay al menos 1 pose).
// El contador es el "sello de validez": si está a 0 el slot se trata como vacío.
void nvsCommitCount(uint8_t seqIdx, uint16_t count) {
    char key[16];
    prefs.begin(NVS_NS, false);
    nvsKeyCount(seqIdx, key, sizeof(key));
    prefs.putUShort(key, count);
    prefs.end();
    printf("[NVS] Seq %u guardada: %u poses\n", seqIdx + 1, count);
}

// ============================================================
//  LEER
// ============================================================

// Devuelve el número de poses de un slot (0 = vacío o no grabado)
uint16_t nvsReadCount(uint8_t seqIdx) {
    char key[16];
    nvsKeyCount(seqIdx, key, sizeof(key));
    prefs.begin(NVS_NS, true);
    uint16_t cnt = prefs.getUShort(key, 0);
    prefs.end();
    return cnt;
}

// Carga una secuencia completa desde NVS hacia el buffer seqBuf[].
// Retorna el número de poses leídas (0 si el slot está vacío o corrupto).
// Se llama desde PLAY_MENU antes de lanzar taskPlayback
uint16_t nvsLoadSeq(uint8_t seqIdx) {
    uint16_t cnt = nvsReadCount(seqIdx);
    // Ignorar slots vacíos o con más pasos de los permitidos
    if (cnt == 0 || cnt > MAX_STEPS_PER_SEQ) return 0;

    char key[16];
    prefs.begin(NVS_NS, true); // Abrir el namespace en solo lectura
    for (uint16_t k = 0; k < cnt; k++) {
        nvsKeyPos(seqIdx, k, key, sizeof(key));
        prefs.getBytes(key, seqBuf[k].pos, sizeof(seqBuf[k].pos));
    }
    prefs.end();
    printf("[NVS] Seq %u cargada: %u poses\n", seqIdx + 1, cnt);
    return cnt;
}