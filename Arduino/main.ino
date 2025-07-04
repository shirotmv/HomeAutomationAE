#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>
#include "DHT.h"

// ENTRADAS (Sensores)
const int pinSwitch = 13;       
const int pinPIRControl = 12;   

bool sistemaActivo = true;
bool pirActivo = true;          

#define DHTPIN 4                
#define DHTTYPE DHT22
const int pir = 3;              
const int sensorCasa = A0;      
const int sensorA0 = A3;        

// SALIDAS (Actuadores)
const int pinLedCasa = 5;       
const int COOLER_PIN = 6;       
const int buzzer = 7;           
const int led = 8;              
const int led1 = 9;

// Configuración CORREGIDA de pines (manteniendo cables actuales)
const int ESP32_RX = 11;  // Pin 11 del Arduino <- GPIO13 del ESP32 (RECIBE datos)
const int ESP32_TX = 10;  // Pin 10 del Arduino -> GPIO15 del ESP32 (ENVÍA datos)         

// Configuración optimizada - TIEMPOS REDUCIDOS
const float TEMP_UMBRAL = 26.0;
const int umbralAlcohol = 60;
const int umbralHumo = 100;
const int umbralCasa = 510;
const unsigned long tiempoDebounce = 150;
const unsigned long noMotionDelay = 5000;
const unsigned long tiempoCalentamiento = 30000;
const unsigned long debounceSwitch = 20;        // MUY RÁPIDO
const unsigned long debouncePIR = 15;           // SÚPER RÁPIDO - PRIORIDAD MÁXIMA
const unsigned long intervaloChequeoWiFi = 15000;
const unsigned long intervaloEnvioDatos = 2500; // ENVÍO CADA 2.5 SEGUNDOS

// Variables de control optimizadas
bool estadoLedCasa = false;
bool estadoLedExterior = false;
bool estadoCooler = false;
bool estadoBuzzer = false;
bool esperandoBajada = false;
bool sistemaEncendido = false;
bool mostroSecuenciaInicio = false;
bool wifiConectado = false;
bool envioAutomaticoHabilitado = false;

// NUEVAS VARIABLES PARA CONTROL MANUAL DE TOGGLES
bool coolerManual = false;      // Control manual del cooler
bool buzzerManual = false;      // Control manual del buzzer
bool ledExteriorManual = false; // Control manual de luces exteriores
bool ledInteriorManual = false; // Control manual de luz interior

// MODOS DE OPERACIÓN (true = automático, false = manual)
bool modoAutoCooler = true;
bool modoAutoBuzzer = true;
bool modoAutoLedExterior = true;
bool modoAutoLedInterior = true;

// Control de switches con prioridad PIR
bool estadoAnteriorSwitch = HIGH;
bool estadoAnteriorPIRControl = HIGH;
unsigned long ultimoCambioSwitch = 0;
unsigned long ultimoCambioPIRControl = 0;

// Contadores y tiempos
unsigned long tiempoUltimoCambio = 0;
unsigned long lastMotionTime = 0;
unsigned long tiempoInicio;
unsigned long ultimoChequeoWiFi = 0;
unsigned long ultimaActualizacionLCD = 0;
unsigned long ultimoReporte = 0;
unsigned long ultimoMensajeApagado = 0;
unsigned long ultimoMensajeCalentamiento = 0;
unsigned long ultimoEnvioDatos = 0; // NUEVO: Control de envío periódico
int motionCount = 0;

// Estados anteriores para detectar cambios
byte estadosAnteriores = 0;

// Variables para eliminar delays en secuencias
bool secuenciaInicioCompletada = false;
bool secuenciaEncendidoCompletada = false;
int pasoSecuenciaInicio = 0;
int pasoSecuenciaEncendido = 0;
unsigned long tiempoInicioSecuencia = 0;

// Objetos
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 20, 4);
SoftwareSerial esp32Serial(ESP32_RX, ESP32_TX);  // (RX=11, TX=10)

