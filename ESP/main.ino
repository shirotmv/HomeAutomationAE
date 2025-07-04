#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include "SD_MMC.h"
#include "FS.h"
#include "camara.h"

// Configuración WiFi
const char* ssid = "REDTEMPORAL";           
const char* password = "AIHouseArduino25";   

// Configuración de pines
#define ARDUINO_RX_PIN 15  
#define ARDUINO_TX_PIN 13  
#define BAUD_RATE 9600

// Servidor web
WebServer server(80);

// Variables de estado
bool wifiConectado = false;
bool sistemaActivo = false;     
bool pirActivo = false;         
bool ledCasa = false;
bool ledExterior = false;
bool cooler = false;
bool buzzer = false;
bool envioAutomatico = false;
bool datosRecibidos = false;    

// Variable para controlar el estado de la SD
bool sdDisponible = false;
String htmlCacheado = "";      

// Variables para los modos
String modoCooler = "auto";
String modoBuzzer = "auto";
String modoLedExt = "auto";
String modoLedInt = "auto";

// Datos de sensores
float temperatura = 0.0;
float humedad = 0.0;
int valorMQ2 = 0;
int valorMic = 0;
int valorPIR = 0;
int contadorMovimientos = 0;

// Control de tiempos
unsigned long ultimoPing = 0;
unsigned long ultimaActualizacion = 0;
const unsigned long intervaloPing = 10000;      
const unsigned long intervaloActualizacion = 2500; 

// Comunicación serial con Arduino
HardwareSerial arduinoSerial(1);

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("=== ESP32 Casa Inteligente ===");
  Serial.println("Iniciando sistema...");
  
  // PRIMERO: Inicializar SD desde el main
  if (inicializarSD()) {
    sdDisponible = true;
    sdInicializada = true; // Actualizar variable global de camara.h
    Serial.println("✅ SD inicializada correctamente");
    cargarHTMLCache();
  } else {
    Serial.println("❌ SD no disponible - funcionando sin tarjeta");
    sdDisponible = false;
    sdInicializada = false;
  }
  
  // SEGUNDO: Inicializar cámara (usa la SD ya inicializada)
  if (inicializarCamara()) {
    Serial.println("✅ Cámara inicializada correctamente");
    // Crear directorio de imágenes si SD está disponible
    if (sdDisponible) {
      crearDirectorioImagenes();
    }
  } else {
    Serial.println("❌ Error inicializando cámara");
  }

  // Inicializar comunicación con Arduino
  arduinoSerial.begin(BAUD_RATE, SERIAL_8N1, ARDUINO_RX_PIN, ARDUINO_TX_PIN);
  
  // Conectar a WiFi
  conectarWiFi();
  
  // Configurar rutas del servidor web
  configurarServidor();
  
  // Configurar rutas de cámara (sin inicializar nada)
  configurarRutasCamara();
  
  // Iniciar servidor web
  server.begin();
  Serial.println("✅ Servidor web iniciado");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  
  // Enviar confirmación inicial al Arduino
  arduinoSerial.println("ESP32_OK");
  
  Serial.println("✅ Sistema ESP32 listo!");
}

void loop() {
  unsigned long ahora = millis();
  
  // Manejar cliente web
  server.handleClient();
  
  // Actualizar cámara
  actualizarCamara();
  
  // Verificar conexión WiFi
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiConectado) {
      Serial.println("WiFi desconectado. Intentando reconectar...");
      wifiConectado = false;
    }
  } else {
    wifiConectado = true;
  }
  
  // Procesar comandos del Arduino
  procesarMensajesArduino();
  
  // Ping periódico al Arduino
  if (ahora - ultimoPing > intervaloPing) {
    arduinoSerial.println("PONG_ESP32");
    ultimoPing = ahora;
  }
  
  // Solicitar datos periódicamente si el envío automático está habilitado
  if (envioAutomatico && ahora - ultimaActualizacion > intervaloActualizacion) {
    arduinoSerial.println("ESP32:GET_DATA");
    ultimaActualizacion = ahora;
  }
  
  delay(50); 
}

void conectarWiFi() {
  Serial.println("Configurando punto de acceso...");

  WiFi.softAP("CasaInteligenteESP", "clave12345"); 

  delay(100); 

  Serial.println("Punto de acceso creado!");
  Serial.print("IP local del AP: ");
  Serial.println(WiFi.softAPIP());

  wifiConectado = true;  
}

