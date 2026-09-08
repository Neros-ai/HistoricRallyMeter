#ifndef I2C_COUNTER_H
#define I2C_COUNTER_H

#include <cstdint>
#include "i_counter.h"

class I2CCounter : public ICounter {
private:
    int i2c_fd;
    int device_address;
    
public:
    I2CCounter(int bus_number, int address);
    ~I2CCounter();
    
    uint32_t readRegister(uint8_t reg) override;
};

#endif // I2C_COUNTER_H
