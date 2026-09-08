#ifndef I_COUNTER_H
#define I_COUNTER_H

#include <cstdint>

// Hardware-abstraction interface for a 32-bit counter source. The concrete
// I2CCounter reads an LSI LS7866C over I2C on the Pi; SimCounter synthesises
// counts for off-target development where /dev/i2c-1 does not exist.
// Implementations must be safe to call repeatedly from the GTK poll loop.
class ICounter {
public:
    virtual ~ICounter() = default;
    virtual uint32_t readRegister(uint8_t reg) = 0;
};

#endif // I_COUNTER_H
