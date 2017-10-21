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
    1. The OS stores a page identifier in the PTE and marks it as remote.
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
new-pages queue, the OS can query the FREE_STAT and NEW_STAT ports.

## Limitations
* The current PFA design does not support multiple cores.
  * **Example**: Queues cannot be atomically queried for free space and pushed to.
  * **Example**: PFA directly modifies page table with no regard for locking or other MMUs.
* The PFA does not handle shared pages.

# RISCV Standards
**User Spec**: 2.1 (RV64 only)

**Priv Spec**: 1.10 (Sv39 or Sv48 only)

# PTE
Remote pages use a unique PTE format:

```
63         40                  12             2        1       0
|  Unused  |      Page ID      |  protection  | remote | valid |
```

Fields
* **Valid:** Valid Flag
  * **1** Indicates that this page is valid and shall be interpreted as a normal Sv48 PTE.
  * **0** indicates that this PTE is invalid (the remote bit will be checked).
* **Remote:** Remote memory flag.
  * **1** indicates the page is in remote memory.
  * **0** indicates that this page is simply invalid (access triggers a page fault).
  * _Note_: This is an incompatible change from the RISC-V priviledge spec 1.10
    which specifies that bits 1-63 are don't cares if the valid bit is 0. This
    is compatible in-practice with the current RISC-V Linux implementation.
* **Protection:** Protection bits to use after a page is fetched. These match
  the first 10 bits of a standard PTE.
  * _Note_: This includes a valid bit which may differ from the Remote PTE
    valid bit. If this is 'invalid', the PFA will fetch the remote page, but
    then trigger a page-fault anyway.
* **Page ID:** A unique identifier for this page.
  * Must match a pageID that was evicted and not-yet-fetched.

# MMIO
| Name       | Value      |
| ---------- | ---------  |
| BASE       | 0x10017000 |
| FREE       | BASE       |
| FREE_STAT  | BASE + 8   |
| EVICT      | BASE + 16  |
| EVICT_STAT | BASE + 24  |
| NEW_PGID   | BASE + 32  |
| NEW_VADDR  | BASE + 40  |
| NEW_STAT   | BASE + 48  |

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
Illegal

### Store
Expected Value: Packed eviction uint64 containing pfn and pgid 

Eviction requires two values (packed into a single 8-byte value, see below):
* pfn: Page frame number. This is the physical address of the page shifted 12b
  to the right (since the first 12b are always 0 in page-aligned adresses).
* Page ID:  A unique 28-bit value to be associated with the page. Must be unique
  among all currently evicted pages (pgids may be reused after being seen in
  the newq)

The two values must be packed into a single 8-byte value as follows:
```
63                        36                            0
|         Page ID         |            pfn              |
```

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
is illegal to store to EVICT. Returning EVICT_MAX (size of evict Q) means all
pages have been successfully evicted.

### Store
Illegal

## NEW_PGID
Check which pages have been fetched automatically by the PFA (useful for OS
bookkeeping)

### Load
Returned Value: Page ID of oldest fetched page that has not been reported (FIFO
order).

**Note**: It is illegal to load from an empty new queue. You must check
NEW_STAT before loading from NEW.

**Note**: Unlike EVICT, NEW always reports every fetched page. Since it may be
bounded, it is important for SW to drain this queue periodically. A full new
queue will result in a page-fault being delivered to the OS.

### Store
Illegal

## NEW_VADDR
Same as NEW_PGID but returns the vaddr of fetched pages.

### Load
Returned Value: virtual address of oldest fetched page that has not been
reported (FIFO order).

**Note:** It is up to the user to keep these queues in sync. Ideally they would
both be loaded from at the same time.

### Store
Illegal

## NEW_STAT
Query status of new page queue.

### Load
Returned Value: Number of new pages in the queue.

**Note**: It is undefined which size (NEW_VADDR or NEW_PGID) is being reported.
It is advised to pop both queues together to avoid confusion.
 
### Store
Illegal