// FUNCIÓN CRÍTICA - MÁXIMA PRIORIDAD PARA PIR
bool leerPulsadorPIR() {
  static bool estadoPrevio = HIGH;
  static unsigned long ultimoTiempo = 0;
  
  bool estadoActual = digitalRead(pinPIRControl);
  unsigned long ahora = millis();
  
  // Detección de flanco de bajada con debounce mínimo
  if (estadoActual == LOW && estadoPrevio == HIGH && (ahora - ultimoTiempo) > debouncePIR) {
    estadoPrevio = estadoActual;
    ultimoTiempo = ahora;
    
    // ACCIÓN INMEDIATA SIN DELAYS
    pirActivo = !pirActivo;
    
    // Log inmediato
    Serial.print(F("PIR "));
    Serial.println(pirActivo ? F("ACTIVADO") : F("DESACTIVADO"));
    
    // Si se desactiva PIR, apagar LEDs inmediatamente (solo si está en modo automático)
    if (!pirActivo && modoAutoLedExterior) {
      digitalWrite(led, LOW);
      digitalWrite(led1, LOW);
      estadoLedExterior = false;
    }
    
    return true; // Cambio detectado
  }
  
  // Actualizar estado previo para flancos de subida
  if (estadoActual != estadoPrevio && (ahora - ultimoTiempo) > debouncePIR) {
    estadoPrevio = estadoActual;
    ultimoTiempo = ahora;
  }
  
  return false;
}

bool leerSwitchRapido(int pin, bool &estadoAnterior, unsigned long &ultimoCambio, unsigned long debounceTime) {
  bool estadoActual = digitalRead(pin);
  unsigned long ahora = millis();
  
  if (estadoActual != estadoAnterior && (ahora - ultimoCambio) > debounceTime) {
    estadoAnterior = estadoActual;
    ultimoCambio = ahora;
    return true;
  }
  return false;
}

void verificarWiFi() {
  esp32Serial.println(F("PING_ESP32"));
  
  unsigned long inicio = millis();
  while (millis() - inicio < 500) { // Reducido de 800 a 500
    if (esp32Serial.available()) {
      String respuesta = esp32Serial.readStringUntil('\n');
      respuesta.trim();
      if (respuesta == F("PONG_ESP32") || respuesta == F("ESP32_OK")) {
        wifiConectado = true;
        return;
      }
    }
  }
  wifiConectado = false;
}

  void enviarDatos() {
    if (!wifiConectado) return;
    
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    int lecturaSensor = analogRead(sensorA0);
    int valorCasa = analogRead(sensorCasa);
    int motion = digitalRead(pir);
    
    esp32Serial.print(F("DATA:{\"temp\":"));
    esp32Serial.print(temp);
    esp32Serial.print(F(",\"hum\":"));
    esp32Serial.print(hum);
    esp32Serial.print(F(",\"mq2\":"));
    esp32Serial.print(lecturaSensor);
    esp32Serial.print(F(",\"mic\":"));
    esp32Serial.print(valorCasa);
    esp32Serial.print(F(",\"pir\":"));
    esp32Serial.print(motion);
    esp32Serial.print(F(",\"ledCasa\":"));
    esp32Serial.print(estadoLedCasa ? 1 : 0);
    esp32Serial.print(F(",\"ledExt\":"));
    esp32Serial.print(estadoLedExterior ? 1 : 0);
    esp32Serial.print(F(",\"cooler\":"));
    esp32Serial.print(estadoCooler ? 1 : 0);
    esp32Serial.print(F(",\"buzzer\":"));
    esp32Serial.print(estadoBuzzer ? 1 : 0);
    esp32Serial.print(F(",\"sistema\":"));
    esp32Serial.print(sistemaActivo ? 1 : 0);
    esp32Serial.print(F(",\"pirActivo\":"));
    esp32Serial.print(pirActivo ? 1 : 0);
    esp32Serial.print(F(",\"motions\":"));
    esp32Serial.print(motionCount);
    esp32Serial.print(F(",\"modoCooler\":"));
    esp32Serial.print(modoAutoCooler ? F("\"auto\"") : F("\"manual\""));
    esp32Serial.print(F(",\"modoBuzzer\":"));
    esp32Serial.print(modoAutoBuzzer ? F("\"auto\"") : F("\"manual\""));
    esp32Serial.print(F(",\"modoLedExt\":"));
    esp32Serial.print(modoAutoLedExterior ? F("\"auto\"") : F("\"manual\""));
    esp32Serial.print(F(",\"modoLedInt\":"));
    esp32Serial.print(modoAutoLedInterior ? F("\"auto\"") : F("\"manual\""));
    esp32Serial.println(F("}"));
  }

