#include "pfa.h"
#include "sim.h"
#include "mmu.h"
#include <cassert>

const char* const _pfa_port_names[PFA_NPORTS] = {
  "FREE_FRAME",
  "FREE_STAT",
  "EVICT_PAGE",
  "EVICT_STAT",
  "NEW_PGID",
  "NEW_VADDR",
  "NEW_STAT",
};

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
    case PFA_FREESTAT:
      return free_check_size(bytes);
    case PFA_EVICTSTAT:
      return evict_check_size(bytes);
    case PFA_NEWPGID:
      return pop_newpgid(bytes);
    case PFA_NEWVADDR:
      return pop_newvaddr(bytes);
    case PFA_NEWSTAT:
      return check_newpage(bytes);
    default:
      if(addr % 8 != 0 || addr > PFA_PORT_LAST) {
        pfa_err("Unrecognized load to PFA offset %ld\n", addr);
      } else {
        pfa_err("Cannot load from %s\n", PFA_PORT_NAME(addr));
      }
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

    case PFA_EVICTPAGE:
      return evict_page(bytes);

    default:
      if(addr % 8 != 0 || addr > PFA_PORT_LAST) {
        pfa_err("Unrecognized store to PFA offset %ld\n", addr);
      } else {
        pfa_err("Cannot store to %s\n", PFA_PORT_NAME(addr));
      }
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
    pfa_info("No available free frame for (vaddr=0x%lx)\n", vaddr);
    return PFA_NO_FREE;
  }
  if(new_pgid_q.size() == PFA_NEW_MAX || new_vaddr_q.size() == PFA_NEW_MAX) {
    pfa_info("No free slots in new page queue for (vaddr=0x%lx)\n", vaddr);
    return PFA_NO_NEW;
  }
  
  pgid_t pageid = pfa_remote_get_pageid(*host_pte);

  if(eviction_in_progress && eviction_pgid == pageid) {
    pfa_err("Fetching page before eviction is complete\n");
    return PFA_ERR;
  }

  /* Get the remote page (if it exists) */
  rmem_t::iterator ri = rmem.find(pageid);
  if(ri == rmem.end()) {
    /* not found */
    pfa_err("Requested (vaddr=0x%lx) not in remote memory\n", vaddr);
    return PFA_NO_PAGE;
  }

  reg_t paddr = freeq.front();
  freeq.pop();

  /* Update the new queues */
  new_pgid_q.push(pageid);
  new_vaddr_q.push(vaddr);

  /* Assign ppn to pte and make local*/
  *host_pte = pfa_mk_local_pte(*host_pte, paddr);

  pfa_info("fetching (vaddr=0x%lx) into (paddr=0x%lx), (pgid=%d), (pte=0x%lx)\n",
      vaddr, paddr, pageid, *host_pte);

  /* Copy over remote data into new frame */
  void *host_page = (void*)sim->addr_to_mem(paddr);
  if(host_page == NULL) {
    pfa_err("fetching bad physical address: (paddr=%lx)\n", paddr);
    return PFA_ERR;
  }
  memcpy(host_page, ri->second, 4096);
  
  /* Free the rmem buffer */
  delete [] ri->second;
  rmem.erase(ri);

  return PFA_OK;
}

bool pfa_t::pop_newpgid(uint8_t *bytes)
{
  pgid_t pgid;
  if(new_pgid_q.empty()) {
    return false;
  }  else {
    pgid = new_pgid_q.front();
    new_pgid_q.pop();
  }

  pfa_info("Reporting newpage (pgid=%d)\n", pgid);
  reg_t wide_pgid = (reg_t)pgid;
  memcpy(bytes, &wide_pgid, sizeof(reg_t));
  return true;
}

bool pfa_t::pop_newvaddr(uint8_t *bytes)
{

  reg_t vaddr;
  if(new_vaddr_q.empty()) {
    return false;
  }  else {
    vaddr = new_vaddr_q.front();
    new_vaddr_q.pop();
  }

  pfa_info("Reporting newpage (vaddr=0x%lx)\n", vaddr);
  memcpy(bytes, &vaddr, sizeof(reg_t));
  return true;
}

bool pfa_t::check_newpage(uint8_t *bytes)
{
  reg_t nnew = (reg_t)new_pgid_q.size();
  // pfa_info("Reporting %ld new pages\n", nnew);
  memcpy(bytes, &nnew, sizeof(reg_t));
  return true;
}

bool pfa_t::evict_check_size(uint8_t *bytes)
{
  /* Simulate polling for completion for one cycle only (eviction is actually
   * synchronous) */
  if(eviction_in_progress) {
    *((reg_t*)bytes) = PFA_EVICT_MAX - 1;
    eviction_in_progress = false;
  } else {
      *((reg_t*)bytes) = PFA_EVICT_MAX;
  }
  return true;
}

bool pfa_t::evict_page(const uint8_t *bytes)
{
  uint64_t evict_val;

  if(eviction_in_progress) {
    pfa_err("Evicting again without polling for the previous completion\n");
    return false;
  }

  memcpy(&evict_val, bytes, sizeof(reg_t));

  /* Extract the paddr and pgid. See spec for details of evict_val format */
  uint64_t paddr = (evict_val << 28) >> 16;
  pgid_t pgid  = (evict_val >> 36);

  /* Copy page out to remote buffer */
  uint8_t *page_val = new uint8_t[4096];
  void *host_page = (void*)sim->addr_to_mem(paddr);
  if(host_page == NULL) {
    pfa_err("Invalid paddr for evicted page (paddr=0x%lx)\n", paddr);
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

  eviction_in_progress = true;
  eviction_pgid = pgid;
  pfa_info("Evicting page at (paddr=0x%lx) (pgid=%d)\n", paddr, pgid);

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
      pfa_err("Invalid paddr for free frame: (paddr=0x%lx)\n", paddr);
      return false;
    }

    pfa_info("Adding (paddr=0x%lx) to list of free frames\n", paddr);
    freeq.push(paddr);
    return true;
  } else {
    pfa_err("Attempted to push to full free queue\n");
    return false;
  }
}
