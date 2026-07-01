/* * Project myProject
 * Author: Idwin Balderas
 * Date: 2026
 * For comprehensive documentation and examples, please visit:
 * https://docs.particle.io/firmware/best-practices/firmware-template/
 */

// Include Particle Device OS APIs
#include "Particle.h"
#include "RF24.h"

// Let Device OS manage the connection to the Particle Cloud
SYSTEM_MODE(SEMI_AUTOMATIC);

// Show system, cloud connectivity, and application logs over USB
SerialLogHandler logHandler(LOG_LEVEL_INFO);

// Configuración de hardware para la antena nRF24
RF24 radio(D4, D5);
const uint64_t direccionPipe = 0xE8E8F0F0E1LL; 

// Definición de los estados del menú
enum ModoSistema {
    MODO_MENU,
    MODO_WIFI,
    MODO_BLE,
    MODO_RF24 
};

ModoSistema modoActual = MODO_MENU;
unsigned long ultimoEscaneo = 0;
unsigned long ultimoEscaneoBLE = 0; 
unsigned long ultimoPublishBLE = 0;

// Prototipos de funciones
void mostrarMenu();
void ejecutarEscaneoWiFi();
void manejarRedEncontrada(WiFiAccessPoint* ap, void* cookie);
void onScanResultBLE(const BleScanResult* scanResult, void* context);
void ejecutarEscaneoBLE();
void miManejadorDeEventos(const char *event, const char *data);
void publicarYSerial(const char* mensaje);

void setup() {
    // Esperar hasta 5 segundos a que abras el monitor serie en la laptop
    waitFor(Serial.isConnected, 5000);
    
    // Inicialización obligatoria de la antena nRF24
    radio.begin();
    radio.setPALevel(RF24_PA_MAX); // Máxima potencia para generar interferencia
    radio.setChannel(10);          // Canal 10 del nRF24 (aprox 2410 MHz, canal 1 de Wi-Fi)
    radio.openWritingPipe(direccionPipe); 
    radio.stopListening();         // Configurar en modo transmisión

    // Registro de suscripción para recibir comandos desde "Publish an Event"
    Particle.subscribe("cambiar-modo", miManejadorDeEventos);
    
    mostrarMenu();
}

