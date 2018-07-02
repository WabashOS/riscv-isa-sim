#include "nic.h"

bool nic_t::load(reg_t addr, size_t len, uint8_t* bytes)
{
      uint16_t counts = 0;
  switch(addr) {
    case NIC_IO_MACADDR:
      if (len != 8) {
        printf("MAC address is 8 bytes, received load of %ld bytes\n", len);
        return false;
      }
      memcpy(bytes, &macaddr, len);
      break;
    case NIC_IO_COUNTS:
      if (len != 2) {
        printf("Counts is a 2 byte field, received load of %ld bytes\n", len);
        return false;
      }
      memcpy(bytes, &counts, len);
      break;
    default:
      printf("Unsupported load from NIC MMIO range (NIC currently only supports limited functionality)."
          "Received request to load %ld bytes from offset %lx.\n",
          len, addr);
      return false;
  }

  return true;
}
      

bool nic_t::store(reg_t addr, size_t len, const uint8_t* bytes)
{
  printf("Unsupported store to NIC MMIO range (NIC currently only supports limited functionality)."
    " Received request to write %ld bytes to offset %lx.\n",
      len, addr);
  return false;
}
