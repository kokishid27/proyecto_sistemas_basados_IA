/****************************************************************************************************
 * FileName:     tasks_control.cpp
 * Dependencies: bsp.hpp, helpers.cpp, nvs.cpp
 * Company:      TecNM /IT Chihuahua
 * Description:  Tareas de control de servos
 * Authors:      Ana Cardona, Emiliano Pérez, Luis Anchondo
 * updated:      09/05/2026
 ***************************************************************************************************/

// ============================================================
//  - taskPlayback      reproduce una secuencia desde NVS en loop
//  - taskButtonScan    escanea botones y maneja la máquina de estados
//  - taskXboxScan      lee el mando Xbox y envía comandos de servo
//  - taskServoControl  ejecuta comandos de la cola y escribe PWM
// ============================================================

#include "bsp.hpp"


// ============================================================
//  TASK: Playback  (prioridad 3)
//
//  Lee seqBuf[] (ya cargado por quien lanzó la tarea) y
//  reproduce la secuencia en loop hasta que playStopReq = true.
//  Mueve cada servo en incrementos de PLAY_STEP_US cada PLAY_TICK_MS ms.
//  Solo avanza al siguiente paso cuando TODOS los servos llegaron.
// ============================================================

void taskPlayback(void *) {

    printf("[PLAY] Iniciando seq %u (%u poses)\n",
           playSeqIdx + 1, seqBufLen);

    uint16_t step = 0;

    
    // Se inicializa con la posición actual real para no hacer un salto brusco
    // al inicio de la reproducción.
    uint16_t cur[NUM_SERVOS];
    xSemaphoreTake(xMutex, portMAX_DELAY);
    for (int i = 0; i < NUM_SERVOS; i++) cur[i] = servoPos[i];
    xSemaphoreGive(xMutex);

    // itera pasos en loop hasta que se le pida parar
    while (!playStopReq) {

        // Puntero al array de posiciones objetivo del paso actual
        const uint16_t *tgt = seqBuf[step].pos;

        // Notificar OLED del paso actual
        playStepIdx = step;
        notifyUI();
        printf("[PLAY] Step %u/%u\n", step + 1, seqBufLen);

        // Mover en incrementos hasta que todos los servos
        // lleguen al destino, chequeando ESC cada tick
        bool arrived = false;
        while (!arrived && !playStopReq) {

            arrived = true;  // se pone false si algún servo aún no llegó

            xSemaphoreTake(xMutex, portMAX_DELAY);
            for (int i = 0; i < NUM_SERVOS; i++) {
                if (cur[i] == tgt[i]) continue; // Este servo ya llegó

                arrived = false; // Al menos uno sigue en camino

                // Calcular cuánto avanzar: mínimo de (PLAY_STEP_US, distancia restante)
                // con signo según la dirección del movimiento
                int32_t delta = (int32_t)tgt[i] - (int32_t)cur[i];
                int32_t step_us = (delta > 0)
                                  ? (delta > PLAY_STEP_US ? PLAY_STEP_US : delta)
                                  : (delta < -PLAY_STEP_US ? -PLAY_STEP_US : delta);
                // Actualizar posición y enviar pulso al servo
                cur[i]      = (uint16_t)((int32_t)cur[i] + step_us);
                servoPos[i] = cur[i];
                servos[i].writeMicroseconds(cur[i]);
            }
            xSemaphoreGive(xMutex);
            
            // Esperar PLAY_TICK_MS antes del siguiente tick solo si
            // todavía hay servos en movimiento
            if (!arrived) vTaskDelay(pdMS_TO_TICKS(PLAY_TICK_MS));
        }

        // Todos los servos llegaron (o ESC pulsado): avanzar paso
        if (!playStopReq) {
            step = (step + 1) % seqBufLen;
        }
    }

    printf("[PLAY] Detenido\n");
    hPlayback = nullptr; // limpiar el handle para que taskButtonScan sepa que terminó
    vTaskDelete(nullptr); // Autoeliminar tarea
}

