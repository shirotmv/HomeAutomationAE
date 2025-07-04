#ifndef CAMARA_H
#define CAMARA_H

#include <WebServer.h>
#include "esp_camera.h"
#include "SD_MMC.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include "FS.h"

// Configuración de pines para ESP32-CAM
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Declaración externa del servidor web
extern WebServer server;

// Variables globales para la cámara
bool camaraInicializada = false;
bool streamActivo = false;
bool sdInicializada = false; // Esta será actualizada por el código principal
unsigned long ultimoFrame = 0;
const unsigned long intervaloFrame = 100;
String ultimaFotoPath = "";

// Función para inicializar SOLO la cámara
bool inicializarCamara() {
    Serial.println("📷 Inicializando cámara ESP32-CAM...");
    
    // Verificar si ya está inicializada
    if (camaraInicializada) {
        Serial.println("⚠️ Cámara ya inicializada");
        return true;
    }
    
    // Configuración de la cámara
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;

    // Inicializar cámara
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("❌ Error inicializando cámara: 0x%x\n", err);
        return false;
    }
    
    Serial.println("✅ Cámara inicializada correctamente");
    camaraInicializada = true;
    return true;
}

// Función para inicializar SD (ahora será llamada desde el main)
bool inicializarSD() {
    Serial.println("💾 Inicializando tarjeta SD...");
    
    if (sdInicializada) {
        Serial.println("⚠️ SD ya inicializada");
        return true;
    }
    
    if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("❌ Error inicializando tarjeta SD");
        return false;
    }
    
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("❌ No hay tarjeta SD");
        return false;
    }
    
    Serial.printf("✅ Tarjeta SD inicializada - Tipo: %s\n", 
                  cardType == CARD_MMC ? "MMC" : 
                  cardType == CARD_SD ? "SD" : 
                  cardType == CARD_SDHC ? "SDHC" : "UNKNOWN");
    
    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("📊 Tamaño de la tarjeta: %lluMB\n", cardSize);
    
    sdInicializada = true;
    return true;
}

// Función para crear directorio de imágenes
void crearDirectorioImagenes() {
    if (!sdInicializada) {
        Serial.println("❌ SD no inicializada, no se puede crear directorio");
        return;
    }
    
    if (!SD_MMC.exists("/imagenes")) {
        if (SD_MMC.mkdir("/imagenes")) {
            Serial.println("📁 Directorio /imagenes creado");
        } else {
            Serial.println("❌ Error creando directorio /imagenes");
        }
    } else {
        Serial.println("📁 Directorio /imagenes ya existe");
    }
}

// Función para convertir RGB565 a JPEG
bool convertRGB565ToJPEG(camera_fb_t *fb, uint8_t **jpeg_buf, size_t *jpeg_len) {
    return fmt2jpg(fb->buf, fb->len, fb->width, fb->height, PIXFORMAT_RGB565, 80, jpeg_buf, jpeg_len);
}

// Capturar y guardar foto
void handleCapture() {
    Serial.println("📸 Capturando foto...");
    
    if (!camaraInicializada) {
        server.send(500, "application/json", "{\"success\":false,\"error\":\"Cámara no inicializada\"}");
        return;
    }
    
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
        server.send(500, "application/json", "{\"success\":false,\"error\":\"Error capturando imagen\"}");
        return;
    }

    // Convertir RGB565 a JPEG
    uint8_t *jpeg_buf = NULL;
    size_t jpeg_len = 0;
    
    if (!convertRGB565ToJPEG(fb, &jpeg_buf, &jpeg_len)) {
        esp_camera_fb_return(fb);
        server.send(500, "application/json", "{\"success\":false,\"error\":\"Error convirtiendo imagen\"}");
        return;
    }

    // LIBERAR EL FRAME BUFFER INMEDIATAMENTE
    esp_camera_fb_return(fb);

    // Generar nombre único
    String filename = "/imagenes/foto_" + String(millis()) + ".jpg";
    
    // Guardar en SD
    bool guardado = false;
    if (sdInicializada) {
        File file = SD_MMC.open(filename, FILE_WRITE);
        if (file) {
            size_t written = file.write(jpeg_buf, jpeg_len);
            file.close();
            guardado = (written == jpeg_len);
            if (guardado) {
                Serial.printf("✅ Foto guardada: %s\n", filename.c_str());
                ultimaFotoPath = filename;
            }
        }
    }
    
    // LIBERAR BUFFER JPEG INMEDIATAMENTE
    free(jpeg_buf);
    
    if (guardado) {
        String response = "{\"success\":true,\"filename\":\"" + filename.substring(10) + "\"}";
        server.send(200, "application/json", response);
    } else {
        server.send(500, "application/json", "{\"success\":false,\"error\":\"Error guardando archivo\"}");
    }
}

