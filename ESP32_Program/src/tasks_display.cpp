/****************************************************************************************************
 * FileName:     tasks_display.cpp
 * Dependencies: bsp.hpp, helpers.cpp, nvs.cpp
 * Company:      TecNM /IT Chihuahua
 * Description:  Tareas de visualización
 * Authors:      Ana Cardona, Emiliano Pérez, Luis Anchondo
 * updated:      09/05/2026
 ***************************************************************************************************/
// ============================================================
//  - taskDisplayOLED   renderiza la OLED según el estado actual
//  - taskDisplaySerial imprime tabla de servos por el puerto Serie
// ============================================================

#include "bsp.hpp"


// ============================================================
//  HELPERS DE DIBUJO OLED 
// ============================================================

// Dibuja un título centrado con fuente grande, seguido de una línea
// separadora horizontal. Restaura la fuente pequeña al final.
static void drawTitle(const char* t) {
    oled.setFont(u8g2_font_6x10_tr);
    int tw = oled.getStrWidth(t);
    oled.drawStr((128 - tw) / 2, 10, t);   // centrado horizontalmente
    oled.drawHLine(0, 13, 128); // línea de 128 px de ancho
    oled.setFont(u8g2_font_5x7_tr);
}

// Dibuja un menú vertical de opciones con resaltado de la opción seleccionada
// El cursor resaltado usa fondo blanco con texto negro (color invertido).
//   labels  -> array de punteros a strings de las opciones
//   count -> número de opciones
//   cursor -> índice de la opción resaltada
//   yStart -> coordenada Y del texto de la primera opción
//   lineH  -> altura en píxeles de cada línea
static void drawMenu(const char* const* labels, uint8_t count,
                     uint8_t cursor, uint8_t yStart, uint8_t lineH) {
    for (uint8_t i = 0; i < count; i++) {
        int y = yStart + i * lineH;
        if (i == cursor) {
            // Opción seleccionada: fondo blanco + texto negro + flecha »
            oled.setDrawColor(1);
            oled.drawBox(0, y - lineH + 2, 128, lineH);  // recuadro de selección
            oled.setDrawColor(0);  // texto en negro sobre fondo blanco
            oled.drawStr(2,  y, "\xbb"); // carácter » (U+00BB)
            oled.drawStr(12, y, labels[i]);
            oled.setDrawColor(1);
        } else {
            oled.drawStr(12, y, labels[i]);
        }
    }
}

// Dibuja el hint [ESC=back] en la esquina inferior derecha con fuente 4x6.
static void drawEscHint() {
    oled.setFont(u8g2_font_4x6_tr);
    oled.drawStr(86, 64, "[ESC=back]");
    oled.setFont(u8g2_font_5x7_tr);
}

// Pantalla de servo compartida por LIVE_CONTROL y RECORD_CAPTURE.
// Muestra los 6 servos con barra de progreso y servo activo resaltado.
//   header -> texto del encabezado ("LIVE" o "REC")
//   pos[] -> posiciones actuales en µs
//   active -> índice del servo activo
//   xboxOk -> true si el mando está conectado
//   recStep -> número de poses grabadas (solo en modo REC)
//   isRecord -> true para mostrar contador de poses y hint de grabación
static void drawServoScreen(const char* header, uint16_t pos[NUM_SERVOS],
                             uint8_t active, bool xboxOk,
                             uint16_t recStep, bool isRecord) {
    oled.setFont(u8g2_font_5x7_tr);
    oled.drawStr(0, 7, header);   // modo en la esquina superior izquierda

    // En modo grabación, mostrar el contador de poses en el centro del header.
    if (isRecord) {
        char buf[12];
        snprintf(buf, sizeof(buf), "P:%u/%u", recStep, MAX_STEPS_PER_SEQ);
        int tw = oled.getStrWidth(buf);
        oled.drawStr(64 - tw / 2, 7, buf);
    }
    oled.drawStr(98, 7, xboxOk ? "XBX:OK" : "XBX:--");   // estado del mando
    oled.drawHLine(0, 9, 128);   // línea separadora

    // Dibujar una fila por cada servo.
    for (int i = 0; i < NUM_SERVOS; i++) {
        float deg = usToDeg(pos[i]);   // convertir µs → grados para display
        // Porcentaje de posición dentro del rango útil del servo (para la barra).
        float pct = (float)(pos[i] - SERVO_CFG[i].minUs)
                  / (float)(SERVO_CFG[i].maxUs - SERVO_CFG[i].minUs);
        int  y   = 18 + i * 9;   // coordenada Y de esta fila (9 px por servo)
        bool sel = (i == active); // ¿es el servo activo?

        // Servo activo: fondo blanco + texto negro.
        if (sel) {
            oled.setDrawColor(1);
            oled.drawBox(0, y - 7, 96, 8);
            oled.setDrawColor(0);
        } else {
            oled.setDrawColor(1);
        }

        char buf[12];
        oled.drawStr(0, y, sel ? ">" : " ");    // indicador de selección
        snprintf(buf, sizeof(buf), "S%u", i + 1);
        oled.drawStr(6, y, buf);                 // número de servo
        oled.drawStr(18, y, SERVO_CFG[i].name); // nombre de la articulación
        snprintf(buf, sizeof(buf), "%3.0f", deg);
        oled.drawStr(50, y, buf);                // valor en grados (3 cifras)
        oled.drawGlyph(65, y, 0xB0);             // símbolo de grado °

        // Barra de progreso de posición (22 px de ancho, 5 px de alto).
        const int bx = 73, bw = 22, bh = 5, by = y - 5;
        oled.drawFrame(bx, by, bw, bh);         // marco exterior
        int filled = (int)(pct * (bw - 2));
        if (filled > 0) oled.drawBox(bx + 1, by + 1, filled, bh - 2); // relleno

        oled.setDrawColor(1);
    }

    // Hint en la parte inferior: instrucciones contextuales.
    oled.setFont(u8g2_font_4x6_tr);
    oled.drawStr(isRecord ? 56 : 98, 64,
                 isRecord ? "SEL=grabar ESC=guardar" : "[ESC=menu]");
    oled.setFont(u8g2_font_5x7_tr);
}

