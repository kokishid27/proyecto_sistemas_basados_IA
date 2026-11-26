/****************************************************************************************************
 * FileName:     main.cpp
 * Dependencies: bsp.hpp
 * Processor:    Tensilica Xtensa LX6 160 MHz
 * Board:        ESP32 DevKitC Wroom 32
 * Company:      TecNM /IT Chihuahua
 * Description:  Laboratorio control de brazo robótico con uso de FreeRTOS y memoria NVS
 * Authors:      Ana Cardona, Emiliano Pérez, Luis Anchondo
 * Updated:      05/2026
 * Created on:   30 Mar. 2026
 * updated:      09/05/2026
 ***************************************************************************************************/
#include "bsp.hpp"

// ============================================================
//  SETUP
// ============================================================

void setup() {

    // Puerto serie: 115200 
    Serial.begin(115200);
    delay(300);   // esperar a que el USB-Serial se estabilice
    printf("\n[SYS] JaibArm RTOS v7.0\n");

    // Bus I2C para la pantalla OLED 
    Wire.begin(OLED_SDA, OLED_SCL);

    // Timers de hardware para PWM de servos 
    // ESP32 tiene 4 timers de 32 bits; asignar uno a cada canal PWM.
    // La biblioteca ESP32Servo los necesita preasignados.
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);

    // Inicializar servos 
    // Cada servo arranca en su posición home definida en SERVO_CFG.
    for (int i = 0; i < NUM_SERVOS; i++) {
        servoPos[i] = SERVO_CFG[i].homeUs; // posición inicial en RAM
        servos[i].setPeriodHertz(50); // frecuencia estándar RC 50 Hz
        servos[i].attach(SERVO_PINS[i], SERVO_MIN_US, SERVO_MAX_US);
        servos[i].writeMicroseconds(servoPos[i]); // mover físicamente al home
        printf("[SYS] S%u %s GPIO%u home=%u us\n",
               i + 1, SERVO_CFG[i].name, SERVO_PINS[i], servoPos[i]);
    }
    delay(600); // dar tiempo a los servos de llegar al home antes de crear tareas

    // Verificar espacio disponible en NVS 
    prefs.begin(NVS_NS, true);
    printf("[SYS] NVS free entries: %u\n", prefs.freeEntries());
    prefs.end();

    // Creación de elementos para optimización de procesos
    xMutex    = xSemaphoreCreateMutex();  // protege servoPos[]
    xCmdQueue = xQueueCreate(CMD_QUEUE_SIZE, sizeof(ServoCmd));  // cola de comandos
    xUiSem    = xSemaphoreCreateBinary();  // notificación OLED
    // Verificar que la creación fue exitosa (configASSERT aborta en fallo).
    configASSERT(xMutex && xCmdQueue && xUiSem);

    // Iniciar Bluetooth Low Energy (BLE) 
    xboxCtrl.begin();
    printf("[SYS] BLE iniciado\n");

    // Crear tareas FreeRTOS 
    xTaskCreate(taskButtonScan,    "ButtonScan",    3072, nullptr, 3, nullptr);
    xTaskCreate(taskXboxScan,      "XboxScan",      4096, nullptr, 3, nullptr);
    xTaskCreate(taskServoControl,  "ServoCtrl",     2560, nullptr, 4, nullptr);
    xTaskCreate(taskDisplayOLED,   "DisplayOLED",   4096, nullptr, 2, nullptr);
    xTaskCreate(taskDisplaySerial, "DisplaySerial", 3584, nullptr, 1, nullptr);

    printf("[SYS] Sistema listo. BTN_SEL=%u PLUS=%u MINUS=%u ESC=%u\n",
           BTN_SEL, BTN_PLUS, BTN_MINUS, BTN_ESC);
}

void loop() {}
// Se encargan las tareas