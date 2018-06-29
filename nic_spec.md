The nic is not really supported right now. All it has so far is a MAC address
register so that code that depends on a local MAC address can still work.

# MMIO
| Name          | Value      |
| --------------|------------|
| Base          | 0x10016000 |
| SEND\_REQ     | BASE       |
| RECV\_REQ     | BASE + 8   |
| SEND\_COMP    | BASE + 16  |
| RECV\_COMP    | BASE + 18  |
| COUNTS        | BASE + 20  |
| MACADDR       | BASE + 24  |
| INTMASK       | BASE + 32  |
| CKSUM\_COUNTS | BASE + 36  |
| CKSUM\_RESP   | BASE + 38  |
| CKSUM\_REQ    | BASE + 40  |

## MACADDR
Returns the currently configured MAC address of this NIC
