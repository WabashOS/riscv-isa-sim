#ifndef PFA_H
#define PFA_H
#include <queue>
#include "devices.h"
#include "encoding.h"

#define pfa_info(M, ...) printf("SPIKE PFA: " M, ##__VA_ARGS__)
// #define pfa_info(M, ...)

/* Register Offsets 
 * PFA_BASE in encoding.h contains the physical address where the device is mapped.
 * The device model is independent of the base address and only sees these
 * offsets (addr in load()/store()). */
#define PFA_FREEFRAME 0
#define PFA_EVICTPAGE 8
#define PFA_NEWPAGE 16

/* first=vaddr of stored page, second=pointer to 4kb buffer holding page */
typedef std::map<reg_t, uint8_t*> rmem_t;

/* Forward declare sim_t to avoid circular dep with sim.h */
class sim_t;

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
     * Returns:
     *    success: paddr of new frame containing page
     *    failure: 0 (couldn't find page with vaddr)
     * XXX this will crash if there aren't enough free frames, eventually we
     * need to plumb in a trap to the OS in this situation.
     */
    reg_t fetch_page(reg_t vaddr, reg_t* host_pte);

  private:
    /* Pop the most recent new page into bytes.
     * If there is a new page: returns vaddr of new page (FIFO order)
     * If there is no new pages: returns 0*/
    bool pop_newpage(uint8_t *bytes);

    /* Evict a page. Acts as a state-machine:
     * 1st call: Stores "bytes" as vaddr 
     * 2nd call: Stores "bytes" as paddr of pte and performs eviction
     *           (then resets to initial state)
     * Returns: True on legal operation, False on illegal operation
     */
    bool evict_page(const uint8_t *bytes);

    /* Reports the status of the last evicted page. In spike, this always
     * 0 the first time, and vaddr the second to fully test polling.
     *
     * bytes <- vaddr of last evicted page or 0 if eviction in progress.
     */
    bool check_evict_status(uint8_t *bytes);

    /* Enqueu a free frame to be used on the next page fault 
     * bytes: paddr of frame. */
    bool free_frame(const uint8_t *bytes);

    sim_t *sim;

    std::queue<reg_t> freeq;
    std::queue<reg_t> newq;
    rmem_t rmem;

    /* Used to implement state for check_evict_status which needs to pretend
     * that eviction takes time. */
    bool  evict_status = false;

    /* Used to implement the state for evict_page. false->waiting for vaddr.
     * true->waiting for pte */
    bool  evict_page_state = false;

    /* Eviction requires two stores to PFA_EVICTPAGE, first is the vaddr of the
     * page to be evicted, second is paddr of pte. We store them here across
     * calls to pfa.store()
     */
    reg_t evict_pte = 0;
    reg_t evict_vaddr = 0;
};
#endif
