This serves as documenation for the memory blade client device. While the
Spike implementation of this device is intended to behave as similar as
possible to the actual RTL, there are a few caveats that will be called out in
their respective sections. The main difference is that Spike does not have an
actual NIC or memory blade to communicate with. Therefore, the Spike model only
works in loopback mode and ignores network or NIC-specific commands (e.g.
setting the destination MAC always results in loopback, regardless of what you
pass it).

# MMIO
The memblade client is configured using MMIO. Right now, the base address is
hard-coded (see riscv/encoding.h MB\_BASE for the current value). This will
eventualy be managed through the device tree.

The following commands/registers are available (addresses are offsets relative
to MB\_BASE), see below for detailed descriptions of each command.

| Name          | Offset | R/W | Size (B) | Description |
|---------------|--------|-----|----------|-------------|
| MB\_SRC\_ADDR | 0x00   | W   | 8        | Physical page address on the client (usage depends on opcode) |
| MB\_DST\_ADDR | 0x08   | W   | 8        | Physical page address on the client (usage depends on opcode) |
| MB\_DSTMAC    | 0x10   | W   | 8        | MAC address of memory blade server to use (ignored in Spike model) |
| MB\_OPCODE    | 0x16   | W   | 1        | Which operation to perform |
| MB\_PAGENO    | 0x18   | W   | 8        | Physical address on memory blade server for request (usage depends on opcode)  |
| MB\_REQ       | 0x20   | R   | 4        | Request sent notification (contains transaction IDs for requests) |
| MB\_RESP      | 0x24   | R   | 4        | Response received notification (contains transaction ID for response) |
| MB\_NREQ      | 0x28   | R   | 4        | Number of available request slots |
| MB\_NRESP     | 0x2C   | R   | 4        | Number of unread response notifications |

# Usage

## Sending Requests
To send a request you start by filling in the relevant arguments
(SRC/DST\_ADDR, PAGENO) as specified by the operation (see "opcodes" below for
details on arguments). You must then poll MB\_NREQ until you receive a non-zero
value (this register tells you how many more outstanding requests you can
submit before waiting for one to complete). Once you receive a non-zero value
from MB\_NREQ, you can read MB\_REQ which will initiate sending the request and
return a transaction ID.

## Receiving Responses
For requests that expect a response, you may read the MB\_RESP register which
will return the transaction ID of the next available response. A response
indicates that the request with that transaction ID is now complete. This read
will block until there is an available response. To avoid blocking, you may
optionally poll MB\_NRESP which returns the number of available responses.

# Opcodes
| Name | Value |
|------|-------|
| PAGE\_READ  | 0 |
| PAGE\_WRITE | 1 |
| WORD\_READ  | 2 |
| WORD\_WRITE | 3 |
| ATOMIC\_ADD | 4 |
| COMP\_SWAP  | 5 |

For the opcodes below, an argument of "N/A" is unused, you place any value (or
no value) in these registers before sending the request. Note that some
instructions take an extended header. For these instructions, the SRC field
contains the physical address of this extended header. All multi-byte fields
use native byte order.

## PAGE\_READ
Read a page from a remote memory blade. "pageno" must refer to a page that has
previously been written. Reading from a page that has never been written to may
result in arbitrary data.

    CLIENT[DST_ADDR] <- SERVER[pageno]

| Argument | Value |
|----------|-------|
| SRC\_ADDR | N/A |
| DST\_ADDR | Physical address on client to write remote page into (must be page aligned) |
| DSTMAC    | MAC address of remote memory blade |
| OPCODE    | 0 |
| PAGENO    | Physical address on remote memory blade to read from (must be page aligned) |

## PAGE\_WRITE
Copy a page from the client to a remote memory blade.

    SERVER[pageno] <- CLIENT[SRC_ADDR]

| Argument | Value |
|----------|-------|
| SRC\_ADDR | Physical address on client to read the remote page from (must be page aligned) |
| DST\_ADDR | N/A |
| DSTMAC    | MAC address of remote memory blade |
| OPCODE    | 1 |
| PAGENO    | Physical address on remote memory blade to write to (must be page aligned) |

