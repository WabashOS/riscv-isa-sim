#ifndef __MEMBLADE_H__
#define __MEMBLADE_H__

#include "devices.h"
#include "encoding.h"
#include "stdint.h"

#define memblade_info(M, ...) fprintf(stderr, "SPIKE Memblade: " M, ##__VA_ARGS__)
// #define memblade_info(M, ...)

#define memblade_err(M, ...) fprintf(stderr, "SPIKE Memblade: " M, ##__VA_ARGS__)

#define MB_REQ_ETH_TYPE 0x0408
#define MB_RESP_ETH_TYPE 0x0508
#define MB_DRAFT_VERSION 1

#define MB_OC_PAGE_READ 0
#define MB_OC_PAGE_WRITE 1
#define MB_OC_WORD_READ 2
#define MB_OC_WORD_WRITE 3
#define MB_OC_ATOMIC_ADD 4
#define MB_OC_COMP_SWAP 5

#define MB_RC_PAGE_OK 0x80
#define MB_RC_NODATA_OK 0x81
#define MB_RC_WORD_OK 0x82
#define MB_RC_ERROR 0x83

#define MB_SRC_ADDR   0x00
#define MB_DST_ADDR   0x08
#define MB_DSTMAC     0x10
#define MB_OPCODE     0x16
#define MB_PAGENO     0x18
#define MB_REQ        0x20
#define MB_RESP       0x24
#define MB_NREQ       0x28
#define MB_NRESP      0x2C
// This should always be 1B past the highest mapped MMIO address
#define MB_LAST       0x2D

/* Forward declare sim_t to avoid circular dep with sim.h */
class sim_t;

struct memblade_request {
	uint8_t version;
	uint8_t opcode;
	uint8_t part_id;
	uint8_t reserved;
	uint32_t xact_id;
	uint64_t pageno;
};

struct memblade_response {
	uint8_t version;
	uint8_t resp_code;
	uint8_t part_id;
	uint8_t reserved;
	uint32_t xact_id;
};

static inline uint64_t memblade_make_exthead(int offset, int size)
{
	return ((offset & 0xfff) << 4) | (size & 0x3);
}

/* Page-Fault accelerator device */
class memblade_t : public abstract_device_t {
  public:
    memblade_t(sim_t *host_sim) {
      sim = host_sim;
    }

    /* These are the standard load/store functions from abstract_device_t 
     * They get called when a registered address is loaded/stored */
    bool load(reg_t addr, size_t len, uint8_t* bytes);
    bool store(reg_t addr, size_t len, const uint8_t* bytes);

  private:
    sim_t *sim;
};

#endif