// Stream de video
void handleStream() {
    if (!camaraInicializada) {
        server.send(500, "text/plain", "Cámara no inicializada");
        return;
    }
    
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
        server.send(500, "text/plain", "Error capturando imagen");
        return;
    }

    // Convertir RGB565 a JPEG para mostrar en navegador
    uint8_t *jpeg_buf = NULL;
    size_t jpeg_len = 0;
    
    if (!convertRGB565ToJPEG(fb, &jpeg_buf, &jpeg_len)) {
        esp_camera_fb_return(fb);
        server.send(500, "text/plain", "Error convirtiendo imagen");
        return;
    }

    // Headers para evitar caché
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.setContentLength(jpeg_len);
    server.send(200, "image/jpeg");
    WiFiClient client = server.client();
    client.write(jpeg_buf, jpeg_len);
    
    free(jpeg_buf);
    esp_camera_fb_return(fb);
}

// Listar fotos guardadas
void handleList() {
    if (!sdInicializada) {
        server.send(500, "application/json", "{\"error\":\"SD no disponible\"}");
        return;
    }
    
    File root = SD_MMC.open("/imagenes");
    if (!root) {
        server.send(500, "application/json", "{\"error\":\"No se puede acceder a la carpeta\"}");
        return;
    }

    String json = "{\"files\":[";
    File file = root.openNextFile();
    bool first = true;
    
    while (file) {
        if (!file.isDirectory()) {
            if (!first) json += ",";
            json += "\"" + String(file.name()) + "\"";
            first = false;
        }
        file = root.openNextFile();
    }
    
    json += "]}";
    server.send(200, "application/json", json);
}

// Descargar foto
void handleDownload() {
    if (!server.hasArg("file")) {
        server.send(400, "text/plain", "Archivo no especificado");
        return;
    }

    String filename = "/imagenes/" + server.arg("file");
    
    if (!sdInicializada) {
        server.send(500, "text/plain", "SD no disponible");
        return;
    }
    
    File file = SD_MMC.open(filename, FILE_READ);
    
    if (!file) {
        server.send(404, "text/plain", "Archivo no encontrado");
        return;
    }

    server.setContentLength(file.size());
    server.send(200, "image/jpeg");
    
    WiFiClient client = server.client();
    uint8_t buffer[1024];
    while (file.available()) {
        int bytesRead = file.read(buffer, sizeof(buffer));
        client.write(buffer, bytesRead);
    }
    
    file.close();
}

// Eliminar foto
void handleDelete() {
    if (!server.hasArg("file")) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Archivo no especificado\"}");
        return;
    }

    String filename = "/imagenes/" + server.arg("file");
    
    if (!sdInicializada) {
        server.send(500, "application/json", "{\"success\":false,\"error\":\"SD no disponible\"}");
        return;
    }
    
    if (SD_MMC.remove(filename)) {
        server.send(200, "application/json", "{\"success\":true}");
    } else {
        server.send(500, "application/json", "{\"success\":false,\"error\":\"Error eliminando archivo\"}");
    }
}

// Ver foto
void handleView() {
    if (!server.hasArg("file")) {
        server.send(400, "text/plain", "Archivo no especificado");
        return;
    }

    String filename = "/imagenes/" + server.arg("file");
    
    if (!sdInicializada) {
        server.send(500, "text/plain", "SD no disponible");
        return;
    }
    
    File file = SD_MMC.open(filename, FILE_READ);
    
    if (!file) {
        server.send(404, "text/plain", "Archivo no encontrado");
        return;
    }

    server.setContentLength(file.size());
    server.send(200, "image/jpeg");
    
    WiFiClient client = server.client();
    uint8_t buffer[1024];
    while (file.available()) {
        int bytesRead = file.read(buffer, sizeof(buffer));
        client.write(buffer, bytesRead);
    }
    
    file.close();
}

// Función para obtener información de la cámara
void infoCamara() {
    String json = "{";
    json += "\"inicializada\":" + String(camaraInicializada ? "true" : "false") + ",";
    json += "\"sd_disponible\":" + String(sdInicializada ? "true" : "false") + ",";
    json += "\"formato\":\"RGB565\",";
    json += "\"resolucion\":\"320x240\",";
    json += "\"memoria_libre\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"psram_disponible\":" + String(psramFound() ? "true" : "false");
    json += "}";
    
    server.send(200, "application/json", json);
}

// Función principal de configuración - SOLO configura rutas, no inicializa nada
void configurarRutasCamara() {
    Serial.println("🚀 Configurando rutas de cámara...");
    
    // Configurar rutas del servidor web
    server.on("/capture", handleCapture);
    server.on("/stream", handleStream);
    server.on("/list", handleList);
    server.on("/download", handleDownload);
    server.on("/view", handleView);
    server.on("/delete", handleDelete);
    server.on("/info", infoCamara);
    
    Serial.println("✅ Rutas de cámara configuradas correctamente");
}

// Función para verificar el estado en el loop principal
void actualizarCamara() {
    // Verificar estado de la cámara periódicamente
    static unsigned long ultimaVerificacion = 0;
    const unsigned long intervaloVerificacion = 30000; // 30 segundos
    
    if (millis() - ultimaVerificacion > intervaloVerificacion) {
        if (camaraInicializada) {
            // Verificar que la cámara sigue funcionando
            camera_fb_t* fb = esp_camera_fb_get();
            if (fb) {
                esp_camera_fb_return(fb);
            } else {
                Serial.println("⚠️ Cámara no responde - Puede necesitar reinicio");
            }
        }
        ultimaVerificacion = millis();
    }
}

#endif // CAMARA_H