// ============================================================
//  TASK: ButtonScan  (prioridad 3)
//
//  Escanea los 4 botones físicos cada 10 ms con debounce y
//  auto-repetición. Maneja la máquina de estados y encola
//  comandos de servo para taskServoControl.
//
//  Pines: BTN_SEL, BTN_PLUS, BTN_MINUS, BTN_ESC (INPUT_PULLUP)
//  Lógica: botón presionado -> pin en LOW -> !digitalRead() = true
// ============================================================

void taskButtonScan(void *) {

    // Configurar pines como entrada con pull-up interno
    pinMode(BTN_SEL,   INPUT_PULLUP);
    pinMode(BTN_PLUS,  INPUT_PULLUP);
    pinMode(BTN_MINUS, INPUT_PULLUP);
    pinMode(BTN_ESC,   INPUT_PULLUP);

    // Estado de debounce por botón
    //   last* -> último valor leído del pin (para detectar flancos)
    //   stab* -> estado estable tras debounce (true = presionado)
    //   chg*  -> timestamp del último cambio detectado
    //   rep*  -> timestamp del último evento de auto-repetición
    bool lastSel  = true,  lastPlus  = true,  lastMinus  = true,  lastEsc  = true;
    bool stabSel  = false, stabPlus  = false, stabMinus  = false, stabEsc  = false;
    uint32_t chgSel = 0, chgPlus   = 0, chgMinus = 0, chgEsc = 0;
    uint32_t repSel = 0, repPlus   = 0, repMinus = 0, repEsc = 0;
    bool combo  = false;

    // Contador de poses grabadas en la sesión actual de RECORD_CAPTURE
    uint16_t recStep = 0; 

    // Tiempo de inicio para vTaskDelayUntil
    TickType_t lastWake = xTaskGetTickCount();

    while (true) {
        uint32_t now = millis();

        // Leer el estado raw de los pines (HIGH = suelto, LOW = presionado).
        // Como son INPUT_PULLUP, el pin está en HIGH cuando no hay botón.
        bool rawSel   = (digitalRead(BTN_SEL)   == HIGH);
        bool rawPlus  = (digitalRead(BTN_PLUS)  == HIGH);
        bool rawMinus = (digitalRead(BTN_MINUS) == HIGH);
        bool rawEsc   = (digitalRead(BTN_ESC)   == HIGH);

        // Lambda de debounce: detecta cambios en el pin y aplica
        // el retardo DEBOUNCE_MS antes de considerar el valor válido.
        // Luego delega en processBtnGeneric para el primer flanco + auto-repeat.
        // raw = true significa que el pin está en HIGH (botón suelto).
        // processBtnGeneric recibe !raw (true = presionado lógicamente).
        auto updateRaw = [&](bool &last, bool &stab, uint32_t &chg,
                              uint32_t &rep, bool raw) -> bool {
            if (raw != last) { last = raw; chg = now; }
            if ((now - chg) < DEBOUNCE_MS) return false;
            return processBtnGeneric(stab, rep, !raw, now);
        };

        // Aplicar debounce + auto-repetición a cada botón.
        // ev* = true en el ciclo en que se debe procesar ese botón.
        bool evSel   = updateRaw(lastSel,   stabSel,   chgSel,   repSel,   rawSel);
        bool evPlus  = updateRaw(lastPlus,  stabPlus,  chgPlus,  repPlus,  rawPlus);
        bool evMinus = updateRaw(lastMinus, stabMinus, chgMinus, repMinus, rawMinus);
        bool evEsc   = updateRaw(lastEsc,   stabEsc,   chgEsc,   repEsc,   rawEsc);

        AppState st = appState; // lectura del estado actual

        // ESC siempre vuelve al menú principal desde cualquier estado
        // excepto SPLASH y MAIN_MENU. Se procesa antes del switch para
        // que tenga prioridad sobre cualquier otro botón.
        if (evEsc && st != STATE_SPLASH && st != STATE_MAIN_MENU) {

            // Al salir de LIVE_CONTROL: mover servos a home.
            if (st == STATE_LIVE_CONTROL) {
                resetServosToHome();
            }

            // Al salir de RECORD_CAPTURE: confirmar la secuencia si
            // se grabó al menos una pose; si no, descartar silenciosamente.
            if (st == STATE_RECORD_CAPTURE) {
                if (recStep > 0) {
                    // Escribir el contador final para validar la secuencia.
                    nvsCommitCount(recSeqIdx, recStep);
                    printf("[REC] Seq %u guardada con %u poses\n",
                           recSeqIdx + 1, recStep);
                } else {
                    printf("[REC] Sin poses, secuencia descartada\n");
                }
                recStep = 0;         // resetear el contador local para la próxima sesión
                resetServosToHome(); // volver a posición neutral
            }

            // Al salir de PLAY_RUNNING: señalar a taskPlayback que se detenga.
            if (st == STATE_PLAY_RUNNING) {
                playStopReq = true;
                // Esperar a que taskPlayback termine (máximo 200 ms = 20 × 10 ms).
                uint8_t tries = 20;
                while (hPlayback != nullptr && tries-- > 0)
                    vTaskDelay(pdMS_TO_TICKS(10));
                // Si no terminó en tiempo, eliminarla forzosamente.
                if (hPlayback != nullptr) {
                    vTaskDelete(hPlayback);
                    hPlayback = nullptr;
                }
                resetServosToHome();
            }

            // Limpiar la cola de comandos pendientes para que los eventos
            // generados antes del ESC no contaminen el próximo modo.
            ServoCmd dummy;
            while (xQueueReceive(xCmdQueue, &dummy, 0) == pdTRUE) {}

            // Transición al menú principal
            menuCursor = 0;
            appState   = STATE_MAIN_MENU;
            notifyUI();
            vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(10));
            continue;   // saltar el resto del switch en este ciclo
        }

        // Máquina de estados: acciones según el estado actual 
        switch (st) {

            // MENÚ PRINCIPAL 
            case STATE_MAIN_MENU: {
                // PLUS: mover cursor hacia arriba (con wrap-around)
                if (evPlus) {
                    menuCursor = (menuCursor == 0)
                                 ? MAIN_MENU_ITEMS - 1
                                 : menuCursor - 1;
                    notifyUI();
                }
                // MINUS: mover cursor hacia abajo (con wrap-around)
                if (evMinus) {
                    menuCursor = (menuCursor + 1) % MAIN_MENU_ITEMS;
                    notifyUI();
                }
                // SEL: confirmar opción resaltada y transicionar al estado correspondiente.
                // La condición !stabPlus evita activar SEL si se está pulsando PLUS.
                if (evSel && !stabPlus) {
                    subCursor = 0;   // resetear el cursor de submenús
                    switch (menuCursor) {
                        case 0: appState = STATE_LIVE_CONTROL; break;
                        case 1: appState = STATE_RECORD_MENU;  break;
                        case 2: appState = STATE_PLAY_MENU;    break;
                        case 3: appState = STATE_HELP;         break;
                    }
                    notifyUI();
                }
                break;
            }

            // LIVE CONTROL
            case STATE_LIVE_CONTROL: {
                // Combo SEL+PLUS: reset todos los servos a home.
                // combo evita que se dispare continuamente mientras ambos están presionados.
                if (stabSel && stabPlus) {
                    if (!combo) {
                        sendCmd(CMD_RESET_ALL, 0xFF);
                        combo = true;
                    }
                    evSel = false; evPlus = false;   // consumir eventos para no procesar individualmente
                } else {
                    combo = false;
                }
                // SEL solo (sin PLUS): ciclar servo activo.
                if (evSel && !stabPlus) sendCmd(CMD_SELECT,    0xFF);
                // PLUS solo (sin SEL): mover servo activo hacia arriba.
                if (!stabSel && evPlus)  sendCmd(CMD_STEP_UP,   0xFF);
                // MINUS solo (sin SEL): mover servo activo hacia abajo.
                if (!stabSel && evMinus) sendCmd(CMD_STEP_DOWN, 0xFF);
                break;
            }

            // RECORD MENU (seleccionar slot de grabación) 
            case STATE_RECORD_MENU: {
                // Navegar los 6 slots con PLUS/MINUS
                if (evPlus) {
                    subCursor = (subCursor == 0)
                                ? NUM_SEQUENCES - 1
                                : subCursor - 1;
                    notifyUI();
                }
                if (evMinus) {
                    subCursor = (subCursor + 1) % NUM_SEQUENCES;
                    notifyUI();
                }
                // SEL: confirmar slot y entrar en modo captura.
                if (evSel && !stabPlus) {
                    recSeqIdx = subCursor; // guardar qué slot se está grabando
                    nvsClearSeq(recSeqIdx); // borrar el slot anterior
                    recStep  = 0;  // contador de poses en esta sesión
                    recStepDisplay = 0; // actualizar display
                    resetServosToHome(); // empezar desde posición neutral
                    appState  = STATE_RECORD_CAPTURE;
                    notifyUI();
                }
                break;
            }

            // RECORD CAPTURE (capturar poses) 
            // Los botones tienen tres modos según combinación:
            //   PLUS/MINUS solos   → mover servo activo
            //   SEL solo           → ciclar servo activo
            //   SEL + PLUS         → guardar pose actual en NVS
            //   SEL + MINUS        → reset all servos (no guarda)
            case STATE_RECORD_CAPTURE: {

                // Combo SEL+PLUS: guardar pose 
                if (stabSel && stabPlus) {
                    if (!combo) {
                        combo = true;
                        if (recStep < MAX_STEPS_PER_SEQ) {
                            // Leer posición actual de todos los servos.
                            Pose pose;
                            xSemaphoreTake(xMutex, portMAX_DELAY);
                            for (int i = 0; i < NUM_SERVOS; i++)
                                pose.pos[i] = servoPos[i];
                            xSemaphoreGive(xMutex);

                            // Escribir la pose en NVS y avanzar el contador.
                            nvsWritePose(recSeqIdx, recStep, pose);
                            recStep++;
                            recStepDisplay = recStep;   // actualizar display
                            printf("[REC] Pose %u guardada\n", recStep);
                            notifyUI();
                        } else {
                            // Se alcanzó el máximo de poses; informar pero no bloquear.
                            printf("[REC] Max poses (%u) alcanzado\n", MAX_STEPS_PER_SEQ);
                        }
                    }
                    evSel = false; evPlus = false;   // consumir eventos

                // Combo SEL+MINUS: reset all servos 
                } else if (stabSel && stabMinus) {
                    if (!combo) {
                        sendCmd(CMD_RESET_ALL, 0xFF);
                        combo = true;
                    }
                    evSel = false; evMinus = false;

                } else {
                    combo = false;
                }

                // SEL solo: ciclar servo activo 
                if (evSel && !stabPlus && !stabMinus)
                    sendCmd(CMD_SELECT, 0xFF);

                // PLUS/MINUS solos: mover servo activo 
                if (!stabSel && evPlus)  sendCmd(CMD_STEP_UP,   0xFF);
                if (!stabSel && evMinus) sendCmd(CMD_STEP_DOWN, 0xFF);

                break;
            }

            // PLAY MENU (seleccionar slot de reproducción)
            case STATE_PLAY_MENU: {
                // Navegar los 6 slots.
                if (evPlus) {
                    subCursor = (subCursor == 0)
                                ? NUM_SEQUENCES - 1
                                : subCursor - 1;
                    notifyUI();
                }
                if (evMinus) {
                    subCursor = (subCursor + 1) % NUM_SEQUENCES;
                    notifyUI();
                }
                // SEL: cargar la secuencia y lanzar la tarea de reproducción.
                if (evSel && !stabPlus) {
                    uint16_t cnt = nvsLoadSeq(subCursor);   // cargar en seqBuf[]
                    if (cnt == 0) {
                        // Slot vacío: no hacer nada (el usuario verá que no cambia la pantalla).
                        printf("[PLAY] Seq %u esta vacia\n", subCursor + 1);
                    } else {
                        seqBufLen  = cnt;
                        playSeqIdx = subCursor;
                        playStepIdx = 0;
                        playStopReq = false;           // limpiar bandera de parada
                        appState   = STATE_PLAY_RUNNING;
                        notifyUI();
                        // Crear la tarea de reproducción. Se auto-elimina al terminar.
                        xTaskCreate(taskPlayback, "Playback", 3072,
                                    nullptr, 3, &hPlayback);
                    }
                }
                break;
            }

            // PLAY RUNNING y HELP: nada que hacer aquí 
            // El ESC ya se maneja antes del switch para estos estados.
            case STATE_PLAY_RUNNING:
            case STATE_HELP:
            default:
                break;
        }

        // Esperar al siguiente ciclo (periodo exacto de 10 ms).
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(10));
    }
}

