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
      pfa_err("Cannot load from PFA_EVICTPAGE");
      return false;
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
  vaddr &= PGMASK;

  /* Basic feasibility checks */
  if(freeq.empty()){
    pfa_info("No available free frame for vaddr 0x%lx\n", vaddr);
    return PFA_NO_FREE;
  }
  if(newq.size() == PFA_NEW_MAX) {
    pfa_info("No free slots in new page queue for vaddr 0x%lx\n", vaddr);
    return PFA_NO_NEW;
  }
  
  pgid_t pageid = pfa_remote_get_pageid(*host_pte);

  /* Get the remote page (if it exists) */
  rmem_t::iterator ri = rmem.find(pageid);
  if(ri == rmem.end()) {
    /* not found */
    pfa_err("Requested vaddr (0x%lx) not in remote memory\n", vaddr);
    return PFA_NO_PAGE;
  }

  reg_t paddr = freeq.front();
  freeq.pop();

  /* Stick the pageID in the new queue */
  newq.push(pageid);

  /* Assign ppn to pte and make local*/
  *host_pte = pfa_mk_local_pte(*host_pte, paddr);

  pfa_info("fetching vaddr (0x%lx) into paddr (0x%lx), pageid (0x%d), pte=0x%lx\n",
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
  rmem.erase(ri);

  return PFA_OK;
}

bool pfa_t::pop_newpage(uint8_t *bytes)
{
  reg_t pgid;
  if(newq.empty()) {
    return false;
  }  else {
    pgid = newq.front();
    newq.pop();
  }

  pfa_info("Reporting newpage id=0x%lx\n", pgid);
  reg_t wide_pgid = (reg_t)pgid;
  memcpy(bytes, &wide_pgid, sizeof(reg_t));
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
  uint64_t evict_val;
  memcpy(&evict_val, bytes, sizeof(reg_t));

  /* Extract the paddr and pgid. See spec for details of evict_val format */
  uint64_t paddr = (evict_val << 28) >> 16;
  pgid_t pgid  = (evict_val >> 36);

  /* Copy page out to remote buffer */
  uint8_t *page_val = new uint8_t[4096];
  void *host_page = (void*)sim->addr_to_mem(paddr);
  if(host_page == NULL) {
    pfa_err("Invalid paddr for evicted page (0x%lx)\n", paddr);
    return false;
  }
  memcpy(page_val, host_page, 4096);

  auto res = rmem.emplace(pgid, page_val);
  if(res.second == false) {
    /* Replacing an existing entry */
    auto ri = res.first;
    delete [] ri->second;
    ri->second = page_val;
  }

  pfa_info("Evicting page at paddr=0x%lx (pgid=%d)\n", paddr, pgid);

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