// NUEVA FUNCIÓN: Envío periódico de datos
void manejarEnvioDatos() {
  unsigned long ahora = millis();
  
  if (wifiConectado && envioAutomaticoHabilitado && (ahora - ultimoEnvioDatos > intervaloEnvioDatos)) {
    enviarDatos();
    ultimoEnvioDatos = ahora;
  }
}

void detectarCambios() {
  if (!wifiConectado || !envioAutomaticoHabilitado) return;
  
  byte estadoActual = 0;
  estadoActual |= (estadoLedCasa ? 1 : 0) << 0;
  estadoActual |= (estadoLedExterior ? 1 : 0) << 1;
  estadoActual |= (estadoCooler ? 1 : 0) << 2;
  estadoActual |= (estadoBuzzer ? 1 : 0) << 3;
  estadoActual |= (sistemaActivo ? 1 : 0) << 4;
  estadoActual |= (pirActivo ? 1 : 0) << 5;
  
  if (estadoActual != estadosAnteriores) {
    esp32Serial.println(F("CHANGE:Estado_Actualizado"));
    enviarDatos();
    estadosAnteriores = estadoActual;
    ultimoEnvioDatos = millis(); // Resetear timer de envío periódico
  }
}

void procesarComandos() {
  if (esp32Serial.available()) {
    String mensaje = esp32Serial.readStringUntil('\n');
    mensaje.trim();
    
    if (mensaje.startsWith(F("ESP32:"))) {
      String comando = mensaje.substring(6);
      
      // TOGGLES PRINCIPALES
      if (comando == F("TOGGLE_SISTEMA")) {
        sistemaActivo = !sistemaActivo;
        esp32Serial.println(sistemaActivo ? F("Sistema ON") : F("Sistema OFF"));
        if (sistemaActivo) {
          sistemaEncendido = false;
          secuenciaEncendidoCompletada = false;
          pasoSecuenciaEncendido = 0;
        }
      }
      else if (comando == F("TOGGLE_PIR")) {
        pirActivo = !pirActivo;
        if (!pirActivo && modoAutoLedExterior) {
          digitalWrite(led, LOW);
          digitalWrite(led1, LOW);
          estadoLedExterior = false;
        }
        esp32Serial.println(pirActivo ? F("PIR ON") : F("PIR OFF"));
      }
      
      // TOGGLE COOLER
      else if (comando == F("TOGGLE_COOLER")) {
        if (modoAutoCooler) {
          // Cambiar a manual y activar/desactivar
          modoAutoCooler = false;
          coolerManual = !estadoCooler;
        } else {
          // Ya está en manual, solo cambiar estado
          coolerManual = !coolerManual;
        }
        esp32Serial.print(F("Cooler "));
        esp32Serial.print(coolerManual ? F("ON") : F("OFF"));
        esp32Serial.println(F(" (Manual)"));
      }
      
      // TOGGLE BUZZER
      else if (comando == F("TOGGLE_BUZZER")) {
        if (modoAutoBuzzer) {
          // Cambiar a manual y activar/desactivar
          modoAutoBuzzer = false;
          buzzerManual = !estadoBuzzer;
        } else {
          // Ya está en manual, solo cambiar estado
          buzzerManual = !buzzerManual;
        }
        esp32Serial.print(F("Buzzer "));
        esp32Serial.print(buzzerManual ? F("ON") : F("OFF"));
        esp32Serial.println(F(" (Manual)"));
      }
      
      // TOGGLE LIGHT EXTERIOR
      else if (comando == F("TOGGLE_LIGHT_EXT")) {
        if (modoAutoLedExterior) {
          // Cambiar a manual y activar/desactivar
          modoAutoLedExterior = false;
          ledExteriorManual = !estadoLedExterior;
        } else {
          // Ya está en manual, solo cambiar estado
          ledExteriorManual = !ledExteriorManual;
        }
        esp32Serial.print(F("Luz Exterior "));
        esp32Serial.print(ledExteriorManual ? F("ON") : F("OFF"));
        esp32Serial.println(F(" (Manual)"));
      }
      
      // TOGGLE LIGHT INTERIOR
      else if (comando == F("TOGGLE_LIGHT_INT")) {
        if (modoAutoLedInterior) {
          // Cambiar a manual y activar/desactivar
          modoAutoLedInterior = false;
          ledInteriorManual = !estadoLedCasa;
        } else {
          // Ya está en manual, solo cambiar estado
          ledInteriorManual = !ledInteriorManual;
        }
        esp32Serial.print(F("Luz Interior "));
        esp32Serial.print(ledInteriorManual ? F("ON") : F("OFF"));
        esp32Serial.println(F(" (Manual)"));
      }
      
      // COMANDOS PARA VOLVER A MODO AUTOMÁTICO
      else if (comando == F("AUTO_COOLER")) {
        modoAutoCooler = true;
        esp32Serial.println(F("Cooler AUTO"));
      }
      else if (comando == F("AUTO_BUZZER")) {
        modoAutoBuzzer = true;
        esp32Serial.println(F("Buzzer AUTO"));
      }
      else if (comando == F("AUTO_LIGHT_EXT")) {
        modoAutoLedExterior = true;
        esp32Serial.println(F("Luz Exterior AUTO"));
      }
      else if (comando == F("AUTO_LIGHT_INT")) {
        modoAutoLedInterior = true;
        esp32Serial.println(F("Luz Interior AUTO"));
      }
      
      // COMANDOS EXISTENTES
      else if (comando == F("GET_DATA")) {
        enviarDatos();
      }
      else if (comando == F("AUTO_ON")) {
        envioAutomaticoHabilitado = true;
        esp32Serial.println(F("Auto ON"));
      }
      else if (comando == F("AUTO_OFF")) {
        envioAutomaticoHabilitado = false;
        esp32Serial.println(F("Auto OFF"));
      }
      else if (comando == F("PONG_ESP32") || comando == F("ESP32_OK")) {
        wifiConectado = true;
      }
    }
  }
  
  // Debug commands
  if (Serial.available()) {
    String mensaje = Serial.readStringUntil('\n');
    mensaje.trim();
    
    if (mensaje == F("TEST")) {
      verificarWiFi();
    }
    else if (mensaje == F("DATA")) {
      enviarDatos();
    }
    else if (mensaje == F("STATUS")) {
      Serial.print(F("Sistema: "));
      Serial.println(sistemaActivo ? F("ON") : F("OFF"));
      Serial.print(F("WiFi: "));
      Serial.println(wifiConectado ? F("OK") : F("NO"));
      Serial.print(F("Cooler: "));
      Serial.println(modoAutoCooler ? F("AUTO") : F("MANUAL"));
      Serial.print(F("Buzzer: "));
      Serial.println(modoAutoBuzzer ? F("AUTO") : F("MANUAL"));
    }
  }
}