void procesarMensajesArduino() {
  if (arduinoSerial.available()) {
    String mensaje = arduinoSerial.readStringUntil('\n');
    mensaje.trim();
    
    Serial.print("Arduino -> ESP32: ");
    Serial.println(mensaje);
    
    // Responder a ping del Arduino
    if (mensaje == "PING_ESP32") {
      arduinoSerial.println("PONG_ESP32");
      Serial.println("ESP32 -> Arduino: PONG_ESP32");
    }
    // Procesar datos de sensores
    else if (mensaje.startsWith("DATA:")) {
      procesarDatosSensores(mensaje.substring(5));
    }
    // Procesar eventos
    else if (mensaje.startsWith("EVENT:")) {
      String evento = mensaje.substring(6);
      Serial.println("Evento recibido: " + evento);
    }
    // Procesar cambios de estado
    else if (mensaje.startsWith("CHANGE:")) {
      String cambio = mensaje.substring(7);
      Serial.println("Cambio detectado: " + cambio);
    }
  }
}

void procesarDatosSensores(String jsonData) {
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, jsonData);
  
  if (error) {
    Serial.print("Error al parsear JSON: ");
    Serial.println(error.c_str());
    return;
  }
  
  // Actualizar variables locales
  temperatura = doc["temp"] | 0.0;
  humedad = doc["hum"] | 0.0;
  valorMQ2 = doc["mq2"] | 0;
  valorMic = doc["mic"] | 0;
  valorPIR = doc["pir"] | 0;
  ledCasa = doc["ledCasa"] | false;
  ledExterior = doc["ledExt"] | false;
  cooler = doc["cooler"] | false;
  buzzer = doc["buzzer"] | false;
  sistemaActivo = doc["sistema"] | false;
  pirActivo = doc["pirActivo"] | false;
  contadorMovimientos = doc["motions"] | 0;
  
  // Actualizar nuevas variables de modo
  modoCooler = doc["modoCooler"] | "auto";
  modoBuzzer = doc["modoBuzzer"] | "auto";
  modoLedExt = doc["modoLedExt"] | "auto";
  modoLedInt = doc["modoLedInt"] | "auto";
  
  datosRecibidos = true;  
  Serial.println("Datos actualizados desde Arduino");
}

