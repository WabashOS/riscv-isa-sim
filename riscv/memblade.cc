#include "memblade.h"
#include "sim.h"
#include <cassert>

bool memblade_t::load(reg_t addr, size_t len, uint8_t* bytes)
{
  // All loads are 32B for now, so just check once
  uint32_t val;
  if(len != 4) {
    memblade_err("Illegal read of length %lu from 0x%lx\n", len, addr);
    return false;
  }
  val = addr;
  memcpy(bytes, &val, len);

  switch(addr) {
    case MB_NREQ:
      /* The whole system is synchronous for now */
      val = 1;
      memcpy(bytes, &val, 4);
      break;
    case MB_REQ:
      return send_request(bytes);
      break;
    case MB_NRESP:
      memcpy(bytes, &nresp, 4);
      break;
    case MB_RESP:
      if(nresp > 0) {
        val = txid - nresp;
        nresp--;
        memcpy(bytes, &val, 4);
      } else {
        return false;
      }
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

  if(addr == MB_OPCODE) {
    // Special case since it's an 1B read
    if(len != 1) {
      memblade_err("Illegal store of length %lu to MB_OPCODE (should be 1B)\n", len);
      return false;
    }
    oc = (mb_opcode_t)(*bytes);
    return true;
  } else {
    // 8B stores
    if(len != 8) {
      memblade_err("Illegal store of length %lu to 0x%lx\n", len, addr);
      return false;
    }
    /* Note that we only validate/parse values when the command is actually
     * submitted by reading MB_RESP */
    switch (addr) {
      case MB_SRC_ADDR:
        memcpy(&src, bytes, 8);
        break;
      case MB_DST_ADDR:
        memcpy(&dst, bytes, 8);
        break;
      case MB_DSTMAC:
        // We ignore this in spike
        break;
      case MB_PAGENO:
        memcpy(&pageno, bytes, 8);
        break;
      default:
        memblade_err("Store to illegal offset: 0x%lx\n", addr);
        return false;
        break;
    }

    return true;
  }
}

bool memblade_t::send_request(uint8_t *bytes) 
{
  bool res = false;

  switch(oc) {
    case MB_OC_PAGE_READ:
      res = page_read();
      break;
    case MB_OC_PAGE_WRITE:
      res = page_write();
      break;
    case MB_OC_WORD_READ:
      res = word_read();
      break;
    case MB_OC_WORD_WRITE:
      res = word_write();
      break;
    case MB_OC_ATOMIC_ADD:
      res = atomic_add();
      break;
    case MB_OC_COMP_SWAP:
      res = comp_swap();
      break;
    default:
      memblade_err("Invalid opcode: %d\n", oc);
      return false;
  }

  memcpy(bytes, &txid, 4);
  txid++;
  nresp++;
  return true;
}

bool memblade_t::page_read(void) 
{
  memblade_info("Page Read (dst=0x%lx, pageno=0x%lx, txid=%u)\n",
      dst, pageno, txid);

  /* Get the destination on the client */
  void *client_page = (void*)sim->addr_to_mem(dst);
  if(client_page == NULL) {
    memblade_err("Invalid destination address: 0x%lx\n", dst);
    return false;
  }
  
  /* Find the page on the memory blade */
  mb_rmem_t::iterator ri = rmem.find(pageno);  
  if(ri == rmem.end()) {
    /* Remote page has never been written to.
     * Technically, we can write anything here. */
    memset(client_page, 0, 4096);
  } else {
    memcpy(client_page, ri->second, 4096);
  }

  return true;
}

bool memblade_t::page_write(void)
{
  memblade_info("Page Write (src=0x%lx, pageno=0x%lx, txid=%u)\n",
      src, pageno, txid);

  uint8_t *rpage = NULL; // remote page
  uint8_t *lpage = NULL; // local page


  lpage = (uint8_t*)sim->addr_to_mem(src);
  if(lpage == NULL) {
    memblade_err("Invalid src address for page write: 0x%lx\n", src);
    return false;
  }

  /* Use existing remote page (if it exists) or allocate new one*/
  mb_rmem_t::iterator ri = rmem.find(pageno);  
  if(ri != rmem.end()) {
    rpage = ri->second;
  } else {
    rpage = new uint8_t[4096];
    rmem.emplace(pageno, rpage);
  }

  memcpy(rpage, lpage, 4096);
  return true;
}

bool memblade_t::word_read(void)
{
  //Parse extended header
  uint64_t *extdata = (uint64_t*)sim->addr_to_mem(src);
  ext.sz = mb_ext_sz(extdata);
  ext.off = mb_ext_off(extdata);
 
  memblade_info("Word Read (size=%u, offset=%u, pageno=0x%lx, txid=%u)\n",
      ext.sz, ext.off, pageno, txid);

  /* Get the destination on the client*/
  uint8_t *host_dst = ((uint8_t*)sim->addr_to_mem(dst));
  if(host_dst == NULL) {
    memblade_err("Invalid destination address: 0x%lx\n", dst);
    return false;
  } 

  /* Get the word from the memory blade */
  mb_rmem_t::iterator ri = rmem.find(pageno);  
  if(ri == rmem.end()) {
    /* Remote page has never been written to.
     * Technically, we can write anything here. */
    memset(host_dst, 0, ext.sz);
  } else {
    memcpy(host_dst, ri->second + ext.off, ext.sz);
  }

  return true;
}

bool memblade_t::word_write(void)
{
  //Parse extended header
  uint64_t *extdata = (uint64_t*)sim->addr_to_mem(src);
  ext.sz = mb_ext_sz(extdata);
  ext.off = mb_ext_off(extdata);
  ext.value = extdata[1];
 
  memblade_info("Word Write (size=%u, offset=%u, value=%lu, pageno=0x%lx, txid=%u)\n",
      ext.sz, ext.off, ext.value, pageno, txid);

  /* Use existing remote page (if it exists) or allocate new one*/
  uint8_t *rpage = NULL; // remote page
  mb_rmem_t::iterator ri = rmem.find(pageno);  
  if(ri != rmem.end()) {
    rpage = ri->second;
  } else {
    rpage = new uint8_t[4096];
    rmem.emplace(pageno, rpage);
  }

  memcpy(rpage + ext.off, &ext.value, ext.sz);
  return true;
}

bool memblade_t::atomic_add(void)
{
  //Parse extended header
  uint64_t *extdata = (uint64_t*)sim->addr_to_mem(src);
  ext.sz = mb_ext_sz(extdata);
  ext.off = mb_ext_off(extdata);
  ext.value = extdata[1];
 
  memblade_info("Atomic Add (size=%u, offset=%u, value=%lu, pageno=0x%lx, txid=%u)\n",
      ext.sz, ext.off, ext.value, pageno, txid);

  /* Use existing remote page (if it exists) or allocate new one*/
  uint8_t *rpage = NULL; // remote page
  mb_rmem_t::iterator ri = rmem.find(pageno);  
  if(ri != rmem.end()) {
    rpage = ri->second;
  } else {
    rpage = new uint8_t[4096];
    rmem.emplace(pageno, rpage);
  }

  /* Get the destination on the client*/
  uint8_t *host_dst = ((uint8_t*)sim->addr_to_mem(dst));
  if(host_dst == NULL) {
    memblade_err("Invalid destination address: 0x%lx\n", dst);
    return false;
  } 

  switch(ext.sz) {
    case 1:
      *host_dst = *(rpage + ext.off);
      *(rpage + ext.off) += (uint8_t)ext.value;
      break;

    case 2:
      *((uint16_t*)host_dst) = *((uint16_t*)(rpage + ext.off));
      *((uint16_t*)(rpage + ext.off)) += (uint16_t)ext.value;
      break;

    case 4:
      *((uint32_t*)host_dst) = *((uint32_t*)(rpage + ext.off));
      *((uint32_t*)(rpage + ext.off)) += (uint32_t)ext.value;
      break;

    case 8:
      *((uint64_t*)host_dst) = *((uint64_t*)(rpage + ext.off));
      *((uint64_t*)(rpage + ext.off)) += (uint64_t)ext.value;
      break;

    default:
      memblade_err("Invalid size field for atomic add. Must be 1,2,4, or 8 bytes (was %d)\n", ext.sz);
      return false;
  }

  return true;
}

/* A generic swap function */
template <class T>
static void genSwap(uint8_t *rval, uint8_t *lval, uint64_t value, uint64_t compValue)
{
  /* Cast everything to the correct type */
  T *t_rval = (T*)rval;
  T *t_lval = (T*)lval;
  T t_value = (T)value;
  T t_compValue = (T)compValue;

  if (*t_rval == t_compValue) {
    *t_rval = t_value;
    *t_lval = (T)1;
  } else {
    *t_lval = (T)0;
  }
}

bool memblade_t::comp_swap(void)
{
  //Parse extended header
  uint64_t *extdata = (uint64_t*)sim->addr_to_mem(src);
  ext.sz = mb_ext_sz(extdata);
  ext.off = mb_ext_off(extdata);
  ext.value = extdata[1];
  ext.compValue = extdata[2];
 
  memblade_info("Comp_Swap (size=%u, offset=%u, value=%lu, compValueue=%lu, pageno=0x%lx, txid=%u)\n",
      ext.sz, ext.off, ext.value, ext.compValue, pageno, txid);

  /* Use existing remote page (if it exists) or allocate new one*/
  uint8_t *rpage = NULL; // remote page
  mb_rmem_t::iterator ri = rmem.find(pageno);  
  if(ri != rmem.end()) {
    rpage = ri->second;
  } else {
    rpage = new uint8_t[4096];
    rmem.emplace(pageno, rpage);
  }

  /* Get the destination on the client*/
  uint8_t *host_dst = ((uint8_t*)sim->addr_to_mem(dst));
  if(host_dst == NULL) {
    memblade_err("Invalid destination address: 0x%lx\n", dst);
    return false;
  }

  switch(ext.sz) {
    case 1:
      genSwap<uint8_t>(rpage + ext.off, host_dst, ext.value, ext.compValue);
      break;

    case 2:
      genSwap<uint16_t>(rpage + ext.off, host_dst, ext.value, ext.compValue);
      break;

    case 4:
      genSwap<uint32_t>(rpage + ext.off, host_dst, ext.value, ext.compValue);
      break;

    case 8:
      genSwap<uint64_t>(rpage + ext.off, host_dst, ext.value, ext.compValue);
      break;

    default:
      memblade_err("Invalid size field for compare/swap. Must be 1,2,4, or 8 bytes (was %d)\n", ext.sz);
      return false;
  }

  return true;
}

