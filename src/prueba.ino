/* * Project: Mega-Jammer-Dual-SPI1
 * Author: Idwin Balderas & Gemini
 * Date: 2026
 * Description: Sistema unificado con escáner Wi-Fi/BLE (módulo interno) 
 * y Transmisor de alta potencia Dual nRF24L01 sobre SPI1.
 */

#include "Particle.h"
#include <RF24.h>

// Elige el modo de gestión de la nube (SEMI_AUTOMATIC no bloquea si no hay red)
SYSTEM_MODE(SEMI_AUTOMATIC);
SerialLogHandler logHandler(LOG_LEVEL_INFO);

// ==========================================
//   CONFIGURACIÓN HARDWARE DUAL nRF24 (SPI1)
// ==========================================
#define CE_PIN_A    D6
#define CSN_PIN_A   A2

#define CE_PIN_B    D5
#define CSN_PIN_B   A1

RF24 radioA(CE_PIN_A, CSN_PIN_A);
RF24 radioB(CE_PIN_B, CSN_PIN_B);

const uint64_t direccion_wifi = 0xE8E8F0F0E1LL;
const uint64_t direccion_ble  = 0x42414c4542LL;

int sub_modo_rf24 = 0; // 0 = BLE, 1 = Wi-Fi (Controlado por el botón MODE)
bool boton_presionado = false;

unsigned long ultimoReporteHW = 0;
const unsigned long intervaloReporteHW = 3000;

// ==========================================
//          ESTADOS DEL MENÚ GENERAL
// ==========================================
enum ModoSistema {
    MODO_MENU,
    MODO_WIFI,
    MODO_BLE,
    MODO_RF24 
};

ModoSistema modoActual = MODO_MENU;
unsigned long ultimoEscaneoWiFi = 0;
unsigned long ultimoEscaneoBLE  = 0;
uint8_t canal_barrido_A = 0;
uint8_t canal_barrido_B = 62; // Empezamos a la mitad del espectro para cubrir más rango en paralelo
// Prototipos de funciones
void mostrarMenu();
void ejecutarEscaneoWiFi();
void manejarRedEncontrada(WiFiAccessPoint* ap, void* cookie);
void onScanResultBLE(const BleScanResult* scanResult, void* context);
void ejecutarEscaneoBLE();
void miManejadorDeEventos(const char *event, const char *data);
void publicarYSerial(const char* mensaje);
uint8_t leerRegistroManual(rf24_gpio_pin_t csn, uint8_t reg);
void configureAntenna(RF24 &radio, uint8_t canal, rf24_datarate_e velocidad, uint64_t dir, char id);
void aplicarCanalesDualnRF24(int modo);

// ==========================================
//                  SETUP
// ==========================================
void setup() {
    // Esperar hasta 5 segundos al monitor serie
    waitFor(Serial.isConnected, 5000);
    
    // Liberar botón físico MODE del Photon/Argon para usarlo de switch
    System.enableFeature((HAL_Feature)1);

    Serial.println("\n==================================================");
    Serial.println("SISTEMA CENTRALIZADO: ESCÁNERES + DUAL nRF24 (SPI1)");
    Serial.println("==================================================");

    // Configurar CSN como salidas estables antes de encender el bus SPI1
    pinMode(CSN_PIN_A, OUTPUT);
    pinMode(CSN_PIN_B, OUTPUT);
    digitalWrite(CSN_PIN_A, HIGH);
    digitalWrite(CSN_PIN_B, HIGH);

    // DELAY CRÍTICO: Carga de capacitores de 100uF para estabilizar las antenas
    delay(1000);
    SPI1.begin();

    // Suscripción a la nube Particle
    Particle.subscribe("cambiar-modo", miManejadorDeEventos);
    
    // Mostrar estado inicial de las antenas en frío
    uint8_t stA = leerRegistroManual(CSN_PIN_A, 0x07);
    uint8_t stB = leerRegistroManual(CSN_PIN_B, 0x07);
    Serial.printf("[HARDWARE] Registro STATUS Inicial Radio A: 0x%02X\n", stA);
    Serial.printf("[HARDWARE] Registro STATUS Inicial Radio B: 0x%02X\n", stB);

    // Inicializar frecuencias base de las antenas externas
    aplicarCanalesDualnRF24(sub_modo_rf24);

    mostrarMenu();
}