// Secuencia de inicio sin delays
void manejarSecuenciaInicio() {
  if (secuenciaInicioCompletada) return;
  
  unsigned long ahora = millis();
  
  switch (pasoSecuenciaInicio) {
    case 0:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("Iniciando..."));
      tiempoInicioSecuencia = ahora;
      pasoSecuenciaInicio = 1;
      break;
      
    case 1:
      if (ahora - tiempoInicioSecuencia > 1500) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("Regulando MQ2"));
        tiempoInicioSecuencia = ahora;
        pasoSecuenciaInicio = 2;
      }
      break;
      
    case 2:
      if (ahora - tiempoInicioSecuencia > 1500) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("Buscando WiFi..."));
        verificarWiFi();
        tiempoInicioSecuencia = ahora;
        pasoSecuenciaInicio = 3;
      }
      break;
      
    case 3:
      if (ahora - tiempoInicioSecuencia > 1000) {
        lcd.clear();
        lcd.setCursor(0, 0);
        if (wifiConectado) {
          lcd.print(F("WiFi OK!"));
        } else {
          lcd.print(F("Sin WiFi - Local"));
        }
        tiempoInicioSecuencia = ahora;
        pasoSecuenciaInicio = 4;
      }
      break;
      
    case 4:
      if (ahora - tiempoInicioSecuencia > 1000) {
        secuenciaInicioCompletada = true;
        mostroSecuenciaInicio = true;
      }
      break;
  }
}