// ============================================================
//  TASK: XboxScan  (prioridad 3)
//
//  Activo en STATE_LIVE_CONTROL y STATE_RECORD_CAPTURE.
//  Mapea los 12 botones digitales del mando a comandos de servo.
// ============================================================

#define XBOX_BTN_COUNT 12

// Asocia cada botón del mando con un servo y una dirección de movimiento.
struct XboxBtnDef { uint8_t servoIdx; CmdType cmd; };

// Tabla de mapeo: { servo, dirección } para los 12 botones, en el mismo
// orden que los devuelve getXboxBtnRaw() (índices 0..11).
static const XboxBtnDef XBOX_BTN_MAP[XBOX_BTN_COUNT] = {
    { 0, CMD_STEP_DOWN }, { 0, CMD_STEP_UP   },   // 0=DPad izq (S1↓), 1=DPad der (S1↑)
    { 1, CMD_STEP_UP   }, { 1, CMD_STEP_DOWN },   // 2=DPad arr (S2↑), 3=DPad abj (S2↓)
    { 2, CMD_STEP_UP   }, { 2, CMD_STEP_DOWN },   // 4=X (S3↑),        5=B (S3↓)
    { 3, CMD_STEP_UP   }, { 3, CMD_STEP_DOWN },   // 6=Y (S4↑),        7=A (S4↓)
    { 4, CMD_STEP_UP   }, { 4, CMD_STEP_DOWN },   // 8=LB (S5↑),       9=RB (S5↓)
    { 5, CMD_STEP_UP   }, { 5, CMD_STEP_DOWN },   // 10=LS (S6↑),      11=RS (S6↓)
};

