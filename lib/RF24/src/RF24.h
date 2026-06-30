#ifndef __RF24_H__
#define __RF24_H__

#include "Particle.h"

class RF24 {
private:
    uint16_t ce_pin;
    uint16_t csn_pin;
    bool wide_band;
    uint8_t payload_size;
    bool ack_payload_available;
    bool dynamic_payloads_enabled;
    uint8_t pipe0_reading_address[5];

protected:
    void csn(bool mode);
    void ce(bool mode);
    uint8_t read_register(uint8_t reg, uint8_t* buf, uint8_t len);
    uint8_t read_register(uint8_t reg);
    uint8_t write_register(uint8_t reg, const uint8_t* buf, uint8_t len);
    uint8_t write_register(uint8_t reg, uint8_t value);
    uint8_t write_payload(const void* buf, uint8_t len, const uint8_t writeType);
    uint8_t read_payload(void* buf, uint8_t len);
    uint8_t flush_rx(void);
    uint8_t flush_tx(void);
    uint8_t get_status(void);

public:
    RF24(uint16_t _cepin, uint16_t _csnpin);
    bool begin(void);
    void startListening(void);
    void stopListening(void);
    bool available(void);
    bool read(void* buf, uint8_t len);
    bool write(const void* buf, uint8_t len);
    void openWritingPipe(uint64_t address);
    void openReadingPipe(uint8_t number, uint64_t address);
    void setPALevel(uint8_t level);
    void setChannel(uint8_t channel);
    void payload_size_set(uint8_t size);
};

// Constantes básicas del nRF24
#define RF24_PA_MIN  0
#define RF24_PA_LOW  1
#define RF24_PA_HIGH 2
#define RF24_PA_MAX  3

#endif