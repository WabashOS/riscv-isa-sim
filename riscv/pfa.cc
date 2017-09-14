#include "pfa.h"
#include "sim.h"
#include "mmu.h"
#include <cassert>

/* Generic Helpers */
reg_t pfa_mk_local_pte(reg_t rem_pte, uintptr_t paddr)
{
  reg_t local_pte;
  /* move in protection bits */
  local_pte = rem_pte >> PFA_PROT_SHIFT;
  /* stick in paddr (clear upper bits, then OR in)*/
  local_pte = (local_pte & ~(~0ul << PTE_PPN_SHIFT)) |
              ((paddr >> 12) << PTE_PPN_SHIFT);
 
  return local_pte;
}

bool pfa_t::load(reg_t addr, size_t len, uint8_t* bytes)
{
  /* Only word-sized values accepted */
  assert(len == sizeof(reg_t));

  switch(addr) {
    case PFA_FREEFRAME:
      pfa_err("Cannot load from PFA_FREEPAGE");
      return false;
    case PFA_FREESTAT:
      return free_check_size(bytes);
    case PFA_EVICTPAGE:
      return check_evict_status(bytes);
    case PFA_EVICTSTAT:
      return evict_check_size(bytes);
    case PFA_NEWPAGE:
      return pop_newpage(bytes);
    case PFA_NEWSTAT:
      return check_newpage(bytes);
    default:
      pfa_err("Unrecognized load to PFA offset %ld\n", addr);
      return false;
  }

  /* should not reach */
  return true;
}

bool pfa_t::store(reg_t addr, size_t len, const uint8_t* bytes)
{
  /* Only word-sized values accepted */
  assert(len == sizeof(reg_t));

  switch(addr) {
    case PFA_FREEFRAME:
      return free_frame(bytes);
    case PFA_FREESTAT:
      pfa_err("Cannot store to FREESTAT\n");
      return false;
    case PFA_EVICTPAGE:
      return evict_page(bytes);
    case PFA_EVICTSTAT:
      pfa_err("Cannot store to EVICTSTAT\n");
      return false;
    case PFA_NEWPAGE:
      pfa_err("Cannot store to PFA_NEWFRAME\n");
      return false;
    case PFA_NEWSTAT:
      pfa_err("Cannot store to PFA_NEWSTAT\n");
      return false;
    default:
      pfa_err("Unrecognized store to PFA offset %ld\n", addr);
      return false;
  }

  /* should not reach */
  return true;
}

pfa_err_t pfa_t::fetch_page(reg_t vaddr, reg_t *host_pte)
{
  /* Basic feasibility checks */
  if(freeq.empty()){
    pfa_info("No available free frames\n");
    return PFA_NO_FREE;
  }
  if(newq.size() == PFA_NEW_MAX) {
    pfa_info("No free slots in new page queue\n");
    return PFA_NO_NEW;
  }
  
  /* Get the remote page (if it exists) */
  rmem_t::iterator ri = rmem.find(vaddr & PGMASK);
  if(ri == rmem.end()) {
    /* not found */
    pfa_err("Requested vaddr (0x%lx) not in remote memory\n", vaddr);
    return PFA_NO_PAGE;
  }

  reg_t paddr = freeq.front();
  freeq.pop();

  /* Stick the pageID in the new queue */
  uint64_t pageid = pfa_remote_get_pageid(*host_pte);
  newq.push(pageid);

  /* Assign ppn to pte and make local*/
  *host_pte = pfa_mk_local_pte(*host_pte, paddr);

  pfa_info("fetching vaddr (0x%lx) into paddr (0x%lx), pageid (0x%lx), pte=0x%lx\n",
      vaddr, paddr, pageid, *host_pte);

  /* Copy over remote data into new frame */
  void *host_page = (void*)sim->addr_to_mem(paddr);
  if(host_page == NULL) {
    pfa_err("Bad physical address: %lx\n", paddr);
    return PFA_ERR;
  }
  memcpy(host_page, ri->second, 4096);
  
  /* Free the rmem buffer */
  delete [] ri->second;

  return PFA_OK;
}

bool pfa_t::pop_newpage(uint8_t *bytes)
{
  reg_t vaddr;
  if(newq.empty()) {
    vaddr = 0;
  }  else {
    vaddr = newq.front();
    newq.pop();
  }

  pfa_info("Reporting newpage at 0x%lx\n", vaddr);
  memcpy(bytes, &vaddr, sizeof(reg_t));
  return true;
}

bool pfa_t::check_newpage(uint8_t *bytes)
{
  reg_t nnew = (reg_t)newq.size();
  pfa_info("Reporting %ld new pages\n", nnew);
  memcpy(bytes, &nnew, sizeof(reg_t));
  return true;
}

bool pfa_t::evict_check_size(uint8_t *bytes)
{
  /* We currently can't model the asynchrony of eviction so we just provide
   * an evict q of length 1 and evict synchronously. */
  *((reg_t*)bytes) = 1;
  return true;
}

bool pfa_t::evict_page(const uint8_t *bytes)
{
  if(evict_page_state == false) {
    memcpy(&evict_vaddr, bytes, sizeof(reg_t));
    evict_page_state = true;
  } else {
    memcpy(&evict_paddr, bytes, sizeof(reg_t));

    /* Copy page out to remote buffer */
    uint8_t *page_val = new uint8_t[4096];
    void *host_page = (void*)sim->addr_to_mem(evict_paddr);
    if(host_page == NULL) {
      fprintf(stderr, "SPIKE PFA: Invalid paddr for evicted page (0x%lx)\n", evict_paddr);
      return false;
    }
    memcpy(page_val, host_page, 4096);

    rmem.insert(std::pair<reg_t, uint8_t*>(evict_vaddr & PGMASK, page_val));

    pfa_info("Evicting page at vaddr 0x%lx (paddr=0x%lx)\n", evict_vaddr, evict_paddr);
    evict_page_state = false;

    /* evict status polling is optional, so we reset after each eviction to
     * avoid inconsistent state */
    evict_status = false;
  }

  return true;
}

bool pfa_t::check_evict_status(uint8_t *bytes)
{
  /* Force apps to poll once for eviction completion */
  if(evict_status == false) {
    reg_t retval = 0;
    memcpy(bytes, &retval, sizeof(reg_t));
    evict_status = true;
  } else {
    memcpy(bytes, &evict_vaddr, sizeof(reg_t));
    evict_status = false;
  }

  return true;
}

bool pfa_t::free_check_size(uint8_t *bytes)
{
  *((reg_t*)bytes) = PFA_FREE_MAX - freeq.size();
  return true;
}

bool pfa_t::free_frame(const uint8_t *bytes)
{
  if(freeq.size() <= PFA_FREE_MAX) {
    reg_t paddr;
    memcpy(&paddr, bytes, sizeof(reg_t));
    
    if(!sim->addr_to_mem(paddr)) {
      pfa_err("Invalid paddr for free frame\n");
    }

    pfa_info("Adding paddr 0x%lx to list of free frames\n", paddr);
    freeq.push(paddr);
    return true;
  } else {
    pfa_err("Attempted to push to full free queue\n");
    return false;
  }

}