// ==========================================
//                  LOOP
// ==========================================
void loop() {
    // 1. CONTROL DE COMANDOS POR MONITOR SERIE (LAPTOP)
    if (Serial.available() > 0) {
        char opcion = Serial.read();
        
        if (opcion != '\n' && opcion != '\r') {
            
            // Acción preventiva de limpieza antes de cambiar de estado
            if (modoActual == MODO_BLE) {
                BLE.stopScanning();
                delay(50); 
            }

            if (opcion == '1') {
                Log.info("=== Cambiando a Modo: Escáner Wi-Fi ===");
                BLE.off(); 
                modoActual = MODO_WIFI;
                ultimoEscaneoWiFi = 0; // Forzar escaneo Wi-Fi inmediato
            }
            else if (opcion == '2') {
                Log.info("=== Cambiando a Modo: Escáner Bluetooth (BLE) ===");
                WiFi.off();
                modoActual = MODO_BLE;
                ultimoEscaneoBLE = 0; 
            } 
            else if (opcion == '3') {
                Log.info("=== Cambiando a Modo: Transmisor Dual nRF24 ===");
                BLE.off(); 
                WiFi.off();
                modoActual = MODO_RF24;
            }
            else if (opcion == 'm' || opcion == 'M') {
                Log.info("=== Regresando al Menú Principal ===");
                BLE.off();
                WiFi.off();
                modoActual = MODO_MENU;
                mostrarMenu();
            } 
            else {
                Log.warn("Opción no válida. Presiona 1, 2, 3 o M.");
            }
        }
    }

    // 2. MÁQUINA DE ESTADOS RECURRENTE
    switch (modoActual) {
        case MODO_WIFI:
            if (millis() - ultimoEscaneoWiFi >= 8000) {
                ultimoEscaneoWiFi = millis();
                ejecutarEscaneoWiFi();
                Log.info("[Tip] Presiona 'M' en el teclado para regresar al menú.");
            }
            break;

        case MODO_BLE:
            if (millis() - ultimoEscaneoBLE >= 5000) { 
                ultimoEscaneoBLE = millis();
                ejecutarEscaneoBLE();
                Log.info("[Tip] Presiona 'M' en el teclado para regresar al menú.");
            }
            break;

        case MODO_RF24:
            // 1. Alternar o avanzar los canales en cada ciclo (Barrido Automático)
            canal_barrido_A++;
            canal_barrido_B++;

            // Reiniciar si superan el límite de 125 canales
            if (canal_barrido_A > 125) canal_barrido_A = 0;
            if (canal_barrido_B > 125) canal_barrido_B = 0;

            // 2. Aplicar los nuevos canales al vuelo de forma rápida
            SPI1.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
            radioA.setChannel(canal_barrido_A);
            radioB.setChannel(canal_barrido_B);
            SPI1.endTransaction();

            // 3. Preparar y enviar el paquete de datos inmediatamente
            {
                char paquete_datos[32];
                // Llenamos con un patrón de bits alternados (0xAA = 10101010) para generar ruido constante
                memset(paquete_datos, 0xAA, sizeof(paquete_datos)); 
                
                radioA.startFastWrite(&paquete_datos, sizeof(paquete_datos), false);
                radioB.startFastWrite(&paquete_datos, sizeof(paquete_datos), false);
            }

            // 4. Reporte de salud periódico cada 3 segundos (para no saturar el Monitor Serie)
            if (millis() - ultimoReporteHW >= intervaloReporteHW) {
                ultimoReporteHW = millis();
                uint8_t statusA  = leerRegistroManual(CSN_PIN_A, 0x07);
                uint8_t statusB  = leerRegistroManual(CSN_PIN_B, 0x07);

                Serial.printf("\n[%ds] ----------- BARRIDO AUTOMÁTICO ACTIVO -----------\n", (int)(millis()/1000));
                Serial.printf("RADIO A -> STATUS: 0x%02X | Canal Actual: %d\n", statusA, canal_barrido_A);
                Serial.printf("RADIO B -> STATUS: 0x%02X | Canal Actual: %d\n", statusB, canal_barrido_B);
                Serial.println("-------------------------------------------------------\n");
            }
            break;

        case MODO_MENU:
            // En reposo, esperando comandos
            break;
    }
}

// ==========================================
//    MANEJADORES Y FUNCIONES DE NRF24 (SPI1)
// ==========================================
uint8_t leerRegistroManual(rf24_gpio_pin_t csn, uint8_t reg) {
    digitalWrite(csn, LOW);
    delayMicroseconds(5);
    SPI1.transfer(reg & 0x1F);         
    uint8_t resultado = SPI1.transfer(0x00); 
    digitalWrite(csn, HIGH);
    return resultado;
}

void configureAntenna(RF24 &radio, uint8_t canal, rf24_datarate_e velocidad, uint64_t dir, char id) {
    Serial.printf("[SPI1 -> Radio %c] Seteando configuraciones de RF...\n", id);
    radio.begin(&SPI1); 
    
    // Fijación estricta de tiempos del bus SPI1
    SPI1.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    radio.setAutoAck(false); 
    radio.stopListening();   
    radio.setRetries(0, 0);
    radio.setPALevel(RF24_PA_MAX); 
    radio.setDataRate(velocidad);
    radio.setCRCLength(RF24_CRC_DISABLED); 
    radio.setChannel(canal);
    radio.openWritingPipe(dir);
    SPI1.endTransaction();
}

void aplicarCanalesDualnRF24(int modo) {
    if (modo == 0) {
        Serial.println("\n[nRF24] >>> CONFIGURADO EN CANALES BLE (Espectro Bajo/Alto) <<<");
        configureAntenna(radioA, 38, RF24_2MBPS, direccion_ble, 'A'); 
        configureAntenna(radioB, 39, RF24_2MBPS, direccion_ble, 'B');
    } else {
        Serial.println("\n[nRF24] >>> CONFIGURADO EN CANALES WIFI (Saturación Dual) <<<");
        configureAntenna(radioA, 76, RF24_2MBPS, direccion_wifi, 'A'); 
        configureAntenna(radioB, 80, RF24_2MBPS, direccion_wifi, 'B'); 
    }
}

