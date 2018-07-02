#include "devices.h"
#include "encoding.h"
#include "stdint.h"

#define NIC_IO_COUNTS  20L
#define NIC_IO_MACADDR 24L

class nic_t : public abstract_device_t {
  public:

    /* These are the standard load/store functions from abstract_device_t 
     * They get called when a registered address is loaded/stored */
    bool load(reg_t addr, size_t len, uint8_t* bytes);
    bool store(reg_t addr, size_t len, const uint8_t* bytes);
  
  private:
    uint64_t macaddr = 0;
};
