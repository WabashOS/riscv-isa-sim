#include "pfa.h"
#include "sim.h"
#include "mmu.h"
#include <cassert>

bool pfa_t::load(reg_t addr, size_t len, uint8_t* bytes)
{
  /* Only word-sized values accepted */
  assert(len == sizeof(reg_t));

  switch(addr) {
    case PFA_FREEFRAME:
      fprintf(stderr, "SPIKE: Cannot load from PFA_FREEPAGE");
      return false;
    case PFA_EVICTPAGE:
      fprintf(stderr, "SPIKE: Cannot load from PFA_EVICTPAGE");
      return false;
    case PFA_NEWPAGE:
      reg_t val;
      return pop_newpage(bytes);
    default:
      fprintf(stderr, "SPIKE: Unrecognized load to PFA offset %ld\n", addr);
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
    case PFA_NEWPAGE:
      fprintf(stderr, "SPIKE: cannot store to PFA_NEWFRAME\n");
      return false;
    default:
      fprintf(stderr, "SPIKE: Unrecognized store to PFA offset %ld\n", addr);
      return false;
  }

  /* should not reach */
  return true;
}

reg_t pfa_t::fetch_page(reg_t vaddr, reg_t *host_pte)
{
  /* Get the remote page (if it exists) */
  rmem_t::iterator ri = rmem.find(vaddr);
  if(ri == rmem.end()) {
    /* not found */
    fprintf(stderr, "SPIKE: requested vaddr (%ld) not in remote memory\n", vaddr);
    return 0;
  }

  /* Get free frame */
  if(freeq.empty()) {
    fprintf(stderr, "SPIKE: PFA no available free frames\n");
    return 0;
  } else {
    reg_t paddr = freeq.front();
    freeq.pop();
    pfa_info("fetching vaddr (%lx) into paddr (%lx)\n", vaddr, paddr);

    /* Change the ppn in the pte */
    *host_pte = (*host_pte & ~(~0 << PTE_PPN_SHIFT)) |
                ~(~(paddr >> 12) << PTE_PPN_SHIFT);
    
    /* Copy over remote data into new frame */
    void *host_page = (void*)sim->addr_to_mem(paddr);
    memcpy(host_page, ri->second, 4096);
    
    newq.push(vaddr);
    /* Free the rmem buffer */
    delete [] ri->second;

    return paddr;
  }
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

  pfa_info("Reporting newpage at %lx\n", vaddr);
  memcpy(bytes, &vaddr, sizeof(reg_t));
  return true;
}

bool pfa_t::evict_page(const uint8_t *bytes)
{
  if(evict_vaddr == 0) {
    memcpy(&evict_vaddr, bytes, sizeof(reg_t));
  } else {
    memcpy(&evict_pte, bytes, sizeof(reg_t));

    reg_t *host_pte = (reg_t*)sim->addr_to_mem(evict_pte);
    if(host_pte == NULL) {
      fprintf(stderr, "PFA: paddr of page pte (%lx) not valid\n", evict_pte);
      return false;
    }
    reg_t evict_paddr = (*host_pte >> PTE_PPN_SHIFT) << 12;

    /* Copy page out to remote buffer */
    uint8_t *page_val = new uint8_t[4096];
    void *host_page = (void*)sim->addr_to_mem(evict_paddr);
    if(host_page == NULL) {
      fprintf(stderr, "PFA: Invalid paddr for evicted page (%lx)\n", evict_paddr);
      return false;
    }
    memcpy(page_val, host_page, 4096);

    rmem.insert(std::pair<reg_t, uint8_t*>(evict_vaddr, page_val));

    /* Set remote bit in pte */
    *host_pte |= PTE_REM;
    /* XXX PFA I'm lazy, this will only work for one cpu right now */
    mmu_t *mmu = sim->procs[0]->get_mmu();
    mmu->flush_tlb();

    pfa_info("Evicting page at vaddr %lx (pte=%lx)\n", evict_vaddr, *host_pte);
    /* Reset for next eviction */
    evict_vaddr = 0;
    evict_pte = 0;
  }

  return true;
}



bool pfa_t::free_frame(const uint8_t *bytes)
{
  reg_t paddr;
  memcpy(&paddr, bytes, sizeof(reg_t));
  pfa_info("Adding paddr %lx to list of free frames\n", paddr);
  freeq.push(paddr);
  return true;
}

