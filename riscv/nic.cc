#include "nic.h"

bool nic_t::load(reg_t addr, size_t len, uint8_t* bytes)
{
  switch(addr) {
    case NIC_IO_MACADDR:
      if (len != 8) {
        printf("MAC address is 8 bytes, received load of %d bytes\n", len);
        return false;
      }
      memcpy(bytes, &macaddr, len);
      break;
    default:
      printf("NIC currently only supports reporting it's MAC address\n");
      return false;
  }

  return true;
}
      

bool nic_t::store(reg_t addr, size_t len, const uint8_t* bytes)
{
  printf("NIC currently only supports reporting it's MAC address\n");
  return false;
}
