#ifndef __MEMBLADE_H__
#define __MEMBLADE_H__

#include "devices.h"
#include "encoding.h"
#include "stdint.h"

// #define memblade_info(M, ...) fprintf(stderr, "SPIKE Memblade: " M, ##__VA_ARGS__)
#define memblade_info(M, ...)

#define memblade_err(M, ...) fprintf(stderr, "SPIKE Memblade: " M, ##__VA_ARGS__)

#define MB_REQ_ETH_TYPE 0x0408
#define MB_RESP_ETH_TYPE 0x0508
#define MB_DRAFT_VERSION 1

typedef enum mb_opcode {
  MB_OC_PAGE_READ = 0,
  MB_OC_PAGE_WRITE = 1,
  MB_OC_WORD_READ = 2,
  MB_OC_WORD_WRITE = 3,
  MB_OC_ATOMIC_ADD = 4,
  MB_OC_COMP_SWAP = 5,
  MB_OC_LAST
} mb_opcode_t;

/* Expanded extended header, contains all possible fields, though some may be
 * unused depending on the opcode */
typedef struct mb_ext {
  uint8_t sz; /* Size in bytes */
  uint16_t off; /* Offset in bytes */
  uint64_t value; /* Optional value to use */
  uint64_t compValue; /* Value to compare to (for compare/swap) */
} mb_ext_t;

static inline uint8_t mb_ext_sz(uint64_t *extdata)
{
  return (1 << (extdata[0] & 0x3));
}

static inline uint16_t mb_ext_off(uint64_t *extdata)
{
  return ((extdata[0] >> 4) & 0xFFF);
}

/* first=pageno of stored page, second=pointer to 4kb buffer holding page */
typedef std::map<reg_t, uint8_t*> mb_rmem_t;

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

    // Arguments
    mb_opcode_t oc = MB_OC_LAST;
    reg_t src = 0;
    reg_t dst = 0;
    uint64_t pageno = 0;
    mb_ext_t ext = {0};
     
    // Internal State
    uint32_t nresp = 0;
    uint32_t txid = 0;
    mb_rmem_t rmem;

    bool send_request(uint8_t *bytes);

    /* Parse a raw extended header into the expanded mb_ext_t struct.  Assumes
     * that all 3 words of the raw extended header can be safely read, even if
     * they don't contain meaningful data. */
    mb_ext_t parse_ext(uint64_t *extdata);

    bool page_read(void); 
    bool page_write(void);
    bool word_read(void);
    bool word_write(void);
    bool atomic_add(void);
    bool comp_swap(void);
};

#endif
