#ifndef I2C_COUNTER_H
#define I2C_COUNTER_H

#include <cstdint>
#include "i_counter.h"

class I2CCounter : public ICounter {
private:
    int i2c_fd;
    int device_address;

    uint8_t readRegisterByte(uint8_t reg);
    void writeRegister(uint8_t reg, uint8_t value);

public:
    I2CCounter(int bus_number, int address);
    ~I2CCounter();

    uint32_t readRegister(uint8_t reg) override;

    bool powerLost() override;
    void clearPowerLoss() override;
};

#endif // I2C_COUNTER_H
