#include <Arduino.h>
#include <ESP32Servo.h>
#include <PS4Controller.h>
#include <Preferences.h>

// Librerías para la pantalla OLED
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Configuración de la pantalla OLED 
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#define DELAY_ACELEROMETRO 

// Definición de los índices para cada servomotor
#define servo1 0
#define servo2 1
#define servo3 2
#define servo4 3
#define servo5 4
#define servo6 5

Servo servos[6];

// Creación de objeto para manejar la memoria interna (NVS)
Preferences memoriaNVS;

// Estructura para enviar los ángulos a través de la cola de FreeRTOS
struct MensajeAngulos {
  float objetivos[6];
};

// Herramientas de comunicación entre tareas 
QueueHandle_t colaServos;
QueueHandle_t colaSecuencias;
SemaphoreHandle_t semaforoMsg;

// Arreglos para guardar la posición de los motores
float angulosActuales[6];
float angulosObjetivo[6];
float angulosInicio[6];

// Estructura para guardar las secuencias de movimiento
typedef struct {
  float inicio[6];
  float paso1[6];
  float paso2[6];
  float paso3[6];
  float paso4[6];
  float final[6];
} Secuencias_t;

Secuencias_t angulosSecuencia[3];

// Variables de estado del programa
int mode = 0;
int servoSeleccionado = 0;
int secuenciaSeleccionada = 0;
int contadorPasos = 0;
bool brazoEnPosicion;
bool bandera = false; // Bandera para iniciar la secuencia sin necesidad de presionar el botón (para pruebas)
int estadoSecuencia;

// Variables para detectar si un botón acaba de ser presionado
bool estadoAnteriorTriangulo = false;
bool estadoAnteriorOptions = false;
bool estadoAnteriorCuadrado = false;
bool estadoAnteriorUp = false;
bool estadoAnteriorleft = false;
bool estadoAnteriorDown = false;
bool estadoAnteriorRight = false;
bool estadoAnteriorL3 = false;
bool estadoAnteriorR3 = false;
// Variables para el control de mensajes en la pantalla OLED
bool mostrarMensajeGuardado = false;
unsigned long tiempoMensaje = 0;
int tipoMensajeOLED = 0; 

// Variables para ajustar la velocidad del brazo
float velocidadJoystick = 2.0; 
float pasoSuavizado = 1.0;   

/**********************************************************************************
 * ********************************** FUNCIONES PRINCIPALES ***********************
 **********************************************************************************/

// Función que realiza el cambio de modo (0, 1 o 2)
void cambio_de_modo() {
  mode = (mode + 1) % 3;
  Serial.printf("Modo cambiado a: %d\n", mode);
}

// Función que mueve los servomotores poco a poco para evitar movimientos bruscos
void moverServosSuavemente() {
  bool posicionCompleta = true;
  
  for (int i = 0; i < 6; i++) {
    float error = angulosObjetivo[i] - angulosActuales[i];
    
    // Si el motor aún no llega a su destino
    if (abs(error) > pasoSuavizado) {
      posicionCompleta = false;
      if (error > 0) {
        angulosActuales[i] += pasoSuavizado;
      } 
      else {
        angulosActuales[i] -= pasoSuavizado;
      }
    } 
    else {
      // El motor ya llegó a su objetivo
      angulosActuales[i] = angulosObjetivo[i];
    }

    // Escribimos el nuevo ángulo en el servo
    servos[i].write(angulosActuales[i]);
  }
  
  // Actualizamos la bandera para saber si todo el brazo ya se detuvo
  brazoEnPosicion = posicionCompleta;
}

/**********************************************************************************
 * ********************************** TAREAS DE FREERTOS **************************
 **********************************************************************************/