// Dibuja un submenú de 6 slots (para Record y Play).
// Muestra título, lista de slots con el conteo de poses si showStepCount=true,
// y el hint ESC.
static void drawSubMenu(const char* title, uint8_t cursor,
                        bool showStepCount) {
    drawTitle(title);
    // Arrays estáticos para evitar asignación dinámica en cada frame.
    static char   labels[NUM_SEQUENCES][20];
    static const char* ptrs[NUM_SEQUENCES];
    for (int i = 0; i < NUM_SEQUENCES; i++) {
        if (showStepCount) {
            // Consultar NVS para mostrar cuántas poses tiene cada slot.
            uint16_t cnt = nvsReadCount(i);
            if (cnt > 0)
                snprintf(labels[i], sizeof(labels[i]), "Seq %u  [%u poses]", i+1, cnt);
            else
                snprintf(labels[i], sizeof(labels[i]), "Seq %u  [vacia]", i+1);
        } else {
            snprintf(labels[i], sizeof(labels[i]), "Sequence %u", i+1);
        }
        ptrs[i] = labels[i];
    }
    drawMenu(ptrs, NUM_SEQUENCES, cursor, 22, 7);
    drawEscHint();
}

// ============================================================
//  TASK: DisplayOLED  (prioridad 2)
//  Sección de pantallas:
//    STATE_SPLASH  -> logo de inicio (mostrado al inicio)
//    STATE_MAIN_MENU  -> lista de opciones del menú principal
//    STATE_LIVE_CONTROL -> posición de los 6 servos en tiempo real
//    STATE_RECORD_MENU  -> submenú de slots con conteo de poses
//    STATE_RECORD_CAPTURE -> igual que LIVE pero con contador de poses
//    STATE_PLAY_MENU -> submenú de slots para reproducción
//    STATE_PLAY_RUNNING -> slot y paso actual de reproducción
//    STATE_HELP -> pantalla de ayuda (placeholder)
// ============================================================

