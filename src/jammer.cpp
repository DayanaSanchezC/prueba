/* 
 * Project myProject
 * Author: Your Name
 * Date: 
 * For comprehensive documentation and examples, please visit:
 * https://docs.particle.io/firmware/best-practices/firmware-template/
 */

// Include Particle Device OS APIs
#include "Particle.h"
#include "RF24.h"

// Let Device OS manage the connection to the Particle Cloud
SYSTEM_MODE(AUTOMATIC);

// Run the application and system concurrently in separate threads
//SYSTEM_THREAD(ENABLED);

// Show system, cloud connectivity, and application logs over USB
// View logs with CLI using 'particle serial monitor --follow'
SerialLogHandler logHandler(LOG_LEVEL_INFO);

// setup() runs once, when the device is first turned on
// En tu código de la antena nRF24:
/*void setup() {
  radio.begin();
  radio.setPALevel(RF24_PA_MAX); // Máxima potencia para generar interferencia
  radio.setChannel(10);          // Canal 10 del nRF24 (aprox 2410 MHz, canal 1 de Wi-Fi)
  radio.openWritingPipe(pipe);
}

void loop() {
  const char ruido[] = "1234567890123456789012345678901"; // Paquete pesado
  
  // Transmitir en bucle lo más rápido posible sin delays
  radio.write(&ruido, sizeof(ruido)); 
}
*/



// Configuramos el sistema en modo SEMI_AUTOMATIC para que no se trabe
// intentando conectar a la nube si eliges el modo BLE.

// Definición de los estados del menú
enum ModoSistema {
    MODO_MENU,
    MODO_WIFI,
    MODO_BLE
};

ModoSistema modoActual = MODO_MENU;
unsigned long ultimoEscaneo = 0;

// Prototipos de funciones
void mostrarMenu();
void ejecutarEscaneoWiFi();
void onScanResultBLE(const BleScanResult* scanResult, void* context);
void ejecutarEscaneoBLE();

void setup() {
    // Esperar hasta 5 segundos a que abras el monitor serie en la laptop
    waitFor(Serial.isConnected, 5000);
    mostrarMenu();
}

void loop() {
    // 1. LEER LOS COMANDOS DESDE LA LAPTOP
    if (Serial.available() > 0) {
        char opcion = Serial.read();
        
        // Limpiar caracteres basura como saltos de línea (\n o \r)
        if (opcion == '\n' || opcion == '\r') return;

        if (opcion == '1') {
            Log.info("=== Cambiando a Modo: Escáner Wi-Fi ===");
            modoActual = MODO_WIFI;
            ultimoEscaneo = 0; // Forzar escaneo inmediato
        } 
        else if (opcion == '2') {
            Log.info("=== Cambiando a Modo: Escáner Bluetooth (BLE) ===");
            modoActual = MODO_BLE;
            ultimoEscaneo = 0;
        } 
        else if (opcion == 'm' || opcion == 'M') {
            // Regresar al menú principal y apagar radios si es necesario
            BLE.off();
            modoActual = MODO_MENU;
            mostrarMenu();
        } 
        else {
            Log.warn("Opción no válida. Presiona 1, 2 o M.");
        }
    }

    // 2. EJECUTAR EL MODO SELECCIONADO
    switch (modoActual) {
        case MODO_WIFI:
            // Escanear cada 8 segundos
            if (millis() - ultimoEscaneo >= 8000) {
                ultimoEscaneo = millis();
                ejecutarEscaneoWiFi();
                Log.info("[Tip] Presiona 'M' en el teclado para regresar al menú principal.");
            }
            break;

        case MODO_BLE:
            // El escaneo de BLE es síncrono y toma 4 segundos
            ejecutarEscaneoBLE();
            Log.info("[Tip] Presiona 'M' en el teclado para regresar al menú principal.");
            delay(4000); // Espera entre escaneos
            break;

        case MODO_MENU:
            // No hacer nada, esperar la selección del usuario
            break;
    }
}

// Muestra el menú de opciones en la terminal de la laptop
void mostrarMenu() {
    Serial.println("\n=============================================");
    Serial.println("          MENU DE SELECCION - PHOTON 2       ");
    Serial.println("=============================================");
    Serial.println(" 1. Activar Escáner de Canales Wi-Fi");
    Serial.println(" 2. Activar Escáner de Dispositivos BLE");
    Serial.println("---------------------------------------------");
    Serial.println(" Escribe el número (1 o 2) y presiona Enter: ");
    Serial.println("=============================================");
}

// 1. Añadimos primero una función "Manejadora" (Callback) para procesar cada red que encuentre el chip
// IMPORTANTE: Cambiar a AUTOMATIC para que el Photon se conecte al Wi-Fi de tu casa/oficina

// ... (mantén tus definiciones de menús y estados igual) ...

void manejarRedEncontrada(WiFiAccessPoint* ap, void* cookie) {
    // Seguimos imprimiendo en la laptop por USB si está conectada
    Serial.printf("SSID: %-25s | Canal: %-3d | Señal: %d dBm\n", ap->ssid, ap->channel, ap->rssi);

    // NUEVO: Crear un formato de texto corto para enviar por Internet
    char datosRemotos[64];
    snprintf(datosRemotos, sizeof(datosRemotos), "SSID:%s|Ch:%d|RSSI:%d", ap->ssid, ap->channel, ap->rssi);

    // ENVIAR A INTERNET: Publica el evento en la nube de Particle
    // "wifi-detectada" es el nombre del evento, datosRemotos es el mensaje, PRIVATE por seguridad
    Particle.publish("wifi-detectada", datosRemotos, PRIVATE);
    
    // Un pequeño retraso para no saturar la nube (límite de 1 evento por segundo en cuentas gratis)
    delay(1000); 
}

// ... (el resto de tu lógica del menú y loop se mantiene igual) ...
// 2. Modificamos la función principal del escaneo
void ejecutarEscaneoWiFi() {
    Log.info("Escaneando canales Wi-Fi cercanos...");
    Serial.println("\n--- REDES WI-FI DETECTADAS ---");
    
    // Llamamos a WiFi.scan pasándole nuestra función manejadora.
    // El chip de radio se encarga de buscar y dirigir los resultados ahí.
    int redesEncontradas = WiFi.scan(manejarRedEncontrada, NULL);
    
    if (redesEncontradas > 0) {
        Serial.printf("-------------------------------\nTotal de redes impresas: %d\n\n", redesEncontradas);
    } else {
        Serial.println("No se detectaron redes en este escaneo o el radio está ocupado.");
        Serial.println("-------------------------------\n");
    }
}

// Callback obligatorio para procesar cada dispositivo Bluetooth encontrado
void onScanResultBLE(const BleScanResult* scanResult, void* context) {
    BleAddress addr = scanResult->address();
    String name = scanResult->advertisingData().deviceName();
    if (name.length() == 0) name = "[Anónimo]";

    Serial.printf("MAC: %s | RSSI: %d dBm | Nombre: %s\n", addr.toString().c_str(), scanResult->rssi(), name.c_str());
}

// Lógica del escáner Bluetooth
void ejecutarEscaneoBLE() {
    Log.info("Iniciando escaneo BLE (4 segundos)...");
    Serial.println("\n--- DISPOSITIVOS BLE DETECTADOS ---");
    
    BLE.on();
    BLE.setScanTimeout(400); // 400 unidades de 10ms = 4 segundos
    BLE.scan(onScanResultBLE, NULL);
    
    Serial.println("-----------------------------------\n");
}


