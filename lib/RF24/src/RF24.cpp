#include "RF24.h"

RF24::RF24(uint16_t _cepin, uint16_t _csnpin): ce_pin(_cepin), csn_pin(_csnpin), payload_size(32) {}

void RF24::csn(bool mode) {
    digitalWrite(csn_pin, mode ? HIGH : LOW);
}

void RF24::ce(bool mode) {
    digitalWrite(ce_pin, mode ? HIGH : LOW);
}

bool RF24::begin(void) {
    pinMode(ce_pin, OUTPUT);
    pinMode(csn_pin, OUTPUT);
    ce(false);
    csn(true);

    SPI.begin();
    // Configuración segura de velocidad SPI para el Photon 2 (Dividido para estabilidad)
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));

    // Reset por software de registros básicos del nRF
    write_register(0x00, 0x0C); // CONFIG: Enable CRC, 2 bytes CRC, Power Down
    write_register(0x04, 0x5F); // SETUP_RETR: 1500us delay, 15 retries
    
    return true; 
}

uint8_t RF24::write_register(uint8_t reg, uint8_t value) {
    uint8_t status;
    csn(false);
    status = SPI.transfer(0x20 | (reg & 0x1F));
    SPI.transfer(value);
    csn(true);
    return status;
}

uint8_t RF24::write_register(uint8_t reg, const uint8_t* buf, uint8_t len) {
    uint8_t status;
    csn(false);
    status = SPI.transfer(0x20 | (reg & 0x1F));
    while(len--) { SPI.transfer(*buf++); }
    csn(true);
    return status;
}

uint8_t RF24::write_payload(const void* buf, uint8_t len, const uint8_t writeType) {
    uint8_t status;
    const uint8_t* current = reinterpret_cast<const uint8_t*>(buf);
    csn(false);
    status = SPI.transfer(writeType);
    while(len--) { SPI.transfer(*current++); }
    csn(true);
    return status;
}

bool RF24::write(const void* buf, uint8_t len) {
    write_payload(buf, len, 0xA0); // W_TX_PAYLOAD
    ce(true);
    delayMicroseconds(15);
    ce(false);
    return true; 
}

void RF24::openWritingPipe(uint64_t address) {
    write_register(0x0A, reinterpret_cast<uint8_t*>(&address), 5); // RX_ADDR_P0
    write_register(0x10, reinterpret_cast<uint8_t*>(&address), 5); // TX_ADDR
    write_register(0x11, payload_size); // RX_PW_P0
}

void RF24::setPALevel(uint8_t level) {}
void RF24::setChannel(uint8_t channel) { write_register(0x05, channel); }
void RF24::startListening(void) { write_register(0x00, read_register(0x00) | 0x01); ce(true); }
void RF24::stopListening(void) { ce(false); write_register(0x00, read_register(0x00) & ~0x01); }

uint8_t RF24::read_register(uint8_t reg) {
    uint8_t result;
    csn(false);
    SPI.transfer(0x00 | (reg & 0x1F));
    result = SPI.transfer(0xff);
    csn(true);
    return result;
}