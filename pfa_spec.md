# Overview
The page-fault accelerator (PFA) is a device for RISC-V based cpus to store
virtual memory pages to remote memory and fetch them automatically on a
page-fault. With a PFA, the latency-sensitive critical path of swapping-in a
page from remote memory is handled in hardware, while more complex behaviors
like page-replacement policies are handled asynchronously by the OS.

## Basic Usage
1. Eviction:
  1. The OS identifies pages that should be stored remotely.
  1. It evicts them explicitly by writing to the evict queue.
  1. The OS marks the remote bit in the relevant PTE
1. Provide Free Frames:
  1. The OS identifies one or more physical frames that can be used to house
     newly fetched pages.
  1. It gives them to the PFA through the free queue.
1. Page Fault:
  1. Application code issues a load/store for the (now remote) page.
  1. The PFA automatically and synchronously brings the page from remote memory
     and stores it in a free frame.
  1. The PFA clears the remote bit in the pte.
  1. The PFA pushes the virtual address of the fetched page to the new page
     queue.
  1. The application is restarted.
1. New Page Management
  1. The OS periodically queries the new page queue and performs any necessary
     bookkeeping.

## Free Frame and New Page Queue Management
The OS should ensure that there are sufficient free frames in the free queue to
ensure smooth operation. If a remote page is requested and there are no free
frames, the PFA will trap to the OS with a conventional page-fault. The OS must
enqueue one or more free-frames before returning from the interrupt. This may
involve evicting pages synchronously in the page-fault handler.

Similarly, the OS needs to drain the new page queue periodically to ensure it
does not overflow. This will also trap to the OS with a conventional page
fault.

The OS can differentiate a regular page-fault interrupt from a “PFA out of
free-frames” interrupt by checking if the requested page has the remote bit set
in its PTE. To tell the difference between a full free-frames queue and full
new-pages queue, the OS can query the FREE_STAT port.

## Limitations
The current PFA design does not support multiple cores.

**Example**: Queues cannot be atomically queried for free space and pushed to.

**Example**: PFA directly modifies page table with no regard for locking or other MMUs.
The PFA does not handle shared pages.

# RISCV Standards
**User Spec**: 2.1 (RV64 only)

**Priv Spec**: 1.10 (Sv39 or Sv48 only)

# PTE Format
**PTE Remote Bit**:  63

**C Constant**:      PTE_REM = (1l << 63)

The PFA adds one additional bit to the Sv48/39 PTE to indicate that a page is
remote. We currently use bit 63 (technically “reserved for hw” in the spec),
which is a bit dodgy if we start using real large-memory systems that use
higher address bits. All bits in the control section (bits 0-9) of the PTE are
already allocated.

**Note**: It may be tempting to use bits 8/9 (reserved for SW) but this will not
work on Linux.

**Note**: The spec requires SW to zero the reserved upper bits of the PTE (63-54).
This is fine so long as it doesn’t clear them while the page is remote.

# MMIO
| Name       | Value     |
| ---------- | --------- |
| BASE       | 0x2000    |
| FREE       | BASE      |
| FREE_STAT  | BASE + 8  |
| EVICT      | BASE + 16 |
| EVICT_STAT | BASE + 24 |
| NEW        | BASE + 32 |


Basic PFA MMIO behavior is described below. Operations marked “Illegal” will
result in a load/store access fault.

## FREE
Provide free frames to the PFA for use when fetching remote pages.

### Load
Illegal

### Store
Expected Value: physical address (paddr) of frame to publish

Write the physical address of an unused memory frame to FREE to publish it for
use in subsequent remote fetches.

The FREE queue is bounded. Users may query FREE_STAT before pushing to ensure
there is sufficient space. Storing to FREE while the queue is full is illegal.

## FREE_STAT
Query status of free queue.

### Load
Returned Value: Number of unused slots in the free queue. Returning 0 means it
is illegal to store to FREE.

### Store
Illegal

## EVICT
Evict pages to remote memory and check if the eviction is complete.

### Load
Returned Value: virtual address (vaddr) of last successfully evicted page OR 0
if a page is currently being evicted (or no page has ever been evicted).

Loading from EVICT is intended for polling for eviction completion. This is
needed because SW must not write to a frame while its page is being evicted.

**Note**: Only the most recently evicted page is returned if many pages are
evicted before EVICT is polled.

**Note**: 0 will always be returned if no page was ever evicted. In this case, a
polling loop will hang.

### Store
Expected Values:
* vaddr of page to evict
* paddr of page frame to evict

Storing to EVICT evicts a page to remote memory. It takes two stores to evict a
page, one to store the page vaddr, and one to store a physical pointer to the
page. There is no way to query which state the EVICT queue is in, SW must ensure
that stores to EVICT always come in pairs. 

Eviction is asynchronous. Multiple pages may be enqueued for eviction
simultaneously. Users may check EVICT_STAT before storing to ensure there is
sufficient room in the queue. Storing to EVICT while full is illegal.

**Note**: In principle, it may be possible for remote memory to be full (or
exceed allocations). It is expected that the OS will track this out of band. A
store to EVICT when remote memory is full is illegal.

## EVICT_STAT
Query status of evict queue.

### Load
Returned Value: Number of unused slots in the evict queue. Returning 0 means it
is illegal to store to EVICT.

### Store
Illegal

## NEW
Check which pages have been fetched automatically by the PFA (useful for OS
bookkeeping)

### Load
Returned Value: vaddr of oldest fetched page that has not been reported (FIFO
order) OR 0 if no page has been evicted but not reported

**Note**: Unlike EVICT, NEW always reports every fetched page. Since it may be
bounded, it is important for SW to drain this queue periodically. A full new
queue will result in a page-fault being delivered to the OS. In practice, this
shouldn’t be an issue if one always pops off the NEW queue when adding to the
FREE queue since they will likely be the same size.

### Store
Illegal