// Secuencia de encendido sin delays
void manejarSecuenciaEncendido() {
  if (secuenciaEncendidoCompletada || sistemaEncendido) return;
  
  unsigned long ahora = millis();
  
  switch (pasoSecuenciaEncendido) {
    case 0:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("CASA IT"));
      lcd.setCursor(0, 1);
      lcd.print(wifiConectado ? F("Online + Local") : F("Solo Local"));
      tiempoInicioSecuencia = ahora;
      pasoSecuenciaEncendido = 1;
      break;
      
    case 1:
      if (ahora - tiempoInicioSecuencia > 1200) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("Buen dia :)"));
        lcd.setCursor(0, 1);
        lcd.print(F("Sistema Listo"));
        tiempoInicioSecuencia = ahora;
        pasoSecuenciaEncendido = 2;
      }
      break;
      
    case 2:
      if (ahora - tiempoInicioSecuencia > 300) {
        lcd.clear();
        tiempoInicioSecuencia = ahora;
        pasoSecuenciaEncendido = 3;
      }
      break;
      
    case 3:
      if (ahora - tiempoInicioSecuencia > 300) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("Buen dia :)"));
        lcd.setCursor(0, 1);
        lcd.print(F("Sistema Listo"));
        tiempoInicioSecuencia = ahora;
        pasoSecuenciaEncendido = 4;
      }
      break;
      
    case 4:
      if (ahora - tiempoInicioSecuencia > 300) {
        secuenciaEncendidoCompletada = true;
        sistemaEncendido = true;
      }
      break;
  }
}

void mostrarDatos() {
  unsigned long ahora = millis();
  
  // Actualizar LCD solo cada 1 segundo para no sobrecargar
  if (ahora - ultimaActualizacionLCD < 1000) return;
  ultimaActualizacionLCD = ahora;
  
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  int lecturaSensor = analogRead(sensorA0);
  
  lcd.clear();
  
  // Línea 1: Temperatura y Humedad
  lcd.setCursor(0, 0);
  lcd.print(F("T:"));
  lcd.print(temp);
  lcd.print(F("C H:"));
  lcd.print(hum);
  lcd.print(F("%"));
  
  // Línea 2: Calidad del aire
  lcd.setCursor(0, 1);
  if (lecturaSensor < umbralAlcohol) {
    lcd.print(F("Aire Limpio"));
  } else if (lecturaSensor < umbralHumo) {
    lcd.print(F("Alcohol Detectado"));
  } else {
    lcd.print(F("PELIGRO HUMO!"));
  }
  
  // Línea 3: Estados con indicadores de modo
  lcd.setCursor(0, 2);
  lcd.print(F("Int:"));
  lcd.print(estadoLedCasa ? F("ON") : F("OFF"));
  if (!modoAutoLedInterior) lcd.print(F("M"));
  lcd.print(F(" PIR:"));
  lcd.print(pirActivo ? F("ON") : F("OFF"));
  
  // Línea 4: Más estados
  lcd.setCursor(0, 3);
  lcd.print(F("Ext:"));
  lcd.print(estadoLedExterior ? F("ON") : F("OFF"));
  if (!modoAutoLedExterior) lcd.print(F("M"));
  lcd.print(F(" C:"));
  lcd.print(estadoCooler ? F("ON") : F("OFF"));
  if (!modoAutoCooler) lcd.print(F("M"));
  lcd.print(F(" B:"));
  lcd.print(estadoBuzzer ? F("ON") : F("OFF"));
  if (!modoAutoBuzzer) lcd.print(F("M"));
}

void mostrarMovimiento() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Movimiento"));
  lcd.setCursor(0, 1);
  lcd.print(F("Detectado!"));
  lcd.setCursor(0, 2);
  lcd.print(F("Count: "));
  lcd.print(motionCount);
  
  if (wifiConectado) {
    esp32Serial.println(F("EVENT:MOVIMIENTO"));
  }
  
  // SIN DELAY - Se mostrará por 1 segundo en el próximo ciclo
  ultimaActualizacionLCD = millis() - 500; // Forzar actualización rápida
}

