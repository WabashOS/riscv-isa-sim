#ifndef PFA_H
#define PFA_H
#include <queue>
#include "devices.h"
#include "encoding.h"

// #define pfa_info(M, ...) fprintf(stderr, "SPIKE PFA: " M, ##__VA_ARGS__)
#define pfa_info(M, ...)

#define pfa_err(M, ...) fprintf(stderr, "SPIKE PFA: " M, ##__VA_ARGS__)

/* Register Offsets 
 * PFA_BASE in encoding.h contains the physical address where the device is mapped.
 * The device model is independent of the base address and only sees these
 * offsets (addr in load()/store()). */
#define PFA_NPORTS 8
#define PFA_FREEFRAME 0
#define PFA_FREESTAT  8
#define PFA_EVICTPAGE 16
#define PFA_EVICTSTAT 24
#define PFA_NEWPGID   32
#define PFA_NEWVADDR  40
#define PFA_NEWSTAT   48
#define PFA_INITMEM   56
#define PFA_PORT_LAST 56

/* Human-readable names for MMIO ports. Use PFA_PORT_NAME() to use. */
extern const char* const _pfa_port_names[];
/* Retreive a human-readable name (const char *) for an MMIO port number */
#define PFA_PORT_NAME(PORT) _pfa_port_names[PORT / 8]

/* PFA Sizing */
#define PFA_FREE_MAX  10
#define PFA_NEW_MAX   PFA_FREE_MAX
/* We currently can't model multiple outstanding evictions in spike */
#define PFA_EVICT_MAX 10

typedef uint32_t pgid_t;

typedef enum pfa_err {
  PFA_OK,      //Success
  PFA_NO_FREE, //PFA needs more free frames
  PFA_NO_NEW,  //new page queue is full
  PFA_NO_PAGE, //PFA couldn't find the requested page in remote memory
  PFA_ERR      //Generic unrecoverable error
} pfa_err_t;

/* first=vaddr of stored page, second=pointer to 4kb buffer holding page */
typedef std::map<pgid_t, uint8_t*> rmem_t;

/* Forward declare sim_t to avoid circular dep with sim.h */
class sim_t;

/* Generic public PFA helper functions */

/* Check if a pte refers to a remote page */
#define pte_is_remote(pte) (!(pte & PTE_V) && (pte & PFA_REMOTE))

/* extract the page id from a remote pte */
#define pfa_remote_get_pageid(pte) ((pgid_t)(pte >> PFA_PAGEID_SHIFT))

/* Create a local PTE out of a remote pte and a physical address.
 * Note: destroys pageID, extract it first if you want it */
reg_t pfa_mk_local_pte(reg_t rem_pte, uintptr_t paddr);

/* Page-Fault accelerator device */
class pfa_t : public abstract_device_t {
  public:
    pfa_t(sim_t *host_sim) {
      sim = host_sim;
    }

    /* These are the standard load/store functions from abstract_device_t 
     * They get called when a registered address is loaded/stored */
    bool load(reg_t addr, size_t len, uint8_t* bytes);
    bool store(reg_t addr, size_t len, const uint8_t* bytes);

    /* Retrieve the remote page corresponding to vaddr.
     *  vaddr - vaddr of remote page
     *  host_pte - direct pointer to pte in host memory
     */
    pfa_err_t fetch_page(reg_t vaddr, reg_t* host_pte);

  private:
    /* Pop the most recent new page into bytes.
     * If there is a new page: returns vaddr of new page (FIFO order)
     * If there are no new pages: returns 0*/
    bool pop_newpgid(uint8_t *bytes);
    bool pop_newvaddr(uint8_t *bytes);

    /* Report how many new pages are currently waiting to be processed */
    bool check_newpage(uint8_t *bytes);

    /* Check if there is room in the evict queue. This is an MMIO store
     * response.
     * Args:
     *    bytes - will be overwritten with number of free slots in evict queue
     * Return:
     *  True on success, false on failure
     */
    bool evict_check_size(uint8_t *bytes);

    /* Evict a page. Acts as a state-machine:
     * 1st call: Stores "bytes" as vaddr 
     * 2nd call: Stores "bytes" as paddr of pte and performs eviction
     *           (then resets to initial state)
     * Returns: True on legal operation, False on illegal operation
     */
    bool evict_page(const uint8_t *bytes);

    /* Check if there is room in the free queue. This is an MMIO store
     * response.
     * Args:
     *    bytes - will be overwritten with number of free slots in free queue
     * Return:
     *  True on success, false on failure
     */
    bool free_check_size(uint8_t *bytes);

    /* Enqueu a free frame to be used on the next page fault 
     * bytes: paddr of frame. */
    bool free_frame(const uint8_t *bytes);

    /* Provide a page to the PFA to use for its own purposes */
    bool init_mem(const uint8_t *bytes);

    sim_t *sim;

    std::queue<reg_t> freeq;
    std::queue<pgid_t> new_pgid_q;
    std::queue<reg_t>  new_vaddr_q;
    rmem_t rmem;

    /* This enforces polling for completion in the evict queue */
    bool eviction_in_progress = false;
    pgid_t eviction_pgid;
};
#endif