void taskDisplayOLED(void *) {

    oled.begin();   // inicializar driver U8g2 y pantalla

    // Splash de arranque 
    oled.clearBuffer();
    oled.setFont(u8g2_font_7x14B_tr);
    int tw = oled.getStrWidth("JaibArm");
    oled.drawStr((128 - tw) / 2, 28, "JaibArm");   // título grande centrado
    oled.setFont(u8g2_font_5x7_tr);
    tw = oled.getStrWidth("v7.0 - RTOS");
    oled.drawStr((128 - tw) / 2, 42, "v6.0 - RTOS");   // subtítulo
    tw = oled.getStrWidth("ESP32 + BT");
    oled.drawStr((128 - tw) / 2, 54, "ESP32 + BT");    // plataforma
    oled.sendBuffer();
    vTaskDelay(pdMS_TO_TICKS(1800));   // mostrar splash 1.8 segundos

    // Transicionar al menú principal tras el splash.
    appState = STATE_MAIN_MENU;
    notifyUI();

    uint16_t   pos[NUM_SERVOS];
    uint8_t    active;
    TickType_t lastRender = xTaskGetTickCount();


    while (true) {
        // Esperar notificación o timeout de 80 ms.
        xSemaphoreTake(xUiSem, pdMS_TO_TICKS(80));

        // Rate-limit: no renderizar si el último frame fue hace menos de 40 ms.
        TickType_t now = xTaskGetTickCount();
        if ((now - lastRender) < pdMS_TO_TICKS(40)) continue;
        lastRender = now;

        AppState st      = appState;
        uint8_t  mCursor = menuCursor;
        uint8_t  sCursor = subCursor;
        bool     xboxOk  = xboxCtrl.isConnected();

        oled.clearBuffer();   // limpiar buffer interno antes de dibujar

        // Seleccionar la pantalla según el estado actual.
        switch (st) {

            //  Menú principal 
            case STATE_MAIN_MENU: {
                drawTitle("-- Main Menu --");
                drawMenu(MAIN_MENU_LABELS, MAIN_MENU_ITEMS, mCursor, 24, 12);
                break;
            }

            // Live Control: posición en tiempo real 
            case STATE_LIVE_CONTROL: {
                getSnapshot(pos, active);
                drawServoScreen("LIVE", pos, active, xboxOk, 0, false);
                break;
            }

            // Record Menu: selección de slot 
            case STATE_RECORD_MENU: {
                // showStepCount=true muestra cuántas poses tiene cada slot.
                drawSubMenu("-- Record Seq. --", sCursor, true);
                break;
            }

            // Record Capture: pantalla igual que Live 
            case STATE_RECORD_CAPTURE: {
                getSnapshot(pos, active);
                drawServoScreen("REC", pos, active, xboxOk,
                                recStepDisplay, true);
                break;
            }

            // Play Menu: selección de slot 
            case STATE_PLAY_MENU: {
                drawSubMenu("-- Play Seq. --", sCursor, true);
                break;
            }

            // Play Running: slot y paso actuales 
            case STATE_PLAY_RUNNING: {
                drawTitle("-- Playing --");
                oled.setFont(u8g2_font_6x10_tr);
                char buf[24];
                snprintf(buf, sizeof(buf), "Seq: %u / %u poses",
                         playSeqIdx + 1, seqBufLen);
                tw = oled.getStrWidth(buf);
                oled.drawStr((128 - tw) / 2, 30, buf);   // centrado
                snprintf(buf, sizeof(buf), "Step: %u", playStepIdx + 1);
                tw = oled.getStrWidth(buf);
                oled.drawStr((128 - tw) / 2, 46, buf);
                oled.setFont(u8g2_font_4x6_tr);
                oled.drawStr(86, 64, "[ESC=stop]");
                oled.setFont(u8g2_font_5x7_tr);
                break;
            }

            // Help: pantalla de ayuda (placeholder) 
            case STATE_HELP: {
                drawTitle("-- Help --");
                oled.setFont(u8g2_font_10x20_tr);
                tw = oled.getStrWidth("xd");
                oled.drawStr((128 - tw) / 2, 44, "xd");   // TODO: agregar instrucciones reales
                drawEscHint();
                oled.setFont(u8g2_font_5x7_tr);
                break;
            }

            default: break;
        }

        oled.sendBuffer();   // transferir buffer a la pantalla vía I2C
    }
}

// ============================================================
//  TASK: DisplaySerial  (prioridad 1)
//  En modos LIVE y REC muestra una tabla ASCII con la posición
//  de cada servo (µs, grados y barra de progreso).
//  En modo PLAY muestra slot y paso actual.
//  En otros estados muestra el nombre del estado y los cursores.
// ============================================================

void taskDisplaySerial(void *) {

    uint16_t   pos[NUM_SERVOS];
    uint8_t    active;
    TickType_t lastWake = xTaskGetTickCount();

    while (true) {
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(500));

        AppState st = appState;

        if (st == STATE_LIVE_CONTROL || st == STATE_RECORD_CAPTURE) {
            getSnapshot(pos, active);
            const char* mode = (st == STATE_LIVE_CONTROL) ? "LIVE" : "REC ";
            Serial.println();
            Serial.printf("  [%s] Xbox:%s\n", mode,
                          xboxCtrl.isConnected() ? "OK" : "--");
            Serial.println("  ┌────┬──────────┬───────┬───────┬──────────────────────┐");
            Serial.println("  │ #  │ Artículo │  us   │  deg  │ Posición             │");
            Serial.println("  ├────┼──────────┼───────┼───────┼──────────────────────┤");
            for (int i = 0; i < NUM_SERVOS; i++) {
                float   deg    = usToDeg(pos[i]);
                uint8_t filled = (uint8_t)(deg / 180.0f * 20.0f);
                char bar[21];
                for (int j = 0; j < 20; j++) bar[j] = (j < filled) ? '#' : '-';
                bar[20] = '\0';
                char line[80];
                snprintf(line, sizeof(line),
                         "  |%sS%u | %-8s | %5u | %5.1f | [%s] |",
                         (i == active) ? ">" : " ",
                         i + 1, SERVO_CFG[i].name, pos[i], deg, bar);
                Serial.println(line);
            }
            Serial.println("  └────┴──────────┴───────┴───────┴──────────────────────┘");
            if (st == STATE_RECORD_CAPTURE)
                Serial.printf("  Poses grabadas: %u / %u  (SEL=grabar, ESC=guardar)\n",
                              (unsigned)recStepDisplay, MAX_STEPS_PER_SEQ);

        } else if (st == STATE_PLAY_RUNNING) {
            Serial.printf("  [PLAY] Seq:%u  Step:%u/%u\n",
                          playSeqIdx + 1, playStepIdx + 1, seqBufLen);
        } else {
            const char* names[] = { "?","MAIN","LIVE","REC_MENU",
                                    "REC_CAP","PLAY_MENU","PLAY_RUN","HELP" };
            Serial.printf("  [STATE] %s  cur=%u sub=%u\n",
                          (st < 8) ? names[st] : "?", menuCursor, subCursor);
        }
    }
}