// ==========================================
//          ESCÁNER INTEGRADO (INTERNO)
// ==========================================
void ejecutarEscaneoWiFi() {
    Log.info("Encendiendo hardware Wi-Fi interno...");
    WiFi.on(); 
    delay(100); 

    Log.info("Escaneando redes...");
    Serial.println("\n--- REDES WI-FI DETECTADAS ---");
    int redesEncontradas = WiFi.scan(manejarRedEncontrada, NULL);
    
    if (redesEncontradas > 0) {
        Serial.printf("-------------------------------\nTotal de redes impresas: %d\n\n", redesEncontradas);
    } else {
        Serial.println("No se detectaron redes o el módulo está ocupado.\n-------------------------------\n\n");
    }
}

void manejarRedEncontrada(WiFiAccessPoint* ap, void* cookie) {
    char bufferConsola[128];
    snprintf(bufferConsola, sizeof(bufferConsola), "SSID: %-25s | Canal: %-3d | Señal: %d dBm\n", ap->ssid, ap->channel, ap->rssi);
    Serial.print(bufferConsola);

    if (Particle.connected()) {
        char datosRemotos[64];
        snprintf(datosRemotos, sizeof(datosRemotos), "SSID:%s|Ch:%d|RSSI:%d", ap->ssid, ap->channel, ap->rssi);
        Particle.publish("wifi-detectada", datosRemotos, PRIVATE);
    }
}

void ejecutarEscaneoBLE() {
    Log.info("Encendiendo hardware BLE interno...");
    BLE.on();

    Serial.println("\n--- DISPOSITIVOS BLE DETECTADOS ---");
    BleScanParams scanParams;
    memset(&scanParams, 0, sizeof(BleScanParams)); 
    scanParams.size = sizeof(BleScanParams);
    scanParams.active = true; 
    scanParams.interval = 0; 
    scanParams.window = 0;
    
    BLE.setScanParameters(&scanParams);
    BLE.setScanTimeout(400); 
    BLE.scan(onScanResultBLE, NULL);
    
    Serial.println("-----------------------------------\n\n");
}

void onScanResultBLE(const BleScanResult* scanResult, void* context) {
    BleAddress addr = scanResult->address();
    String name = scanResult->advertisingData().deviceName();
    if (name.length() == 0) name = "[Anonimo]";

    char bufferConsola[128];
    snprintf(bufferConsola, sizeof(bufferConsola), "MAC: %s | RSSI: %d dBm | Nombre: %s\n", addr.toString().c_str(), scanResult->rssi(), name.c_str());
    Serial.print(bufferConsola);

    if (Particle.connected()) {
        char datosBle[128];
        snprintf(datosBle, sizeof(datosBle), "MAC:%s|RSSI:%d|Name:%s", addr.toString().c_str(), scanResult->rssi(), name.c_str());
        Particle.publish("ble-detectado", datosBle, PRIVATE);
    }
}

// ==========================================
//          SERVICIOS EN LA NUBE
// ==========================================
void miManejadorDeEventos(const char *event, const char *data) {
    String comando = String(data);
    comando.trim();

    if (modoActual == MODO_BLE) {
        BLE.stopScanning();
        delay(50);
    }

    if (comando == "1") {
        Log.info("=== Evento Nube: Cambiando a Wi-Fi ===");
        BLE.off();
        modoActual = MODO_WIFI;
        ultimoEscaneoWiFi = 0;
    } 
    else if (comando == "2") {
        Log.info("=== Evento Nube: Cambiando a BLE ===");
        WiFi.off();
        modoActual = MODO_BLE;
        ultimoEscaneoBLE = 0;
    } 
    else if (comando == "3") {
        Log.info("=== Evento Nube: Cambiando a Transmisor Dual nRF24 ===");
        BLE.off();
        WiFi.off();
        modoActual = MODO_RF24;
    }
    else if (comando.equalsIgnoreCase("m")) {
        Log.info("=== Evento Nube: Regresando a Menú ===");
        BLE.off();
        WiFi.off();
        modoActual = MODO_MENU;
        mostrarMenu();
    }
}

void publicarYSerial(const char* mensaje) {
    Serial.println(mensaje);
    if (Particle.connected()) {
        Particle.publish("consola-remota", mensaje, PRIVATE);
    }
}

void mostrarMenu() {
    const char* menuCompleto = R"(
=============================================
       MENU DE SELECCION CENTRALIZADO        
=============================================
   1. Activar Escáner de Canales Wi-Fi (Chip)
   2. Activar Escáner de Dispositivos BLE (Chip)
   3. Activar Transmisor DUAL nRF24L01 (SPI1)
---------------------------------------------
 * En Modo 3, usa el botón físico 'MODE' para 
   switchear los canales de las antenas externas.
 * Envía por evento nube con 1, 2, 3 o m
=============================================
)";
    publicarYSerial(menuCompleto);
}