// Estado de debounce + auto-repetición por cada botón del mando.
struct XboxBtnState { bool stable; uint32_t lastRepeat; };

// Traduce el índice de botón (0..11) al campo booleano correspondiente
// del parser de notificaciones del mando Xbox.
static bool getXboxBtnRaw(const XboxControllerNotificationParser &n, int idx) {
    switch (idx) {
        case  0: return n.btnDirLeft;   // DPad izquierda
        case  1: return n.btnDirRight;  // DPad derecha
        case  2: return n.btnDirUp;     // DPad arriba
        case  3: return n.btnDirDown;   // DPad abajo
        case  4: return n.btnX;         // Botón X
        case  5: return n.btnB;         // Botón B
        case  6: return n.btnY;         // Botón Y
        case  7: return n.btnA;         // Botón A
        case  8: return n.btnLB;        // Bumper izquierdo
        case  9: return n.btnRB;        // Bumper derecho
        case 10: return n.btnLS;        // Stick izquierdo presionado
        case 11: return n.btnRS;        // Stick derecho presionado
        default: return false;
    }
}

void taskXboxScan(void *) {

    XboxBtnState btnState[XBOX_BTN_COUNT] = {};
    TickType_t   lastWake = xTaskGetTickCount();

    while (true) {
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(10));
        xboxCtrl.onLoop();

        AppState st     = appState;
        bool     active = (st == STATE_LIVE_CONTROL || st == STATE_RECORD_CAPTURE);

        // Si el mando no está conectado o estamos en un estado inactivo,
        // resetear debounce para evitar auto-repetición al reconectar
        if (!xboxCtrl.isConnected() || !active) {
            for (int i = 0; i < XBOX_BTN_COUNT; i++) {
                btnState[i].stable     = false;
                btnState[i].lastRepeat = 0;
            }
            continue;
        }

        if (xboxCtrl.isWaitingForFirstNotification()) continue;

        uint32_t now = millis();
        const XboxControllerNotificationParser &notif = xboxCtrl.xboxNotif;

        for (int i = 0; i < XBOX_BTN_COUNT; i++) {
            bool fire = processBtnGeneric(btnState[i].stable,
                                          btnState[i].lastRepeat,
                                          getXboxBtnRaw(notif, i), now);
            if (fire) sendCmd(XBOX_BTN_MAP[i].cmd, XBOX_BTN_MAP[i].servoIdx);
        }
    }
}