void setup() {
  // Configurar pines
  pinMode(pinSwitch, INPUT_PULLUP);
  pinMode(pinPIRControl, INPUT_PULLUP);
  pinMode(pir, INPUT);
  pinMode(sensorA0, INPUT);
  pinMode(sensorCasa, INPUT);
  
  pinMode(led, OUTPUT);
  pinMode(led1, OUTPUT);
  pinMode(COOLER_PIN, OUTPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(pinLedCasa, OUTPUT);
  
  // Estado inicial
  digitalWrite(COOLER_PIN, LOW);
  digitalWrite(buzzer, LOW);
  digitalWrite(pinLedCasa, LOW);
  digitalWrite(led, LOW);
  digitalWrite(led1, LOW);
  
  Wire.begin();
  lcd.begin(20, 4);
  lcd.backlight();
  lcd.clear();
  
  dht.begin();
  Serial.begin(9600);
  esp32Serial.begin(9600);
  
  Serial.println(F("Sistema iniciado - OPTIMIZADO CON TOGGLES"));
  Serial.println(F("ESP32: Pin 10->15, Pin 11->13"));
  Serial.println(F("Toggles: SISTEMA, PIR, COOLER, BUZZER, LIGHT_EXT, LIGHT_INT"));
  
  tiempoInicio = millis();
  
  // Inicializar estados de switches
  estadoAnteriorSwitch = digitalRead(pinSwitch);
  estadoAnteriorPIRControl = digitalRead(pinPIRControl);
}

void loop() {
  unsigned long ahora = millis();
  
  // PRIORIDAD MÁXIMA: Pulsador PIR - SE EJECUTA PRIMERO SIEMPRE
  leerPulsadorPIR();
  
  procesarComandos();
  
  // Verificar WiFi periódicamente
  if (ahora - ultimoChequeoWiFi > intervaloChequeoWiFi) {
    verificarWiFi();
    ultimoChequeoWiFi = ahora;
  }
  
  detectarCambios();
  
  // NUEVO: Manejo de envío periódico de datos
  manejarEnvioDatos();
  
  // Switch principal con respuesta rápida
  if (leerSwitchRapido(pinSwitch, estadoAnteriorSwitch, ultimoCambioSwitch, debounceSwitch)) {
    sistemaActivo = !estadoAnteriorSwitch;
    Serial.println(sistemaActivo ? F("SISTEMA ON") : F("SISTEMA OFF"));
    
    // Reiniciar secuencia de encendido
    if (sistemaActivo) {
      sistemaEncendido = false;
      secuenciaEncendidoCompletada = false;
      pasoSecuenciaEncendido = 0;
    }
  }
  
  // Mostrar secuencia de inicio sin delays
  if (!mostroSecuenciaInicio) {
    manejarSecuenciaInicio();
    return; // No continuar hasta completar secuencia
  }
  
  // Si está apagado, mostrar estado sin delay
  if (!sistemaActivo) {
    digitalWrite(COOLER_PIN, LOW);
    digitalWrite(buzzer, LOW);
    digitalWrite(pinLedCasa, LOW);
    digitalWrite(led, LOW);
    digitalWrite(led1, LOW);
    estadoLedExterior = false;
    estadoCooler = false;
    estadoBuzzer = false;
    
    // Resetear modos a automático cuando se apaga el sistema
    modoAutoCooler = true;
    modoAutoBuzzer = true;
    modoAutoLedExterior = true;
    modoAutoLedInterior = true;
    
    if (ahora - ultimoMensajeApagado > 2000) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("Sistema APAGADO"));
      lcd.setCursor(0, 1);
      lcd.print(wifiConectado ? F("WiFi Disponible") : F("WiFi No Disponible"));
      ultimoMensajeApagado = ahora;
    }
    return;
  }

  // Período de calentamiento sin delay
  if (ahora - tiempoInicio < tiempoCalentamiento) {
    if (ahora - ultimoMensajeCalentamiento > 2000) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("Calentando MQ2"));
      lcd.setCursor(0, 1);
      lcd.print((ahora - tiempoInicio) / 1000);
      lcd.print(F(" / 30 seg"));
      ultimoMensajeCalentamiento = ahora;
    }
    return;
  }
  
  // Manejar secuencia de encendido
  if (sistemaActivo && !sistemaEncendido) {
    manejarSecuenciaEncendido();
    if (!secuenciaEncendidoCompletada) return;
  }

  // Lectura de sensores
  int motion = digitalRead(pir);
  int lecturaSensor = analogRead(sensorA0);
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  int valorCasa = analogRead(sensorCasa);
  
  // Reporte periódico
  if (ahora - ultimoReporte > 30000) {
    Serial.print(F("T:"));
    Serial.print(temp);
    Serial.print(F("°C H:"));
    Serial.print(hum);
    Serial.print(F("% MQ2:"));
    Serial.println(lecturaSensor);
    ultimoReporte = ahora;
  }
  
  // Control por micrófono (solo en modo automático)
  if (modoAutoLedInterior) {
    if (!esperandoBajada && valorCasa >= umbralCasa && (ahora - tiempoUltimoCambio > tiempoDebounce)) {
      estadoLedCasa = !estadoLedCasa;
      digitalWrite(pinLedCasa, estadoLedCasa ? HIGH : LOW);
      
      Serial.print(F("MICRÓFONO - Nivel: "));
      Serial.print(valorCasa);
      Serial.println(estadoLedCasa ? F(" LUZ ON") : F(" LUZ OFF"));
      
      tiempoUltimoCambio = ahora;
      esperandoBajada = true;
    }
    
    if (valorCasa < umbralCasa) {
      esperandoBajada = false;
    }
  } else {
    // Modo manual - aplicar estado manual
    estadoLedCasa = ledInteriorManual;
    digitalWrite(pinLedCasa, estadoLedCasa ? HIGH : LOW);
  }
  
  // CONTROL PIR MEJORADO - LOS LEDS SE MANTIENEN MIENTRAS HAY MOVIMIENTO
  if (pirActivo && modoAutoLedExterior) {
    // Modo automático
    if (motion == HIGH) {
      if (!estadoLedExterior) {
        mostrarMovimiento();
        estadoLedExterior = true;
        Serial.println(F("MOVIMIENTO - LUZ EXTERIOR ON"));
      }
      digitalWrite(led, HIGH);
      digitalWrite(led1, HIGH);
      lastMotionTime = ahora; // ACTUALIZAR TIEMPO CONTINUAMENTE
      motionCount++;
    }
    
    // Solo apagar si no hay movimiento por el tiempo especificado
    if (ahora - lastMotionTime > noMotionDelay) {
      if (estadoLedExterior) {
        Serial.println(F("Sin movimiento - LUZ EXTERIOR OFF"));
        estadoLedExterior = false;
      }
      digitalWrite(led, LOW);
      digitalWrite(led1, LOW);
    }
  } else if (!modoAutoLedExterior) {
    // Modo manual - aplicar estado manual
    estadoLedExterior = ledExteriorManual;
    digitalWrite(led, estadoLedExterior ? HIGH : LOW);
    digitalWrite(led1, estadoLedExterior ? HIGH : LOW);
  } else {
    // PIR desactivado - apagar inmediatamente (solo en automático)
    if (estadoLedExterior) {
      estadoLedExterior = false;
    }
    digitalWrite(led, LOW);
    digitalWrite(led1, LOW);
  }
  
  // Control por MQ2
  if (modoAutoBuzzer) {
    // Modo automático
    if (lecturaSensor >= umbralAlcohol) {
      if (!estadoBuzzer) {
        Serial.println(lecturaSensor >= umbralHumo ? F("INCENDIO - BUZZER ON") : F("ALCOHOL - BUZZER ON"));
        estadoBuzzer = true;
      }
      digitalWrite(buzzer, HIGH);
    } else {
      if (estadoBuzzer) {
        Serial.println(F("BUZZER OFF"));
        estadoBuzzer = false;
      }
      digitalWrite(buzzer, LOW);
    }
  } else {
    // Modo manual - aplicar estado manual
    estadoBuzzer = buzzerManual;
    digitalWrite(buzzer, estadoBuzzer ? HIGH : LOW);
  }
  
  // Control de temperatura
  if (modoAutoCooler) {
    // Modo automático
    if (temp >= TEMP_UMBRAL) {
      if (!estadoCooler) {
        Serial.print(F("VENTILADOR ON - "));
        Serial.print(temp);
        Serial.println(F("°C"));
        estadoCooler = true;
      }
      digitalWrite(COOLER_PIN, HIGH);
    } else {
      if (estadoCooler) {
        Serial.print(F("VENTILADOR OFF - "));
        Serial.print(temp);
        Serial.println(F("°C"));
        estadoCooler = false;
      }
      digitalWrite(COOLER_PIN, LOW);
    }
  } else {
    // Modo manual - aplicar estado manual
    estadoCooler = coolerManual;
    digitalWrite(COOLER_PIN, estadoCooler ? HIGH : LOW);
  }
  
  if (sistemaActivo && sistemaEncendido) {
    mostrarDatos();
  }
  
  // DELAY MÍNIMO - Solo 50ms para no sobrecargar el procesador
  delay(50);
}
