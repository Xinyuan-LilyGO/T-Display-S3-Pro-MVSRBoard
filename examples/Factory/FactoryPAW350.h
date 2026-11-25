#pragma once
#include <cstdint>
namespace FactoryPAW350 {
void open();
void close();
// Poll only from loop(), alongside the existing touch/charger I2C clients.
bool poll(uint32_t now,int &dx,int &dy);
bool available();
}