void loop() {
    // 1. LEER LOS COMANDOS DESDE LA LAPTOP Y EL MONITOR SERIE (INTERRUPCIÓN MANUAL)
    if (Serial.available() > 0) {
        char opcion = Serial.read();
        
        if (opcion != '\n' && opcion != '\r') {
            
            // ACCIÓN CRÍTICA: Si estábamos en BLE, detenemos el escaneo antes que cualquier otra cosa
            if (modoActual == MODO_BLE) {
                BLE.stopScanning();
                delay(50); 
            }

            if (opcion == '1') {
                Log.info("=== Cambiando a Modo: Escáner Wi-Fi ===");
                BLE.off(); 
                modoActual = MODO_WIFI;
                ultimoEscaneo = 0; // Forzar escaneo Wi-Fi inmediato
            }
            else if (opcion == '2') {
                Log.info("=== Cambiando a Modo: Escáner Bluetooth (BLE) ===");
                // Apagamos Wi-Fi para que no genere conflicto con BLE en el chip integrado
                WiFi.off();
                modoActual = MODO_BLE;
                ultimoEscaneoBLE = 0; 
            } 
            else if (opcion == '3') {
                Log.info("=== Cambiando a Modo: Transmisor nRF24 ===");
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

    // 2. EJECUTAR EL MODO SELECCIONADO
    switch (modoActual) {
        case MODO_WIFI:
            if (millis() - ultimoEscaneo >= 8000) {
                ultimoEscaneo = millis();
                ejecutarEscaneoWiFi();
                Log.info("[Tip] Presiona 'M' en el teclado para regresar al menú principal.");
            }
            break;

        case MODO_BLE:
            if (millis() - ultimoEscaneoBLE >= 5000) { 
                ultimoEscaneoBLE = millis();
                ejecutarEscaneoBLE();
                Log.info("[Tip] Presiona 'M' en el teclado para regresar al menú principal.");
            }
            break;

        case MODO_RF24:
            {
                const char ruido[] = "1234567890123456789012345678901";
                radio.write(&ruido, sizeof(ruido));
            }
            break;

        case MODO_MENU:
            break;
    }
}

void miManejadorDeEventos(const char *event, const char *data) {
    String comando = String(data);
    comando.trim();

    if (modoActual == MODO_BLE) {
        BLE.stopScanning();
        delay(50);
    }

    if (comando == "1") {
        Log.info("=== Evento Nube Recibido: Cambiando a Wi-Fi ===");
        BLE.off();
        modoActual = MODO_WIFI;
        ultimoEscaneo = 0;
    } 
    else if (comando == "2") {
        Log.info("=== Evento Nube Recibido: Cambiando a BLE ===");
        WiFi.off();
        modoActual = MODO_BLE;
        ultimoEscaneoBLE = 0;
    } 
    else if (comando == "3") {
        Log.info("=== Evento Nube Recibido: Cambiando a Transmisor nRF24 ===");
        BLE.off();
        WiFi.off();
        modoActual = MODO_RF24;
    }
    else if (comando.equalsIgnoreCase("m")) {
        Log.info("=== Evento Nube Recibido: Cambiando a Menú ===");
        BLE.off();
        WiFi.off();
        modoActual = MODO_MENU;
        mostrarMenu();
    }
}

void publicarYSerial(const char* mensaje) {
    Serial.println(mensaje);
    // Solo intenta publicar si el dispositivo está conectado a la red y nube de Particle
    if (Particle.connected()) {
        Particle.publish("consola-remota", mensaje, PRIVATE);
    }
}

void mostrarMenu() {
    const char* menuCompleto = R"(
=============================================
       MENU DE SELECCION - PHOTON 2        
=============================================
   1. Activar Escáner de Canales Wi-Fi     
   2. Activar Escáner de Dispositivos BLE  
   3. Activar Transmisor nRF24            
---------------------------------------------
 En la nube publica 'cambiar-modo' con 1, 2, 3 o m
=============================================
)";
    publicarYSerial(menuCompleto);
}

void manejarRedEncontrada(WiFiAccessPoint* ap, void* cookie) {
    char bufferConsola[128];
    snprintf(bufferConsola, sizeof(bufferConsola), "SSID: %-25s | Canal: %-3d | Señal: %d dBm\n", ap->ssid, ap->channel, ap->rssi);
    
    // Imprimimos directo a Serial
    Serial.print(bufferConsola);

    // Publicamos de forma remota solo si hay conexión nube activa
    if (Particle.connected()) {
        char datosRemotos[64];
        snprintf(datosRemotos, sizeof(datosRemotos), "SSID:%s|Ch:%d|RSSI:%d", ap->ssid, ap->channel, ap->rssi);
        Particle.publish("wifi-detectada", datosRemotos, PRIVATE);
    }
    
    // ELIMINADO EL DELAY(1000) de aquí para evitar congelar el procesador por cada red.
}

void ejecutarEscaneoWiFi() {
    Log.info("Encendiendo módulo de radio Wi-Fi...");
    WiFi.on(); // <-- CORRECCIÓN CRÍTICA: Activa el hardware antes de interactuar con él.
    delay(100); // Pequeño delay de asentamiento para el firmware del módulo de red

    Log.info("Escaneando canales Wi-Fi cercanos...");
    Serial.println("\n--- REDES WI-FI DETECTADAS ---");
    
    int redesEncontradas = WiFi.scan(manejarRedEncontrada, NULL);
    
    if (redesEncontradas > 0) {
        char bufferFin[128]; 
        snprintf(bufferFin, sizeof(bufferFin), "-------------------------------\nTotal de redes impresas: %d\n\n", redesEncontradas);
        Serial.println(bufferFin);
    } else {
        Serial.println("No se detectaron redes en este escaneo o el radio está ocupado.\n-------------------------------\n\n");
    }
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

void ejecutarEscaneoBLE() {
    Log.info("Iniciando escaneo BLE (4 segundos)...");
    Serial.println("\n--- DISPOSITIVOS BLE DETECTADOS ---");
    
    BLE.on();

    // Configuración limpia y segura de parámetros para el Argon
    BleScanParams scanParams;
    memset(&scanParams, 0, sizeof(BleScanParams)); // Borrar basura en memoria
    scanParams.size = sizeof(BleScanParams);
    scanParams.active = true; // Activa la solicitud de nombres (Scan Response)
    
    // Dejando interval y window en 0, el Argon usa los valores estables por defecto del sistema
    scanParams.interval = 0; 
    scanParams.window = 0;
    
    BLE.setScanParameters(&scanParams);
    
    BLE.setScanTimeout(400); 
    BLE.scan(onScanResultBLE, NULL);
    
    Serial.println("-----------------------------------\n\n");
}