## WORD\_READ
Read a word from a remote memory blade into client memory. Reading from a word
that has never been written to may return arbitrary data.

    CLIENT[DST_ADDR] <- SERVER[pageno + offset]

| Argument | Value |
|----------|-------|
| SRC\_ADDR | Physical address of extended header (see below) |
| DST\_ADDR | Physical address on client to write remote page into (must be word aligned) |
| DSTMAC    | MAC address of remote memory blade |
| OPCODE    | 2 |
| PAGENO    | Physical address of page on remote memory blade to read from (must be page aligned) |

Extended Header:

| Bit Range | Field Name | Description |
|-----------|------------|-------------|
| 0:1       | Size       | Size of element 0->1B, 1->2B, 2->4B, 3->8B |
| 2:3       | Reserved   | Must be 0   |
| 4:15      | Offset     | Offset (in bytes) relative to pageno on the server to read from |

## WORD\_WRITE
Write a word from client memory into a remote memory blade.

    SERVER[pageno + offset] <- Value

| Argument | Value |
|----------|-------|
| SRC\_ADDR | Physical address of extended header (see below) |
| DST\_ADDR | N/A |
| DSTMAC    | MAC address of remote memory blade |
| OPCODE    | 3 |
| PAGENO    | Physical address of page on remote memory blade to write to (must be page aligned) |

Extended Header:

| Bit Range | Field Name | Description |
|-----------|------------|-------------|
| 0:1       | Size       | Size of element 0->1B, 1->2B, 2->4B, 3->8B |
| 2:3       | Reserved   | Must be 0   |
| 4:15      | Offset     | Offset (in bytes) relative to pageno on the server to perform the atomic add |
| 16:63     | Reserved   | Must be 0 |
| 64:127    | Value      | The value to write (must be "size" bytes, up to a maximum of 8B) |


## ATOMIC\_ADD
Atomically add to a remote memory location and return the original value.

    CLIENT[DST_ADDR] = SERVER[pageno + offset]
    SERVER[pageno + offset] <- SERVER[pageno + offset] + Value

| Argument | Value |
|----------|-------|
| SRC\_ADDR | Physical address of extended header (see below) |
| DST\_ADDR | Physical address of client memory to write original value into |
| DSTMAC    | MAC address of remote memory blade |
| OPCODE    | 4 |
| PAGENO    | Physical address of page on remote memory blade to write to (must be page aligned) |

Extended Header: 

| Bit Range | Field Name | Description |
|-----------|------------|-------------|
| 0:1       | Size       | Size of element 0->1B, 1->2B, 2->4B, 3->8B |
| 2:3       | Reserved   | Must be 0   |
| 4:15      | Offset     | Offset (in bytes) relative to pageno on the server to perform the atomic add |
| 16:63     | Reserved   | Must be 0 |
| 64:127    | Value      | The value to add |

## COMP\_SWAP
Atomically compare and swap a value with remote memory. The logical operation is as follows:

    if compValue == SERVER(pageno + offset)
        CLIENT(DST_ADDR) <- 1 
        SERVER(pageno + offset) <- Value
    else
        CLIENT(DST_ADDR) <- 0

| Argument | Value |
|----------|-------|
| SRC\_ADDR | Physical address of extended header (see below) |
| DST\_ADDR | Physical address on client to write success/failure (1/0) to |
| DSTMAC    | MAC address of remote memory blade |
| OPCODE    | 5 |
| PAGENO    | Physical address of page on remote memory blade to swap with (must be page aligned) |

Extended Header:

| Bit Range | Field Name | Description |
|-----------|------------|-------------|
| 0:1       | Size       | Size of element 0->1B, 1->2B, 2->4B, 3->8B |
| 2:3       | Reserved   | Must be 0   |
| 4:15      | Offset     | Offset (in bytes) relative to pageno on the server to perform the atomic add |
| 16:63     | Reserved   | Must be 0 |
| 64:127    | Value      | The value to write to remote memory if the compare succeeds |
| 128:191   | compValue  | The value to compare with remote memory |

