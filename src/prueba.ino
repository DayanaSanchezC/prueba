#include "Particle.h"
#include <RF24.h>

// Pines idénticos a tu diagrama físico
#define CE_PIN_A    D6
#define CSN_PIN_A   A2

#define CE_PIN_B    D5
#define CSN_PIN_B   A1

RF24 radioA(CE_PIN_A, CSN_PIN_A);
RF24 radioB(CE_PIN_B, CSN_PIN_B);

const uint64_t direccion_wifi = 0xE8E8F0F0E1LL;
const uint64_t direccion_ble  = 0x42414c4542LL;

int modo_actual = 0; 
bool boton_presionado = false;

unsigned long ultimoReporte = 0;
const unsigned long intervaloReporte = 3000;

// Función manual ultra-segura para leer registros por SPI1
uint8_t leerRegistroManual(rf24_gpio_pin_t csn, uint8_t reg) {
    digitalWrite(csn, LOW);
    delayMicroseconds(5);
    SPI1.transfer(reg & 0x1F);         
    uint8_t resultado = SPI1.transfer(0x00); 
    digitalWrite(csn, HIGH);
    return resultado;
}

void configureAntenna(RF24 &radio, uint8_t canal, rf24_datarate_e velocidad, uint64_t dir, char id) {
    Serial.printf("[SPI1 -> Radio %c] Inicializando begin()...\n", id);
    
    radio.begin(&SPI1); 
    
    // Forzamos los tiempos del bus SPI1 inmediatamente después del begin de la librería
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

void aplicarModoA_Antenas(int modo) {
    if (modo == 0) {
        Serial.println("\n[SISTEMA] >>> CAMBIANDO A MODO BLE (Bluetooth) <<<");
        configureAntenna(radioA, 38, RF24_2MBPS, direccion_ble, 'A'); 
        configureAntenna(radioB, 39, RF24_2MBPS, direccion_ble, 'B'); 
    } else {
        Serial.println("\n[SISTEMA] >>> CAMBIANDO A MODO WIFI <<<");
        configureAntenna(radioA, 76, RF24_2MBPS, direccion_wifi, 'A'); 
        configureAntenna(radioB, 80, RF24_2MBPS, direccion_wifi, 'B'); 
    }
}

void setup() {
    Serial.begin(9600);
    
    // Liberar botón MODE
    System.enableFeature((HAL_Feature)1);
    
    while(!Serial.isConnected()) {
        delay(100);
    }

    Serial.println("\n==================================================");
    Serial.println("SISTEMA DUAL nRF24L01 - FIJADO DE BUS SPI1");
    Serial.println("==================================================");

    // Configurar CSN como salidas estables
    pinMode(CSN_PIN_A, OUTPUT);
    pinMode(CSN_PIN_B, OUTPUT);
    digitalWrite(CSN_PIN_A, HIGH);
    digitalWrite(CSN_PIN_B, HIGH);

    // DELAY CRÍTICO: Damos tiempo a que tus capacitores de 100uF se llenen de energía
    // y estabilicen las antenas antes de activar los relojes lógicos
    delay(1000); 

    SPI1.begin();

    // Chequeo directo de hardware puro
    uint8_t stA = leerRegistroManual(CSN_PIN_A, 0x07);
    uint8_t stB = leerRegistroManual(CSN_PIN_B, 0x07);
    Serial.printf("[CHECK PIN] Registro STATUS Inicial Radio A: 0x%02X\n", stA);
    Serial.printf("[CHECK PIN] Registro STATUS Inicial Radio B: 0x%02X\n", stB);

    aplicarModoA_Antenas(modo_actual);
}

void loop() {
    if (System.buttonPushed()) {
        if (!boton_presionado) {
            boton_presionado = true; 
            modo_actual = !modo_actual; 
            aplicarModoA_Antenas(modo_actual);
        }
    } else {
        boton_presionado = false; 
    }

    char paquete_datos[32];
    if (modo_actual == 0) {
        memset(paquete_datos, 0xAA, sizeof(paquete_datos)); 
    } else {
        memset(paquete_datos, 0xFF, sizeof(paquete_datos)); 
    }
    
    radioA.startFastWrite(&paquete_datos, sizeof(paquete_datos), false);
    radioB.startFastWrite(&paquete_datos, sizeof(paquete_datos), false);

    if (millis() - ultimoReporte >= intervaloReporte) {
        ultimoReporte = millis();

        uint8_t statusA  = leerRegistroManual(CSN_PIN_A, 0x07);
        uint8_t channelA = leerRegistroManual(CSN_PIN_A, 0x05);
        uint8_t statusB  = leerRegistroManual(CSN_PIN_B, 0x07);
        uint8_t channelB = leerRegistroManual(CSN_PIN_B, 0x05);

        Serial.printf("\n[%ds] ----------- LOGS DE HARDWARE -----------\n", (int)(millis()/1000));
        Serial.printf("RADIO A -> STATUS: 0x%02X | Canal: %d -> %s\n", statusA, channelA, (statusA == 0x00 || statusA == 0xFF) ? "[FALLO SPI/ENERGÍA]" : "[OK]");
        Serial.printf("RADIO B -> STATUS: 0x%02X | Canal: %d -> %s\n", statusB, channelB, (statusB == 0x00 || statusB == 0xFF) ? "[FALLO SPI/ENERGÍA]" : "[OK]");
        Serial.println("---------------------------------------------\n");
    }
}