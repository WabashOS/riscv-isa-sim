#include "memblade.h"
#include "sim.h"
#include <cassert>

bool memblade_t::load(reg_t addr, size_t len, uint8_t* bytes)
{
  // All loads are 32B for now, so just check once
  uint32_t val;
  if(len != 4) {
    memblade_err("Illegal read of length %lu from %lx\n", len, addr);
    return false;
  }
  val = addr;
  memcpy(bytes, &val, len);

  switch(addr) {
    case MB_NREQ:
      memblade_info("Load from MB_NREQ\n");
      break;
    case MB_REQ:
      memblade_info("Load from MB_REQ\n");
      break;
    case MB_NRESP:
      memblade_info("Load from MB_NRESP\n");
      break;
    case MB_RESP:
      memblade_info("Load from MB_RESP\n");
      break;
    default:
      memblade_err("Load from illegal offset: 0x%lx\n", addr);
      return false;
      break;
  }
  return true;
} 

bool memblade_t::store(reg_t addr, size_t len, const uint8_t* bytes)
{
  uint64_t val64;
  uint8_t val8;

  if(addr == MB_OPCODE) {
    // Special case since it's an 1B read
    if(len != 1) {
      memblade_err("Illegal store of length %lu to MB_OPCODE\n", len);
      return false;
    }
    memcpy(&val8, bytes, 1);
    memblade_info("Store of 0x%x to MB_OPCODE\n", val8);
    return true;
  } else {
    // 8B stores
    if(len != 8) {
      memblade_err("Illegal store of length %lu to 0x%lx\n", len, addr);
      return false;
    }
    memcpy(&val64, bytes, 8);
    switch (addr) {
      case MB_SRC_ADDR:
        memblade_info("Store of 0x%lx to MB_SRC_ADDR\n", val64);
        break;
      case MB_DST_ADDR:
        memblade_info("Store 0x%lx to MB_DST_ADDR\n", val64);
        break;
      case MB_DSTMAC:
        memblade_info("Store 0x%lx to MB_DSTMAC\n", val64);
        break;
      case MB_PAGENO:
        memblade_info("Store 0x%lx to MB_PAGENO\n", val64);
        break;
      default:
        memblade_err("Store to illegal offset: 0x%lx\n", addr);
        return false;
        break;
    }

    return true;
  }
}