// ============================================================
//  TASK: ServoControl  (prioridad 4 — la más alta del sistema)
//
//  Único productor de pulsos PWM hacia los servos.
//  Espera comandos en xCmdQueue (bloqueante) y los ejecuta
//  con protección de mutex sobre servoPos[].
//  Solo ejecuta si el estado es LIVE_CONTROL o RECORD_CAPTURE.
// ============================================================
void taskServoControl(void *) {

    ServoCmd sc; // Estructura temporal para recibir el comando de la cola

    while (true) {
        // Bloquea la tarea hasta que llegue un comando a la cola 
        if (xQueueReceive(xCmdQueue, &sc, portMAX_DELAY) != pdTRUE) continue;

        // Solo procesa comandos si el usuario está controlando el brazo 
        AppState st = appState;
        if (st != STATE_LIVE_CONTROL && st != STATE_RECORD_CAPTURE) continue;

        // Toma el Mutex para modificar las variables de posición de forma segura 
        xSemaphoreTake(xMutex, portMAX_DELAY);

        switch (sc.cmdType) {
            
            // Cambiar al siguiente servo (Ciclo 1 -> 2 -> ... -> 6 -> 1) 
            case CMD_SELECT:
                activeServo = (activeServo + 1) % NUM_SERVOS;
                break;

            // Incrementar posición (Subir ángulo)
            case CMD_STEP_UP: {
                // Si el comando no especifica un servo (0xFF), usa el seleccionado actualmente
                uint8_t  i   = (sc.servoIdx == 0xFF) ? activeServo : sc.servoIdx;
                
                // Calcula nueva posición y asegura que no pase del límite máximo 
                uint16_t pos = clamp16(servoPos[i] + SERVO_STEP_US,
                                       SERVO_CFG[i].minUs, SERVO_CFG[i].maxUs);
                
                // Si hubo cambio, actualiza el estado y envía el pulso PWM al motor
                if (pos != servoPos[i]) { 
                    servoPos[i] = pos; 
                    servos[i].writeMicroseconds(pos); 
                }
                break;
            }

            // Decrementar posición (Bajar ángulo)
            case CMD_STEP_DOWN: {
                uint8_t  i   = (sc.servoIdx == 0xFF) ? activeServo : sc.servoIdx;
                
                // Calcula nueva posición validando que no sea menor al límite mínimo
                uint16_t pos = (servoPos[i] > SERVO_CFG[i].minUs + SERVO_STEP_US)
                               ? servoPos[i] - SERVO_STEP_US : SERVO_CFG[i].minUs;
                
                if (pos != servoPos[i]) { 
                    servoPos[i] = pos; 
                    servos[i].writeMicroseconds(pos); 
                }
                break;
            }

            // Reiniciar todo el brazo a la posición de reposo (Home)
            case CMD_RESET_ALL:
                for (int i = 0; i < NUM_SERVOS; i++) {
                    servoPos[i] = SERVO_CFG[i].homeUs; // Carga valor por defecto 
                    servos[i].writeMicroseconds(servoPos[i]); // Mueve el motor 
                }
                activeServo = 0; // Regresa a la Base 
                break;
        }

        // Libera el Mutex para que otras tareas (como el OLED) puedan leer los datos 
        xSemaphoreGive(xMutex);
    }
}