void configurarServidor() {
  // Página principal
  server.on("/", HTTP_GET, []() {
    String html = generarPaginaPrincipal();
    server.send(200, "text/html", html);
  });
  
  // API para obtener datos
  server.on("/api/datos", HTTP_GET, []() {
    DynamicJsonDocument doc(1024);
    doc["temperatura"] = temperatura;
    doc["humedad"] = humedad;
    doc["mq2"] = valorMQ2;
    doc["microfono"] = valorMic;
    doc["pir"] = valorPIR;
    doc["ledCasa"] = ledCasa;
    doc["ledExterior"] = ledExterior;
    doc["cooler"] = cooler;
    doc["buzzer"] = buzzer;
    doc["sistemaActivo"] = sistemaActivo;
    doc["pirActivo"] = pirActivo;
    doc["movimientos"] = contadorMovimientos;
    doc["wifiConectado"] = wifiConectado;
    doc["envioAutomatico"] = envioAutomatico;
    doc["datosRecibidos"] = datosRecibidos;
    doc["camaraActiva"] = camaraInicializada;
    doc["sdDisponible"] = sdDisponible;
    
    // Variables de modo
    doc["modoCooler"] = modoCooler;
    doc["modoBuzzer"] = modoBuzzer;
    doc["modoLedExt"] = modoLedExt;
    doc["modoLedInt"] = modoLedInt;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  // Controles básicos
  server.on("/api/toggle/sistema", HTTP_POST, []() {
    arduinoSerial.println("ESP32:TOGGLE_SISTEMA");
    server.send(200, "text/plain", "Sistema toggle enviado");
  });
  
  server.on("/api/toggle/pir", HTTP_POST, []() {
    arduinoSerial.println("ESP32:TOGGLE_PIR");
    server.send(200, "text/plain", "PIR toggle enviado");
  });
  
  // Controles toggle
  server.on("/api/toggle/cooler", HTTP_POST, []() {
    arduinoSerial.println("ESP32:TOGGLE_COOLER");
    server.send(200, "text/plain", "Cooler toggle enviado");
  });
  
  server.on("/api/toggle/buzzer", HTTP_POST, []() {
    arduinoSerial.println("ESP32:TOGGLE_BUZZER");
    server.send(200, "text/plain", "Buzzer toggle enviado");
  });
  
  server.on("/api/toggle/luz_ext", HTTP_POST, []() {
    arduinoSerial.println("ESP32:TOGGLE_LIGHT_EXT");
    server.send(200, "text/plain", "Luz exterior toggle enviado");
  });
  
  server.on("/api/toggle/luz_int", HTTP_POST, []() {
    arduinoSerial.println("ESP32:TOGGLE_LIGHT_INT");
    server.send(200, "text/plain", "Luz interior toggle enviado");
  });
  
  // Controles para volver a automático
  server.on("/api/auto/cooler", HTTP_POST, []() {
    arduinoSerial.println("ESP32:AUTO_COOLER");
    server.send(200, "text/plain", "Cooler modo automático");
  });
  
  server.on("/api/auto/buzzer", HTTP_POST, []() {
    arduinoSerial.println("ESP32:AUTO_BUZZER");
    server.send(200, "text/plain", "Buzzer modo automático");
  });
  
  server.on("/api/auto/luz_ext", HTTP_POST, []() {
    arduinoSerial.println("ESP32:AUTO_LIGHT_EXT");
    server.send(200, "text/plain", "Luz exterior modo automático");
  });
  
  server.on("/api/auto/luz_int", HTTP_POST, []() {
    arduinoSerial.println("ESP32:AUTO_LIGHT_INT");
    server.send(200, "text/plain", "Luz interior modo automático");
  });
  
  // Control de envío automático
  server.on("/api/toggle/auto", HTTP_POST, []() {
    envioAutomatico = !envioAutomatico;
    if (envioAutomatico) {
      arduinoSerial.println("ESP32:AUTO_ON");
    } else {
      arduinoSerial.println("ESP32:AUTO_OFF");
    }
    server.send(200, "text/plain", envioAutomatico ? "Automático ON" : "Automático OFF");
  });
  
  // Solicitar datos manualmente
  server.on("/api/actualizar", HTTP_POST, []() {
    arduinoSerial.println("ESP32:GET_DATA");
    server.send(200, "text/plain", "Datos solicitados");
  });
  
  // Ruta para recargar HTML desde SD
  server.on("/api/reload", HTTP_POST, []() {
    if (sdDisponible) {
      cargarHTMLCache();
      server.send(200, "text/plain", "HTML recargado desde SD");
    } else {
      server.send(200, "text/plain", "SD no disponible");
    }
  });
}

void cargarHTMLCache() {
  if (!sdDisponible) return;
  
  Serial.println("🔄 Cargando HTML desde SD...");
  
  // Probar diferentes rutas
  const char* rutas[] = {"/index.html", "/INDEX.HTML", "/Index.html", "index.html"};
  
  for (int i = 0; i < 4; i++) {
    Serial.printf("Probando: '%s' -> ", rutas[i]);
    
    File file = SD_MMC.open(rutas[i], FILE_READ);
    if (file && file.size() > 0) {
      Serial.println("✅ ¡ENCONTRADO!");
      
      // Leer el archivo completo
      htmlCacheado = file.readString();
      file.close();
      
      if (htmlCacheado.length() > 0) {
        Serial.printf("✅ HTML cargado correctamente (%d bytes)\n", htmlCacheado.length());
        return;
      }
    }
    
    if (file) file.close();
    Serial.println("❌");
  }
  
  Serial.println("❌ No se pudo cargar index.html");
  htmlCacheado = ""; 
}

String generarPaginaPrincipal() {
  // Si tenemos HTML cacheado, usarlo
  if (htmlCacheado.length() > 0) {
    Serial.println("📄 Sirviendo HTML desde cache");
    return htmlCacheado;
  }
  
  // Si SD está disponible pero no hay cache, intentar cargar
  if (sdDisponible) {
    Serial.println("🔄 Intentando cargar HTML desde SD...");
    cargarHTMLCache();
    
    if (htmlCacheado.length() > 0) {
      Serial.println("📄 HTML cargado y servido");
      return htmlCacheado;
    }
  }
  
  // Si no hay HTML disponible, usar versión básica
  Serial.println("📄 Sirviendo HTML básico");
  return generarHTMLBasico();
}

String generarHTMLBasico() {
  return R"(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 Casa Inteligente con Cámara</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
       
</head>
<body>

</body>
</html>
)";
}