// Tarea exclusiva para mover físicamente los servomotores
void servomotores_task(void *parameter) {
  MensajeAngulos msjRecibido;
  
  while (true) {
    // Revisamos si hay nuevos ángulos en la cola
    if (xQueueReceive(colaServos, &msjRecibido, 0) == pdTRUE) {
      for (int i = 0; i < 6; i++) {
        angulosObjetivo[i] = msjRecibido.objetivos[i];
      }
    }

    // Movemos los motores un paso a la vez
    moverServosSuavemente();
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

// Tarea exclusiva para dibujar y actualizar la pantalla OLED
void oled_task(void *parameter) {
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Fallo al iniciar SSD1306 OLED"));
    vTaskDelete(NULL); 
  }
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  while (true) {
    display.clearDisplay();
    
    // Revisamos si alguien activó el semáforo para mostrar un mensaje emergente
    if (xSemaphoreTake(semaforoMsg, 0) == pdTRUE) {
      mostrarMensajeGuardado = true;
      tiempoMensaje = millis();
    }

    // Lógica para mostrar los mensajes emergentes
    if (mostrarMensajeGuardado) {
      display.setCursor(10, 20);
      
      // Evaluamos qué tipo de mensaje mostrar según la variable global
      if (tipoMensajeOLED == 0) {
        display.setTextSize(2); 
        display.println("POSICION");
        display.setCursor(20, 40);
        display.println("GUARDADA!");
      } 
      else if (tipoMensajeOLED == 1) {
        display.setTextSize(1); 
        display.println("Posicion inicial");
        display.setCursor(10, 35);
        display.println("establecida");
      } 
      else if (tipoMensajeOLED == 2) {
        display.setTextSize(1); 
        display.println("Posicion Paso 1");
        display.setCursor(10, 35);
        display.println("establecida");
      }

      else if (tipoMensajeOLED == 3) {
        display.setTextSize(1); 
        display.println("Posicion Paso 2");
        display.setCursor(10, 35);
        display.println("establecida");
      }

      else if (tipoMensajeOLED == 4) {
        display.setTextSize(1); 
        display.println("Posicion Paso 3");
        display.setCursor(10, 35);
        display.println("establecida");
      }

      else if (tipoMensajeOLED == 5) {
        display.setTextSize(1); 
        display.println("Posicion Paso 4");
        display.setCursor(10, 35);
        display.println("establecida");
      }

      else if (tipoMensajeOLED == 6) {
        display.setTextSize(1); 
        display.println("Posicion Final");
        display.setCursor(10, 35);
        display.println("establecida");
      }

      // El mensaje desaparece después de 2 segundos 
      if (millis() - tiempoMensaje > 2000) {
        mostrarMensajeGuardado = false;
      }
    }

    // Interfaz para el Modo 2 (Menú de secuencias)
    else if (mode == 2) {
      static int Selection = 0; 

      display.setTextSize(1); 
      display.setCursor(0, 0);
      display.printf("MENU |  MODO: %d ", mode);
      display.drawLine(0, 12, 128, 12, SSD1306_WHITE);

      // Leemos la cola para saber qué secuencia está seleccionada
      if (xQueueReceive(colaSecuencias, &Selection, 0) == pdTRUE) {
      }

      // Dibujamos las 3 opciones en la pantalla
      for(int i=0; i<3; i++) {
        int y= 15 + (i*15);
        if(i == Selection) {
          display.setCursor(0, y);
          display.print(">");
        }
        display.setCursor(15, y);
        if(i == 0) display.print("Secuencia 1");
        if(i == 1) display.print("Secuencia 2");
        if(i == 2) display.print("Secuencia 3");
      } 
    }

    // Interfaz normal para los Modos 0 y 1 (Mostrar ángulos)
    else {
      display.setTextSize(1); 
      display.setCursor(0, 0);
      display.printf("MODO: %d | Activo: S%d  | Sec: %d", mode, servoSeleccionado + 1, secuenciaSeleccionada + 1);
      display.drawLine(0, 17, 128, 17, SSD1306_WHITE);

      display.setCursor(0, 20);
      display.printf("S1:%4.0f", angulosActuales[0]);
      display.setCursor(64, 20);
      display.printf("S2:%4.0f", angulosActuales[1]);

      display.setCursor(0, 35);
      display.printf("S3:%4.0f", angulosActuales[2]);
      display.setCursor(64, 35);
      display.printf("S4:%4.0f", angulosActuales[3]);

      display.setCursor(0, 50);
      display.printf("S5:%4.0f", angulosActuales[4]);
      display.setCursor(64, 50);
      display.printf("S6:%4.0f", angulosActuales[5]);
    }

    display.display(); 
    vTaskDelay(100 / portTICK_PERIOD_MS); 
  }
}

// Tarea principal: Lectura del controlador PS4 y cálculo de ángulos
void read_ps4(void *parameter) {
  MensajeAngulos msjAEnviar;
  bool estadoActualUp = false;
  bool estadoActualleft = false;
  bool estadoActualDown = false;
  bool estadoActualRight = false;
  bool estadoActualL3 = false;
  bool estadoActualR3 = false;
  unsigned long tiempoSecuencia = 0;
  bool ejecutandoSecuencia = false;

  while (true) {
    if (PS4.isConnected()) {
      
      // Leemos el estado actual de los botones direccionales y L3
      estadoActualUp= PS4.Up();
      estadoActualleft = PS4.Left();
      estadoActualDown = PS4.Down();  
      estadoActualRight = PS4.Right();
      estadoActualR3 = PS4.R3();
      estadoActualL3 = PS4.L3();

      // ********************* LÓGICA DEL MODO 2 (REPRODUCIR SECUENCIAS) *********************
      if(mode == 2) {
        
        // Cambio de secuencia con los botones Arriba/Abajo
        if (estadoActualUp && !estadoAnteriorUp) {
          secuenciaSeleccionada--;
          if (secuenciaSeleccionada < 0) secuenciaSeleccionada = 2;
          xQueueSend(colaSecuencias, &secuenciaSeleccionada, 0); 
        }

        if (estadoActualDown && !estadoAnteriorDown) {
          secuenciaSeleccionada++;
          if (secuenciaSeleccionada > 2) secuenciaSeleccionada = 0;
          xQueueSend(colaSecuencias, &secuenciaSeleccionada, 0); 
        }
        

        if(estadoActualR3 && !estadoAnteriorR3) {
          estadoSecuencia=0; // Reiniciamos la secuencia para que pueda volver a empezar desde el principio
          bandera = false; // Reiniciamos la bandera para permitir iniciar la secuencia desde el principio
        }

        // INICIAR LA SECUENCIA con el botón Izquierdo
        if ((estadoActualleft && !estadoAnteriorleft)|| bandera) {
          if (estadoSecuencia == 0 && !bandera) { // Solo cargamos la secuencia desde la NVS la primera vez que se inicia (o si se reinicia el proceso)
            String llave = "sec" + String(secuenciaSeleccionada);
            memoriaNVS.getBytes(
              llave.c_str(), 
              &angulosSecuencia[secuenciaSeleccionada], 
              sizeof(Secuencias_t)
            );
            bandera = true; // Activamos la bandera para permitir que la secuencia continúe sin necesidad de seguir presionando el botón
          }

          if(estadoSecuencia == 0 && brazoEnPosicion == true) { // Solo se puede iniciar una secuencia si no hay otra en curso
            for (int i = 0; i < 6; i++) {
              angulosObjetivo[i] = angulosSecuencia[secuenciaSeleccionada].inicio[i];
            }
            estadoSecuencia = 1; // Fase 1: Yendo a Posición Inicial
            brazoEnPosicion = false; 
          }

          else if(estadoSecuencia == 1 && brazoEnPosicion == true) {
            for (int i = 0; i < 6; i++) {
              angulosObjetivo[i] = angulosSecuencia[secuenciaSeleccionada].paso1[i];
            }
            estadoSecuencia = 2; // Fase 2: Yendo a Posición Paso 1
            brazoEnPosicion = false;
          }

          else if(estadoSecuencia == 2 && brazoEnPosicion == true) {
            for (int i = 0; i < 6; i++) {
              angulosObjetivo[i] = angulosSecuencia[secuenciaSeleccionada].paso2[i];
            }
            estadoSecuencia = 3; // Fase 3: Yendo a Posición Paso 2
            brazoEnPosicion = false;
          }
  
          else if(estadoSecuencia == 3 && brazoEnPosicion == true) {
            for (int i = 0; i < 6; i++) {
              angulosObjetivo[i] = angulosSecuencia[secuenciaSeleccionada].paso3[i];
            }
            estadoSecuencia = 4; // Fase 4: Yendo a Posición Paso 3
            brazoEnPosicion = false;
          }
          
          else if(estadoSecuencia == 4 && brazoEnPosicion == true) {
            for (int i = 0; i < 6; i++) {
              angulosObjetivo[i] = angulosSecuencia[secuenciaSeleccionada].paso4[i];
            }
            estadoSecuencia = 5; // Fase 5: Yendo a Posición Paso 4
            brazoEnPosicion = false;
          }

          else if(estadoSecuencia == 5 && brazoEnPosicion == true) {
            for (int i = 0; i < 6; i++) {
              angulosObjetivo[i] = angulosSecuencia[secuenciaSeleccionada].final[i];
            }
            estadoSecuencia = 6; // Fase 6: Yendo a Posición Final
            brazoEnPosicion = false;
          }     

          else if (estadoSecuencia == 6 && brazoEnPosicion == true) {
            // La secuencia terminó completamente
            estadoSecuencia = 0; 
            brazoEnPosicion= false; // Reiniciamos la bandera para permitir iniciar la secuencia desde el principio

          }
          vTaskDelay(200 / portTICK_PERIOD_MS); 
        } 
      }

      // ********************* LÓGICA DE LOS MODOS 0 Y 1 **************************
      else{
      
        // Guardar posición inicial de la secuencia (Botón Arriba)
        if (estadoActualUp && !estadoAnteriorUp) {
          switch (contadorPasos)
          {
          case 0:
            for(int i=0; i<6; i++) {
              angulosSecuencia[secuenciaSeleccionada].inicio[i] = angulosActuales[i];
            }
            tipoMensajeOLED = 1; 
            xSemaphoreGive(semaforoMsg); 
            contadorPasos++;
            break;
          case 1:
            for(int i=0; i<6; i++) {
              angulosSecuencia[secuenciaSeleccionada].paso1[i] = angulosActuales[i];
            }
            tipoMensajeOLED = 2;
            xSemaphoreGive(semaforoMsg);
            contadorPasos++;
            break;
          case 2:
            for(int i=0; i<6; i++) {
              angulosSecuencia[secuenciaSeleccionada].paso2[i] = angulosActuales[i];
            }
            tipoMensajeOLED = 3;
            xSemaphoreGive(semaforoMsg);
            contadorPasos++;
             break;
          case 3:
            for(int i=0; i<6; i++) {
              angulosSecuencia[secuenciaSeleccionada].paso3[i] = angulosActuales[i];
            }
            tipoMensajeOLED = 4;
            xSemaphoreGive(semaforoMsg);
            contadorPasos++;
             break;

          case 4:
            for(int i=0; i<6; i++) {
              angulosSecuencia[secuenciaSeleccionada].paso4[i] = angulosActuales[i];
            }
            tipoMensajeOLED = 5;
            xSemaphoreGive(semaforoMsg);
            contadorPasos++;
             break;
          
          case 5:
            for(int i=0; i<6; i++) {
              angulosSecuencia[secuenciaSeleccionada].final[i] = angulosActuales[i];
            }
            tipoMensajeOLED = 6;
            xSemaphoreGive(semaforoMsg);
            contadorPasos = 0;
            break;
          
          
            default:  
            break;
          }
        }

        // Guardar las secuencias en la memoria permanente NVS (Botón L3)
        if (estadoActualL3 && !estadoAnteriorL3) {
          String llave = "sec" + String(secuenciaSeleccionada);

          // Guardamos TODA la estructura de golpe
          memoriaNVS.putBytes(
          llave.c_str(), 
          &angulosSecuencia[secuenciaSeleccionada], 
          sizeof(Secuencias_t)
          );
          
          tipoMensajeOLED = 0; 
          xSemaphoreGive(semaforoMsg);
        }

        // Cambiar de secuencia a guardar (Botón Derecha)
        if (estadoActualRight && !estadoAnteriorRight) {
          secuenciaSeleccionada = (secuenciaSeleccionada + 1) % 3;
        }
      }
      
      // Actualizamos los estados anteriores de los botones
      estadoAnteriorUp = estadoActualUp;
      estadoAnteriorL3 = estadoActualL3;
      estadoAnteriorDown = estadoActualDown;
      estadoAnteriorRight = estadoActualRight;
      estadoAnteriorR3 = estadoActualR3;
      estadoAnteriorleft = estadoActualleft;

      // Botón Options: Cambia entre Modo 0, 1 y 2
      bool estadoActualOptions = PS4.Options();
      if (estadoActualOptions && !estadoAnteriorOptions) {
        cambio_de_modo();
      }
      estadoAnteriorOptions = estadoActualOptions;

      // Botón Cuadrado: Guarda la posición actual como el centro (90°) en la NVS
      bool estadoActualCuadrado = PS4.Square();
      if (estadoActualCuadrado && !estadoAnteriorCuadrado) {
        Serial.println("Guardando nueva posicion inicial en memoria NVS...");
        for (int i = 0; i < 6; i++) {
          angulosInicio[i] = angulosObjetivo[i]; 
          String llave = "s" + String(i); 
          memoriaNVS.putFloat(llave.c_str(), angulosInicio[i]);
        }
        Serial.println("¡Posición guardada exitosamente!");
        tipoMensajeOLED = 0; 
        xSemaphoreGive(semaforoMsg);
      }
      estadoAnteriorCuadrado = estadoActualCuadrado;

      // Botón Círculo: Regresa todos los motores a su posición de inicio guardada
      if (PS4.Circle()) {
        for (int i = 0; i < 6; i++) {
          angulosObjetivo[i] = angulosInicio[i];
        }
      }

      // ************************** CONTROL MANUAL DE MOTORES*******************************
      switch (mode) {

        // MODO 0: Mover un servo a la vez
        case 0: { 
          // Botón Triángulo: Cambia el servo activo
          bool estadoActualTriangulo = PS4.Triangle();
          if (estadoActualTriangulo && !estadoAnteriorTriangulo) {
            servoSeleccionado = (servoSeleccionado + 1) % 6;
          }
          estadoAnteriorTriangulo = estadoActualTriangulo;

          // Movimiento con el joystick izquierdo (Eje Y)
          int valorJoystickY = PS4.LStickY(); 
          if (abs(valorJoystickY) > 15) {
            float incremento = (valorJoystickY / 127.0) * velocidadJoystick;
            angulosObjetivo[servoSeleccionado] += incremento;

            // Límites mecánicos (Servo 6 tiene un rango especial)
            if (servoSeleccionado == servo6) {
              if (angulosObjetivo[servo6] > 160.0) angulosObjetivo[servo6] = 160.0;
              if (angulosObjetivo[servo6] < 70.0) angulosObjetivo[servo6] = 70.0;
             }
            else {
              if (angulosObjetivo[servoSeleccionado] > 180.0) angulosObjetivo[servoSeleccionado] = 180.0;
              if (angulosObjetivo[servoSeleccionado] < 0.0) angulosObjetivo[servoSeleccionado] = 0.0;
             }
          }
          break;
        }

        // MODO 1: Control individual simultáneo de todos los motores
        case 1: { 
          // SERVO 1: Joystick Derecho (Eje X)
          int valorJoystickIzqX = PS4.LStickX();
          if (abs(valorJoystickIzqX) > 15) {
            float incremento = (valorJoystickIzqX / 127.0) * velocidadJoystick;
            angulosObjetivo[servo1] += incremento;
            if (angulosObjetivo[servo1] > 180.0) angulosObjetivo[servo1] = 180.0;
            if (angulosObjetivo[servo1] < 0.0) angulosObjetivo[servo1] = 0.0;
          }

          // SERVO 2: Joystick Izquierdo (Eje Y)
          int valorJoystickIzqY = PS4.LStickY(); 
          if (abs(valorJoystickIzqY) > 15) {
            float incremento = (valorJoystickIzqY / 127.0) * velocidadJoystick;
            angulosObjetivo[servo2] += incremento;
            if (angulosObjetivo[servo2] > 180.0) angulosObjetivo[servo2] = 180.0;
            if (angulosObjetivo[servo2] < 0.0) angulosObjetivo[servo2] = 0.0;
          }

          // SERVO 3: Joystick Derecho (Eje Y)
          int  valorJoystickDerY = PS4.RStickY(); 
          if (abs(valorJoystickDerY) > 15) {
            float incremento = (valorJoystickDerY / 127.0) * velocidadJoystick;
            angulosObjetivo[servo3] += incremento;
            if (angulosObjetivo[servo3] > 180.0) angulosObjetivo[servo3] = 180.0;
            if (angulosObjetivo[servo3] < 0.0) angulosObjetivo[servo3] = 0.0;
          }

          // SERVO 4: Botones Triángulo y X 
          if (PS4.Triangle()) {
            angulosObjetivo[servo4] += velocidadJoystick;
            if (angulosObjetivo[servo4] > 180.0) angulosObjetivo[servo4] = 180.0;
          } 
          else if (PS4.Cross()) { 
            angulosObjetivo[servo4] -= velocidadJoystick;
            if (angulosObjetivo[servo4] < 0.0) angulosObjetivo[servo4] = 0.0;
          }
        
          // SERVO 5: Acelerómetro (Inclinación del control)
          int inclinacion = PS4.data.sensor.accelerometer.x; 
          int anguloCalculado;
          if(abs(inclinacion) < 800) {
            anguloCalculado = 90; // Zona muerta para no registrar temblores ligeros
          }
         else{
          anguloCalculado = map(inclinacion, -8000, 8000, 0, 180);
         }
          angulosObjetivo[servo5] = constrain(anguloCalculado, 0, 180);

          // SERVO 6: Gatillos L2 y R2
          int gatilloR2 = PS4.R2Value(); 
          int gatilloL2 = PS4.L2Value(); 

          if (gatilloR2 > 10) {
            float incremento = (gatilloR2 / 255.0) * velocidadJoystick;
            angulosObjetivo[servo6] += incremento;
            if (angulosObjetivo[servo6] > 160.0) angulosObjetivo[servo6] = 160.0;
          } 
          else if (gatilloL2 > 10) {
            float incremento = (gatilloL2 / 255.0) * velocidadJoystick;
            angulosObjetivo[servo6] -= incremento;
            if (angulosObjetivo[servo6] < 70.0) angulosObjetivo[servo6] = 70.0;
          }

          break;
        }
      }
      
      // Enviamos el cálculo final de los 6 motores a la cola para que la otra tarea los mueva
      for (int i = 0; i < 6; i++) {
        msjAEnviar.objetivos[i] = angulosObjetivo[i];
      }
      xQueueSend(colaServos, &msjAEnviar, 0);
    }

    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  
  Wire.begin(); 
  
  // Abre la partición de memoria no volátil
  memoriaNVS.begin("brazo_config", false); 
  
  // Carga los valores iniciales guardados o pone 90.0 por defecto
  for (int i = 0; i < 6; i++) {
    String llave = "s" + String(i);
    angulosInicio[i] = memoriaNVS.getFloat(llave.c_str(), 90.0);
    
    angulosActuales[i] = angulosInicio[i];
    angulosObjetivo[i] = angulosInicio[i];
  }
  
  // Inicialización del control PS4 con tu dirección MAC
  PS4.begin("94:BB:43:56:C9:07");

  // Asignación de pines a cada servo
  servos[0].attach(4);
  servos[1].attach(5);
  servos[2].attach(16);
  servos[3].attach(23);
  servos[4].attach(18);
  servos[5].attach(19);

  // Inicialización de colas y semáforos para FreeRTOS
  colaServos = xQueueCreate(5, sizeof(MensajeAngulos));
  colaSecuencias = xQueueCreate(5, sizeof(int));
  semaforoMsg = xSemaphoreCreateBinary();

  // Creación de las tareas en los núcleos del ESP32
  xTaskCreatePinnedToCore(read_ps4, "read_ps4", 10000, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(oled_task, "oled_task", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(servomotores_task, "servomotores_task", 2048, NULL, 1, NULL, 1);
}

void loop() {
  // El loop principal no hace nada, todo lo maneja FreeRTOS en sus tareas
  vTaskDelay(portMAX_DELAY);
}