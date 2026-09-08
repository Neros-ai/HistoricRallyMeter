#ifndef MOCK_I2C_COUNTER_H
#define MOCK_I2C_COUNTER_H

#include <cstdint>
#include <map>
#include <stdexcept>
#include "../i_counter.h"

// Mock I2C Counter for testing without hardware
class MockI2CCounter : public ICounter {
private:
    int device_address;
    std::map<uint8_t, uint32_t> registers;
    bool should_fail = false;
    int read_count = 0;
    
public:
    MockI2CCounter(int bus_number, int address) : device_address(address) {
        (void)bus_number;  // Unused in mock
        // Initialize with default values
        registers[0x07] = 0;
    }
    
    ~MockI2CCounter() override {}

    uint32_t readRegister(uint8_t reg) override {
        if (should_fail) {
            throw std::runtime_error("Mock I2C read failure");
        }
        read_count++;
        auto it = registers.find(reg);
        if (it != registers.end()) {
            return it->second;
        }
        return 0;
    }
    
    // Test helper methods
    void setRegisterValue(uint8_t reg, uint32_t value) {
        registers[reg] = value;
    }
    
    void setFailMode(bool fail) {
        should_fail = fail;
    }
    
    int getReadCount() const {
        return read_count;
    }
    
    void resetReadCount() {
        read_count = 0;
    }
    
    int getAddress() const {
        return device_address;
    }
};

#endif // MOCK_I2C_COUNTER_H
