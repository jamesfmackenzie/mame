# Namco C139 Serial Interface Controller — Technical Reference

> **Audience**: Engineers familiar with hardware emulation who are new to this topic.  
> **Scope**: Hardware background, game usage, MAME implementation history, Ridge Racer Full Scale deep-dive, defect analysis, and strategic implementation plan.

---

## Table of Contents

1. [C139 Background](#c139-background)
   - What is the C139 and why does it exist
   - Games that use it
   - Technical reference: bus, serial, RAM, registers, modes
   - Usage across System boards
   - Maturity in MAME master
2. [Enhanced C139 Implementations](#enhanced-c139-implementations)
   - JB implementation
   - SS implementation
   - Comparison and recommendations
3. [Deep Dive: Ridge Racer Full Scale](#deep-dive-ridge-racer-full-scale)
   - Chip usage in ridgeracf
   - Ring topology and data flow
   - Key methods and rendering pipeline
   - Wire format details
4. [Known Issues with Enhanced Implementations](#known-issues-with-enhanced-implementations)
5. [Fixes and Next Steps](#fixes-and-next-steps)
6. [Strategic C139 Implementation Plan](#strategic-c139-implementation-plan)

---

# C139 Background

## What is the C139 and Why Does It Exist

The **Namco C139** is a custom serial interface controller (SIC) chip designed by Namco for use in their arcade hardware from the late 1980s through the late 1990s. Its purpose is to allow multiple arcade PCBs to communicate with each other over a high-speed serial link — enabling **linked multi-cabinet gameplay** (multiple players in separate cabinets sharing game state) and **multi-screen displays** (a single game session spread across multiple physically-adjacent screens).

It sits between the host CPU and an external 9-bit SRAM, managing serial transmission and reception autonomously once configured, and signalling the CPU via interrupt when communication events occur. The CPU reads and writes game data through the C139's RAM window; the chip handles the physical serial framing.

A pin-compatible upgrade, the **C422**, was used on later System 23 hardware, presumably supporting higher clock speeds.

---

## Games That Use the C139

The C139 appears across three Namco hardware platforms:

### System 2 (1987–1993)
| Game | Year | Link Use |
|------|------|----------|
| Final Lap | 1987 | Up to 8 cabinets linked |
| Final Lap 2 | 1990 | Linked |
| Final Lap 3 | 1992 | Linked |
| Four Trax | 1989 | Linked |
| Assault | 1988 | Present on PCB |
| Metal Hawk | 1988 | Present on PCB |
| Suzuka 8 Hours 1 & 2 | 1992–93 | Linked |
| Lucky & Wild | 1992 | Present |
| Many others | — | C139 present but not used for linking |

### System 21 (1988–1993)
| Game | Year | Link Use |
|------|------|----------|
| Winning Run / Suzuka GP / '91 | 1988–91 | Linked |
| Driver's Eyes | 1992 | Multi-screen |
| Starblade | 1991 | Present |
| Air Combat | 1992 | Present |
| Cyber Sled | 1993 | Linked |

### System 22 / Super System 22 (1993–1997)
| Game | Year | Link Use |
|------|------|----------|
| Ridge Racer (all variants) | 1993 | Standard 2-cab link |
| **Ridge Racer Full Scale** | 1993 | **3-screen, unique topology** |
| **Ridge Racer Three Monitor Version** | 1994 | **3-screen, DIP-configured roles** |
| Ridge Racer 2 | 1994 | Linked |
| Rave Racer | 1995 | Linked |
| Cyber Commando | 1994 | Present |
| Ace Driver / Victory Lap | 1994–96 | Linked |
| Alpine Racer 1 & 2 | 1994–96 | Linked |
| Cyber Cycles | 1995 | Linked |
| Dirt Dash | 1995 | Present |
| Time Crisis | 1996 | Present |
| Prop Cycle | 1996 | Present |
| Tokyo Wars | 1996 | Linked |
| Aqua Jet | 1996 | Present |
| Armadillo Racing | 1997 | Linked |

### System 23 (1997+) — C422 variant
Final Furlong 2, Motocross Go!, and other S23 titles use the C422 upgrade.

---

## Technical Reference

### Bus Interface

```
                    ┌────────────────────────────────────┐
                    │           Namco C139               │
Host CPU ──────────►│  14-bit address, 13-bit data       │
                    │  CS-, R/W-, RES-, DTACK-, DT-      │
                    │  12MHz clock, 16MHz clock          │◄── Clocks
                    │  IRQ- (active low output)          │───► CPU interrupt
                    │                                    │
External SRAM ◄────►│  13-bit address, 9-bit data        │
(M5M5179P 9-bit)    │                                    │
                    │  RINGOUT  ─────────────────────────│───► Serial TX
                    │  RINGINA / RINGINK ────────────────│◄─── Serial RX (differential)
                    │  RINGON (from C148 !SRES) ─────────│─── Controls RINGSW
                    └────────────────────────────────────┘
```

Key points:
- **Register writes require 3 bus cycles** before ACK asserts — confirmed by hardware testing with an Arduino jig driving the chip directly.
- The `RINGON` signal (driven by the C148 interrupt controller) controls a physical **ring bypass switch** (`RINGSW`), allowing the ring to be broken in hardware.
- The external SRAM is **9 bits wide** — the 9th bit is the "sync bit" used as a framing/flag signal.

### Serial Lines

The ring topology uses a **daisy-chain** where each PCB's `RINGOUT` connects to the next PCB's `RINGIN`:

```
PCB A ──RINGOUT──► PCB B ──RINGOUT──► PCB C ──RINGOUT──► (back to A)
      ◄──RINGIN──        ◄──RINGIN──        ◄──RINGIN──
```

For Ridge Racer Full Scale (3 screens), the center PCB's `RINGOUT` is **Y-split (electrically branched)** to both side-screen RINGINs. The side screens do not transmit — there is no return path. This is a hardware topology, not a software choice.

### Serial Encoding

- **Format**: 9-N-1 (9 data bits, no parity, 1 stop bit)
- **Speed**: 1 Mbps (default) or 2 Mbps (REG_3 bit-1 selects)
- **Bit order**: MSB first — bit 8 (sync bit) transmitted first, then bits 7–0
- **Frame**: `[START(0)] [bit8] [bit7] [bit6] [bit5] [bit4] [bit3] [bit2] [bit1] [bit0] [STOP(1)]` = 11 line states per word

The **sync bit** (bit 8) is central to interrupt signalling in modes 0x0C and 0x0D (see mode table below). The chip fires an interrupt when a word is received with bit 8 set.

### RAM Layout

The C139's 0x2000-word (9-bit) RAM is divided into two equal halves:

```
┌─────────────────────────────────────────────┐
│  0x0000 – 0x0FFF  │  TX Area (transmit)     │  CPU writes game data here
│                   │  0x1000 words           │  C139 reads and serializes
├─────────────────────────────────────────────┤
│  0x1000 – 0x1FFF  │  RX Area (receive)      │  C139 writes received data here
│                   │  0x1000 words           │  CPU reads game state here
└─────────────────────────────────────────────┘
```

Data written by the CPU is masked to **9 bits** (0x01FF) — the hardware discards bits 9 and above.

### Register Map

Eight 16-bit registers, accessed at word offsets 0–7:

| Offset | Name | Mask | Description |
|--------|------|------|-------------|
| 0 | REG_0 STATUS | 0x0F | Read: chip status flags. Write: clears status / acks interrupt |
| 1 | REG_1 MODE | 0x0F | Operating mode (see mode table) |
| 2 | REG_2 CONTROL | 0x03 | TX control/sync handshake |
| 3 | REG_3 START | 0x03 | TX enable/disable; bit-1 = speed select |
| 4 | REG_4 RXSIZE | 0xFF | Received word count (chip decrements as data arrives) |
| 5 | REG_5 TXSIZE | 0xFF | Transmit word count (chip clears to 0 on send complete) |
| 6 | REG_6 RXOFFSET | 0x1FFF | RX buffer write pointer — **hardware minimum 0x1000** |
| 7 | REG_7 TXOFFSET | 0x1FFF | TX buffer read pointer |

**REG_0 STATUS bit definitions:**
| Bit | Name | Description |
|-----|------|-------------|
| 0 | ERROR | Frame error (receive error) |
| 1 | SYNC | Sync bit received (bit-8 of an incoming word was set) |
| 2 | TXREADY | TXSIZE == 0 (transmit complete or no TX pending) |
| 3 | RXREADY | RXSIZE == 0 (receive complete or no RX pending) |

**Critical hardware constraint**: REG_6 (RXOFFSET) cannot go below 0x1000. Writing a value below 0x1000 results in 0x1000 being OR'd in. For example, writing 0x0800 results in 0x1800 being stored. This is **confirmed real hardware behaviour** from direct chip testing and ensures the RX pointer always stays within the RX half of RAM.

### Power-On Defaults

From direct hardware measurement:

```
REG_0 = 0x000C  (TXREADY + RXREADY both set — both sizes are 0)
REG_1 = 0x000F  (mode 0xF = no interrupt, reset/configuration state)
REG_2 = 0x0000
REG_3 = 0x0000
REG_4 = 0x0000
REG_5 = 0x0000
REG_6 = 0x1000  (RX offset starts at bottom of RX area)
REG_7 = 0x0000
```

Games always initialise to mode 0xF first (no interrupt), configure registers, then switch to their operating mode.

### Mode Register Semantics

REG_1 (MODE) is a 4-bit flag field. The bits are independent:

| Bit | Value 0 | Value 1 |
|-----|---------|---------|
| 0 | Don't include sync bit in TX | Include sync bit in TX data |
| 1 | 1 Mbps / allow sync-bit in RX | 2 Mbps / ignore sync-bit in RX |
| 2 | Fire interrupt on TX complete | Fire interrupt on RX complete |
| 3 | Immediate (fire interrupt instantly if condition met) | Wait (deferred interrupt) |

This gives a logical grouping of modes:

```
Mode  Binary  Int Type   Timing    Condition
0x0   0000    TX int     Instant   RXSIZE==0 OR TXSIZE==0
0x1   0001    TX int     Instant   same, sync bit in TX
0x2   0010    TX int     Instant   same, hispeed
0x3   0011    TX int     Instant   same, sync+hispeed
0x4   0100    RX int     Instant   RXSIZE==0 OR sync bit received
0x5   0101    RX int     Instant   same, sync bit in TX
0x6   0110    RX int     Instant   RXSIZE==0
0x7   0111    RX int     Instant   same
0x8   1000    TX int     Wait      TXSIZE==0
0x9   1001    TX int     Wait      TXSIZE==0, sync bit in TX
0xA   1010    TX int     Wait      TXSIZE==0, hispeed
0xB   1011    TX int     Wait      same
0xC   1100    RX int     Wait      Sync bit received (bit-8 set)  ← ridgeracf
0xD   1101    RX int     Wait      Sync bit received, sync in TX  ← finallap
0xE   1110    No int     —         —
0xF   1111    No int     —         —  ← reset/config state
```

#### Mode 0x0C in Detail (ridgeracf, ridgerac3m)

Mode 0x0C is used by **ridgeracf** and **ridgerac3m** — both 3-screen Ridge Racer installations. No other known Namco title uses it.

- **Interrupt fires when**: A received 9-bit word has bit-8 (the sync bit) set → REG_0 bit-1 (SYNC) becomes set
- **TX control sequence** observed from ROM watchpoints on the center machine:
  ```
  REG_2 = 0x01        ; sync prep
  REG_2 = 0x03        ; sync + TX enable handshake
  REG_3 = 0x01        ; enable TX
  REG_5 = 0x00F2      ; set TX size = 242 words
  REG_1 = 0x000C      ; confirm mode
  REG_3 = 0x00        ; clear/ack
  ```
- The chip clears REG_5 to 0 when transmission completes
- The game loops polling REG_5 — when it reaches 0, it arms the next frame
- Side screens: REG_3 = 0x01, REG_1 = 0x000C written in a loop — no TX, just waiting for sync-bit IRQ

#### Mode 0x0D in Detail (Final Lap, Tokyo Wars, etc.)

Mode 0x0D adds `sync bit in TX` (bit-0 set) to mode 0x0C's RX-interrupt-on-sync behaviour. An additional observed behaviour: when REG_7 (TXOFFSET) points to the RX area (0x1000+), the chip may **automatically relay** received data as TX — effectively acting as a ring repeater in hardware. This is not fully confirmed but is consistent with the ring topology requirements of games like Final Lap.

### Other Technical Notes

- **C422**: Pin-compatible upgrade, higher clock speed. Used on System 23. Functionally equivalent from a software perspective.
- **SCIRQ line**: Routed through the C148 interrupt controller. From the Assault schematic: `SCIRQ → C148 (IRQ4 line)`, `!SCIACK → !BUSACK`.
- **Register write timing**: Three bus cycles required (start assert, ACK, hold). This is important for accurate timing in emulation.
- **Auto-forwarding**: Whether the chip automatically forwards received data on the TX line (ring relay), or whether the game CPU must do this explicitly, is not fully established. Mode 0x0D appears to support automatic relay via REG_7 offset pointing to RX area. Mode 0x0C (ridgeracf) does not use auto-forward — side screens simply don't transmit.

---

## How the C139 Is Used Across System Boards

| Platform | CPU | C139 Address (RAM) | C139 Address (Regs) | Mode Used |
|----------|-----|--------------------|---------------------|-----------|
| System 2 | 68000 | 0x480000–0x483FFF | 0x4A0000–0x4A000F | 0x09, 0x0D |
| System 21 | 68000 | 0xB00000–0xB03FFF | 0xB80000–0xB8000F | 0x09 |
| System 22 | TMS320C25 (master DSP) | 0x20010000–0x20013FFF | 0x20020000–0x2002000F | 0x08, 0x09, 0x0C |
| Super System 22 | TMS320C25 | 0x410000–0x413FFF | 0x420000–0x42000F | 0x09 |
| System 23 | R4650 MIPS | Various | Various | 0x0B, 0x0F (C422) |

---

## C139 Maturity in MAME Master

The current MAME mainline (`master` branch) contains a **stub implementation** that has not been updated since the initial commit. It is effectively non-functional for any linked gameplay:

```cpp
// MAME master namco_c139.cpp — 113 lines total
uint16_t namco_c139_device::status_r()
{
    // STATUS bit?
    return 4;   // Hardcoded: just returns TXREADY to stop games hanging at boot
}
// TODO: Make this to actually work!
```

- No register model (only `status_r` and `noprw` stubs)
- No networking
- No timer or comm tick
- No IRQ callback
- All linked games marked `MACHINE_NODEVICE_LAN` — linking silently disabled

This means **every multi-cabinet Namco game from 1987–1997** (Final Lap, Winning Run, Ridge Racer, and ~40 others) has zero linking support in official MAME.

---

# Enhanced C139 Implementations

## John Bennett Implementation

**Author**: John Bennett  
**Origin**: Independent proof-of-concept, predates SailorSat implementation below
**Header comment**: *"Hacky Proof of Concept (TM)"*  
**Further reading**: https://forum.arcadecontrols.com/index.php?topic=165638.0

John Bennett's initial working attempt to emulate the C139 for multi-cabinet linking. It is a game-specific, proof-of-concept device that successfully drove the research and established the protocol fundamentals, but was never intended for mainline MAME submission.

### Approach

John implemented the C139 as a working network device using **MAME's built-in LAN socket system** (the same mechanism used by Sega Model 1/2 linking). Multiple MAME instances communicate via TCP sockets assigned at launch.

### Architecture

```
MAME Instance A                    MAME Instance B
┌──────────────────┐               ┌──────────────────┐
│  C139 device     │               │  C139 device     │
│  MAME LAN socket │◄──── TCP ────►│  MAME LAN socket │
│  (no ASIO)       │               │  (no ASIO)       │
└──────────────────┘               └──────────────────┘
```

### Technical Details

- **No dedicated comm thread**: Uses MAME's built-in LAN polling, called during normal execution
- **Register handling**: Separate `regs_map` variants per hardware platform (System 2, System 22) — game-specific code paths throughout
- **Frame format**: Custom header prepended to each packet:
  ```
  Byte 0:    (reserved)
  Byte 1:    ID (link node identifier)
  Byte 2:    MSG_COUNTER
  Byte 3:    SIZE_HIGH
  Byte 4:    SIZE_LOW
  Byte 5:    ORIGIN
  Byte 6:    (reserved)
  Byte 7+:   Payload data
  ```
- **Ring topology**: Aware that ridgeracf center is "Y'd" to side screens; side screens do not transmit
- **Known working**: ridgeracf (3-screen), Driver's Eyes, Ridge Racer 2, Rave Racer (2-player, with glitches)
- **Known issues**: Frame synchronisation not working; transmission rate insufficient for 60Hz with 3+ players; packet drops on some games

### Limitations

- Game-specific code branches everywhere — not a clean device model
- Auto-forwarding uncertainty explicitly noted in comments
- Removed Driver's Eyes support partway through development
- Not architected for submission to MAME mainline

---

## SailorSat Implementation

**Author**: Ariane Fugmann (with contributions from Angelo Salese, John Bennett)  
**Based on**: John Bennett's original work, but substantially rewritten  
**Further reading**: https://forum.arcadecontrols.com/index.php/topic,165638.80.html

The SailorSat implementation is a significant rewrite of the John Bennett approach by Ariane Fugmann. It replaces game-specific code paths with a unified register model and moves networking onto a dedicated ASIO thread, making it architecturally much closer to what a submittable MAME device would look like. It is the most mature reference implementation available.

### Approach

Ariane replaced MAME's LAN sockets with a dedicated **ASIO TCP networking thread** running independently of the emulation thread. This gives lower latency, cleaner separation, and allows per-instance port assignment via command-line options.

### Architecture

```
MAME Instance (emulation thread)       Background thread
┌─────────────────────────────┐        ┌──────────────────────────────┐
│  CPU reads/writes C139 RAM  │        │  ASIO io_context             │
│  comm_tick() fires @ 12MHz  │        │  async_accept / async_read   │
│  ─ checks FIFOs ───────────►│──FIFO─►│  async_connect / async_write │
│  ─ fires IRQ if sync seen   │◄─FIFO──│  TCP socket management       │
└─────────────────────────────┘        └──────────────────────────────┘
              │                                    │
              │ TCP (one per direction)            │
              └────────────────────────────────────┘
```

Key structural change: ring forwarding is handled **in the ASIO receive callback** directly — when `m_forward=true`, any bytes received are immediately re-queued for transmission without involving the emulation thread.

### Technical Details

- **Full register model**: All 8 registers with correct masks
- **comm_tick() at 12MHz**: Driven by a MAME timer, evaluates mode conditions and fires IRQ
- **REG_6 starts at 0** (not 0x1000) — no hardware floor enforced on reads
- **sci_de_hack()**: Port assignment function (workaround: DIP switch-based assignment not yet implemented)

**Wire format (send_data)**:
```
Buffer[0]        = linkid (XOR-hash of remote address)
Buffer[1]        = tx_size & 0xFF (size low byte)
Buffer[2]        = (tx_size >> 8) & 0xFF (size high byte)
Buffer[3..N*2+2] = payload: [lo_byte, 0x00] per word (9th bit ignored in TX)
Buffer[N*2+1]   |= 0x01 (sync bit set on last byte of last word)
Buffer[0x1FF]    = 0x01 (frame sentinel)
```

**Wire format (read_data)**:
```
rx_size = buffer[2]<<8 | buffer[1]
rx_offset = m_reg[REG_6_RXOFFSET]  (starts at 0, accumulates)
For each word j:
    m_ram[0x1000 + (rx_offset & 0x0FFF)] = buffer[j*2+1]<<8 | buffer[j*2]  (= 0x00XX)
REG_4_RXSIZE += rx_size   (accumulates, not replaced)
REG_6_RXOFFSET += rx_size (accumulates)
```

**IRQ firing in mode 0x0C**: The sync bit (buffer's last byte has bit-0 set) causes `REG_0_STATUS |= 0x02`, which `comm_tick()` detects to assert the IRQ.

**sci_int_w()**:
```cpp
void namcos22_state::sci_int_w(int state)
{
    // Only relays the CPU interrupt — no render trigger
    int line = 0x04;
    if (m_irq_enabled & line)
        m_maincpu->set_input_line(m_syscontrol[2] & 7, state ? ASSERT_LINE : CLEAR_LINE);
}
```

### What the SailorSat Implementation Improves Over John Bennett's

| Aspect | JB | SS |
|--------|----|----|
| Networking | MAME LAN sockets | ASIO TCP (separate thread) |
| Register model | Game-specific variants | Unified all-8-registers model |
| Forwarding | Explicit per-game logic | ASIO callback (in-band, no emulation thread involvement) |
| Code cleanliness | Game-specific branches | Much cleaner device model |
| Frame drop tolerance | Poor (noted in comments) | Better (larger FIFOs, async I/O) |
| Submittable to MAME | No | Closer, but not yet |

---

## Comparison and Recommendation

SailorSat's is the more mature implementation by a significant margin. It has a cleaner architecture, a proper register model, uses ASIO (which is already in MAME's codebase), and handles forwarding in the network layer rather than the emulation layer.

**However, neither implementation enforces the confirmed hardware floor of REG_6 ≥ 0x1000**, and both have the side-screen rendering defect described below.

**Recommendation**: Use the SailorSat implementation as the foundation for any canonical implementation. John Bennett's version can inform edge cases but should not be the base.

---

# Deep Dive: Ridge Racer Full Scale

## How the C139 Is Used

Ridge Racer Full Scale (`ridgeracf`) is a 3-screen linked arcade installation. Three **identical PCBs** run the same ROM — the only difference is a configuration setting identifying each PCB as center, left, or right. The PCBs communicate using the C139 in mode 0x0C.

Ridge Racer Three Monitor Version (`ridgerac3m`, RRC) uses the same 3-screen mode 0x0C topology. The structural difference between the two is how PCB identity is configured:

| Game | ROM set | PCB role selection | Ports (emulated) |
|------|---------|--------------------|------------------|
| Ridge Racer Full Scale | ridgeracf (RRF2) | `PORT_CONFNAME` — Machine Configuration menu | 15111–15113 |
| Ridge Racer Three Monitor Version | ridgerac3m (RRC) | `PORT_DIPNAME` — physical DIP switches SW2:1/SW2:2 | 15111–15113 |

### ridgerac3m DIP Switch Encoding (SW2:1, SW2:2 — active-low)

| SW2:1 | SW2:2 | Raw value | Role |
|-------|-------|-----------|------|
| OFF (1) | ON  (0) | 0x00010000 | Center/Main (PCB 2) — default |
| ON  (0) | OFF (1) | 0x00020000 | Left (PCB 1) |
| OFF (1) | OFF (1) | 0x00030000 | Right (PCB 3) |

The **center PCB** generates the complete game scene and transmits 242 words of scene state to the two side PCBs every frame. The side PCBs receive this data, use it to render their own perspective of the same scene, and do not transmit anything back.

## Ring Topology and Data Flow

> **Note**: The physical hardware uses a broadcast Y-split where one wire drives two receivers simultaneously. MAME cannot model this directly — it can only create point-to-point TCP connections between processes. The emulated topology therefore uses a relay chain that approximates the physical broadcast. The behaviour from the game's perspective is identical, but the mechanism differs.

### Physical Ring (Real Hardware)

The center's RINGOUT is physically Y-split (soldered/wired) to both side screens:

```
           Center PCB
           RINGOUT ──┬──► Left PCB RINGIN
                     └──► Right PCB RINGIN
           (side screens have no TX path back to center)
```

### Emulated Ring (MAME — 3 processes)

Since MAME can only model point-to-point TCP connections, the Y-split is emulated as a relay chain:

```
Center          Right           Left
(master)        (forwarder)     (slave)
15112→15113 ──► 15113→15111 ──► 15111→15112
                ▲ m_forward=true  ▲ m_left_slave=true
```

Center sends to Right; Right immediately relays to Left (in the ASIO receive callback, without involving the CPU).

### Data Flow Sequence

```
Center CPU                    Right CPU           Left CPU
    │                              │                   │
    │ write scene data to TX RAM   │                   │
    │ REG_5 = 0x00F2 (242 words)   │                   │
    │ REG_3 = 0x01 (TX enable)     │                   │
    │──────── C139 serialises ────►│                   │
    │         (sync bit on last    │──── relayed ─────►│
    │          word → IRQ)         │     immediately   │
    │                              │                   │
    │                     SCI IRQ ─┤          SCI IRQ ─┤
    │                              │                   │
    │                    CPU reads RX RAM              │
    │                    (242 words of scene state)    │
    │                              │                   │
    │                    (triggers rendering)          │
```

## Payload Contents

The 242-word (0xF2) payload transmitted by the center contains the **complete instantaneous game state** needed to render the scene from a different viewpoint. This is **not incremental/delta data** and **not rendering commands**:

| Category | Contents |
|----------|----------|
| Car position | X, Y, Z coordinates (fixed-point) |
| Car orientation | Rotation matrix or equivalent |
| Track position | Current road segment / track progress |
| Other cars | Position and orientation of AI/opponent vehicles |
| Scene state | Active scene identifiers, transition flags |
| Viewport parameters | Camera frustum, lighting, view matrix (the "viewport header") |

The side screens use this data to **compute their own camera perspective** (left or right of the center camera) and then render the scene from that viewpoint using their own local 3D engine.

## Polygon RAM and the Slave List

The received 242-word payload is written into the side screen's **polygon RAM** starting at offset 0x02FF. This forms what we call a **slave list** — a linked list of rendering commands that the simulated slave DSP walks to produce geometry.

```
Polygon RAM layout (side screen, post-receive):
┌──────────────────────────────────────┐
│  0x0000–0x02FE  │ (other data)       │
│  0x02FF         │ command opcode     │ ← slave list starts here
│  0x0300         │ body length (len)  │
│  0x0301–0x0315  │ viewport body      │ 21 words (standard 0x15 format)
│  0x0316         │ 0xFFFF sentinel    │ link
│  0x0317         │ next_ptr           │ → next command
│  ...            │ render primitives  │
└──────────────────────────────────────┘
```

Each command entry: `[opcode (1 word)] [len (1 word)] [body (len words)] [0xFFFF] [next_ptr]`

## Viewport Command Formats

Two viewport formats are observed in ridgeracf:

### Standard Viewport (body_len = 0x15, 21 words)

Used by all SS22 games and by ridgeracf before a scene transition:

```
Word [0x01]: light.ambient | light.power
Word [0x02]: reflection flags | light.dx
Word [0x03]: window_priority | light.dy
Word [0x04]: ? | light.dz
Word [0x05]: vx (screen centre X) | vy (screen centre Y)
Word [0x06]: zoom factor
Word [0x07]: frustum right edge (vr)
Word [0x08]: frustum left edge (vl)
Word [0x09]: frustum upper edge (vu)
Word [0x0A]: frustum lower edge (vd)
Word [0x0B]: border/clip parameter
Words [0x0C–0x14]: 3×3 view rotation matrix (row-major, fixed-point)
```

This is processed by `slavesim_handle_bb0003()` which extracts all camera parameters.

### Extended Viewport (body_len = 0x45 or 0x22)

Used by ridgeracf **after a scene transition** only. The raw `len` word has flag bits set:
- `0x8045` → strip 0x8000 → body_len = 0x45
- `0x4022` → strip 0x4000 → body_len = 0x22

The flag bits are **only stripped on side screens** (`side_screen=true`). The center screen never encounters these in its own polygon RAM.

Structure:
- Words [0x01–0x0B]: **identical layout to the standard 0x15 viewport** (lighting, zoom, frustum)
- Word [0x0C]: **not a valid `vm[0][0]`** — the center writes a non-rotation value here (e.g. 0x000029)
- The `0xFFFF` sentinel and next pointer are at the **0x15-word offset**, not at the full body_len offset

This requires special handling: `slavesim_handle_bb0003()` is called to extract valid parameters, then the view matrix row 0 is checked for degeneracy (|row0|² << 1). If degenerate, row 0 is reconstructed via cross-product of rows 1 and 2.

## Key Methods and Rendering Pipeline

### Methods Reference

| Method | Location | Description |
|--------|----------|-------------|
| `pdp_begin_r()` | `namcos22.cpp` | CPU reads this DSP register to kick the polygon display parser. Sets `m_pdp_render_done=true`, records `m_pdp_frame`, takes polygon RAM snapshot for side screens, runs `pdp_handle_commands()` on SS22. |
| `sci_int_w(state)` | `namcos22.cpp` | C139 IRQ callback. Relays the C139 interrupt line to the CPU. |
| `simulate_slavedsp()` | `namcos22_v.cpp` | Software simulation of the slave DSP. Walks the slave list in polygon RAM from offset 0x02FF, dispatches viewport and geometry commands, submits polygons to the rasteriser queue. |
| `slavesim_handle_bb0003()` | `namcos22_v.cpp` | Processes a viewport command body: extracts lighting, frustum bounds, and 3×3 view matrix into `m_camera_*` and `m_viewmatrix[][]` members. |
| `draw_polygons()` | `namcos22_v.cpp` | Called by `screen_update()`. If `m_pdp_render_done && m_slave_simulation_active`, calls `simulate_slavedsp()` then waits for rasteriser. Also handles stale-frame holdover logic. |
| `screen_update_namcos22()` | `namcos22_v.cpp` | MAME's per-frame video callback. Calls `draw_sprites()`, `draw_polygons()`, `m_poly->render_scene()`, `draw_text_layer()`. |
| `m_pdp_render_done` | `namcos22.h` | Flag: "polygon data is ready to render this frame." Set by `pdp_begin_r()`. Cleared by `draw_polygons()` after render. |
| `m_render_refresh` | `namcos22.h` | Controls stale-frame holdover: if current frame > `m_pdp_frame` and refresh is set, allows `m_pdp_render_done` to be cleared. Always cleared at end of `draw_polygons()`. |
| `m_pdp_frame` | `namcos22.h` | Frame number when the last `pdp_begin_r()` fired. Used by `draw_polygons()` stale-frame logic. |
| `ridgeracf_side_screen()` | `namcos22.cpp` | Returns true if this MAME instance is a ridgeracf side screen (checks `DSW & 0x00030000` for values 0x20000 or 0x30000). |

### Rendering Pipeline Sequence

```
[Per frame — center screen]                 [Per frame — side screen]

CPU executes game code                       CPU executes game code
    │                                            │
    ▼                                            │
CPU writes polygon data                      SCI packet arrives in RAM
    │                                            │ (242 words → polygon RAM 0x02FF+)
    ▼                                            │
CPU reads pdp_begin_r()                      C139 IRQ fires (sync bit seen)
    │                                            │
    │ sets m_pdp_render_done=true                ▼
    │ sets m_pdp_frame=current_frame         sci_int_w() called
    │                                            │ (relays IRQ to CPU only)
    ▼                                            ▼
[Next vblank]                                CPU SCI interrupt service routine
screen_update() called                           │ processes received data
    │                                            │   (whether ISR calls pdp_begin_r()
    ▼                                            │    here is unverified — see Issues)
draw_polygons()                                  │
    │ if (m_pdp_render_done)                 [Next vblank — if m_pdp_render_done set]
    ▼                                           screen_update() called
simulate_slavedsp()                             │
    │ walk slave list                        draw_polygons()
    │ dispatch geometry                         │ if (m_pdp_render_done)
    ▼                                           ▼
render_scene()                               simulate_slavedsp()
    │ rasterise & blit                          ▼
    ▼                                        render_scene()
Frame displayed                                 ▼
                                             Frame displayed
```

## Wire Format Details

### What Goes on the Wire

The 512-byte (0x200 = 0x1FF×2 + header) packet in the SS wire format:

```
Offset  Content
0       linkid (1 byte) — XOR hash of remote address string
1       tx_size low byte
2       tx_size high byte
3..N    payload: for each of 242 words, [lo_byte][0x00]
                 (9th sync bit stripped — emitted as 0)
last-1  last payload byte | 0x01  (sync bit set to trigger IRQ on receiver)
0x1FF   0x01 (frame sentinel)
```

The receiver recovers `m_ram[0x1000 + j] = 0x00XX` for each word — always a 9-bit value with the high byte zero.

### Frame Size

- **242 words** (0xF2) per frame at ~60Hz
- Full 512-byte TCP packet per transmission
- No delta encoding — complete scene state every frame

### What Is Certain vs. What We Invented

| Element | Status | Basis |
|---------|--------|-------|
| 242 words per frame | **Certain** | REG_5=0xF2 observed in ROM traces |
| Mode 0x0C, sync bit IRQ trigger | **Certain** | ROM traces + hardware notes |
| TX RAM 0x0000–0x0FFF, RX RAM 0x1000–0x1FFF | **Certain** | REG_6 floor constraint, schematic |
| REG_6 floor ≥ 0x1000 | **Certain** | Direct hardware test |
| Ring topology (center→right→left) | **Certain** | Physical cabinet wiring + port assignments |
| Slave list at polygon RAM 0x02FF | **Certain** | ROM behaviour + FSPDPWRITE logs |
| Standard 0x15 viewport format | **Certain** | Multiple games, slavesim_handle_bb0003 confirmed |
| Extended 0x45/0x22 viewport format | **Certain (observed)** | ROM-observed flag bits in len words |
| TCP socket transport | **Invented** | No alternative for MAME multi-instance |
| Exact TX condition (reg2==0x03 && reg3==0x00) | **Uncertain** | Approximated from ROM traces |
| Wire packet format (linkid at 0x1FE, size at 0x1FF) | **Invented** | SS implementation design choice |

---

# Known Issues with Enhanced Implementations

## Issue 1: Clipped Roads

### Symptom

For both the John Bennett and SailorSat implementation, side screens (left and right) show the road clipping at the center-camera frustum boundary.

### Root Cause

The rendering pipeline on side screens uses `m_pdp_render_done` as the gate for `simulate_slavedsp()`. The flag must be set **after** the side-camera viewport parameters have been written to polygon RAM.

On the **center screen**, the sequence is correct:
```
CPU writes scene data → reads pdp_begin_r() → m_pdp_render_done=true
→ draw_polygons() → simulate_slavedsp() [with correct center viewport]
```

On **side screens**, `pdp_begin_r()` fires at the normal point in the CPU's execution cycle — but the **SCI data has not arrived yet**. Polygon RAM at 0x02FF still contains whatever was there previously (often center-camera viewport data). The result:
```
CPU reads pdp_begin_r() [too early — SCI data not yet arrived]
→ m_pdp_render_done=true with CENTER viewport in polygon RAM
→ draw_polygons() → simulate_slavedsp() [clips at center frustum]
```

### Why Identical PCBs Matter

Since all three PCBs run identical ROM code, the side screen CPU **also calls `pdp_begin_r()`**. The correct hardware behaviour is almost certainly that the side screen's **SCI interrupt service routine** processes the received data into polygon RAM and then calls `pdp_begin_r()` — making `pdp_begin_r()` the correct trigger on all screens. The current John Bennett and SailorSat implementations fire `pdp_begin_r()` before the SCI data arrives, so it renders with stale/wrong viewport data.

## Issue 2: REG_6 Hardware Floor Not Enforced in SailorSat Implementation

The SailorSat implementation initialises REG_6 to 0 and `reg_r()` returns the plain register value with no 0x1000 OR. The confirmed hardware constraint (REG_6 ≥ 0x1000) is not enforced. This may cause incorrect RX buffer placement in some scenarios.

---

# Fixes and Next Steps

## Fix 1: Clipping Issue

### What to Change

Three additions, all in `namcos22.cpp` / `namcos22.h`:

**Step 1 — Add `ridgeracf_side_screen()` helper to `namcos22.h`:**
```cpp
bool ridgeracf_side_screen() const;
```

**Step 2 — Implement in `namcos22.cpp`:**
```cpp
bool namcos22_state::ridgeracf_side_screen() const
{
    if (strcmp(machine().system().name, "ridgeracf") != 0)
        return false;
    // PCB identity is in DSW bits 17-16:
    //   0x00000 = center
    //   0x20000 = right screen
    //   0x30000 = left screen
    const u32 dsw = ioport("DSW")->read();
    const u32 pcb = dsw & 0x00030000U;
    return (pcb == 0x00020000U) || (pcb == 0x00030000U);
}
```

**Step 3 — Update `sci_int_w()` in `namcos22.cpp`:**
```cpp
void namcos22_state::sci_int_w(int state)
{
    if (state && ridgeracf_side_screen())
    {
        m_pdp_frame = m_screen->frame_number();
        if (m_screen->vblank()) m_pdp_frame++;
        m_pdp_render_done = true;
        m_render_refresh = true;
    }
    int line = 0x04;
    if (m_irq_enabled & line)
    {
        m_irq_state |= line;
        m_maincpu->set_input_line(m_syscontrol[2] & 7, state ? ASSERT_LINE : CLEAR_LINE);
    }
}
```

`m_pdp_frame`, `m_pdp_render_done`, and `m_render_refresh` all already exist in both the John Bennett and SailorSat branches.

### Why This Works

The SCI IRQ fires **after** the 242-word payload is already resident in polygon RAM. At that moment polygon RAM at 0x02FF contains the side-camera viewport (received from center). Setting `m_pdp_render_done=true` here ensures `draw_polygons()` calls `simulate_slavedsp()` with the correct viewport in RAM.

### ISR Trace Result — CONFIRMED (2026-04-12)

Traced via MAME debugger (`./namcos22 ridgeracf -debug`) on a right-screen (forwarder)
instance. Full instruction trace captured from ISR entry to RTE.

**SCI ISR entry point**: `0x15432`

**Confirmed call chain**:
```
0x15432  MOVEM.L ...          ISR prologue (register save)
0x15436  MOVE.W $20020000, D3 reads C139 REG_0 (STATUS) — interrupt ack
         ...
         BSR  0x9D28          branches to data-processing subroutine
           ...
0x9F7C   MOVE.L #$1, $70000000.l  ← writes trigger word to polygon RAM[0]
           ...
         RTS
         ...
         RTE                  ISR returns
```

**Conclusion**: The SCI ISR **does** cause `pdp_begin_r()` to fire, but **indirectly**:

1. ISR reads C139 RX RAM (data already written by our `recv_data()`)
2. ISR copies received data to polygon RAM at offset 0x02FF
3. ISR writes `#$1` to `$70000000` — the master DSP trigger word
4. Master DSP polls `[0x70000000]`, sees non-zero, starts processing cycle
5. Master DSP reads I/O port 2 → `pdp_begin_r()` → `m_pdp_render_done = true`

This is the **"YES" path** from the implementation plan. `sci_int_w()` must **not** be
modified. The `pdp_begin_r()` mechanism is architecturally correct for side screens —
it fires after the ISR has written correct side-camera data to polygon RAM.

The Fix 1 `sci_int_w()` workaround described above is **superseded** by this finding and
should not be implemented. It was based on the assumption that the ISR does not call
`pdp_begin_r()` at all, which is incorrect.

**Implication for canonical implementation**: With a correct C139 that delivers data to
RX RAM before asserting the IRQ (as our Phase 2 implementation does), the side-screen
rendering chain is self-consistent. No driver-level render fix is required. If side
screens still clip to the center frustum after Phases 1–3, the root cause is a MAME CPU
scheduling ordering issue (normal game-loop DSP trigger firing before SCI data arrives),
not a missing `sci_int_w()` fix.

## Fix 2: REG_6 Hardware Floor

In SS `reg_r()`, change:
```cpp
case REG_6_RXOFFSET:
    result = m_reg[offset] | 0x1000;   // enforce hardware minimum
    break;
```
And in `device_reset()`:
```cpp
m_reg[REG_6_RXOFFSET] = 0x1000;   // power-on default
```


---

# Strategic C139 Implementation Plan

## What a Canonical Implementation Should Look Like

Based on confirmed hardware knowledge, a canonical C139 MAME device should:

1. **Model the register set accurately**: All 8 registers with correct masks, correct power-on defaults, and REG_6 hardware floor ≥ 0x1000 enforced.

2. **Implement mode semantics correctly**: `comm_tick()` evaluates the mode register's bit-field and fires the IRQ callback at the correct condition. Mode 0x0C fires on sync-bit received (REG_0 bit-1 set). Mode 0x09 fires when TXSIZE reaches 0.

3. **Keep ridgeracf-specific logic out of the C139 device**: The device should not contain port-number-based topology detection. Topology role (center, forwarder, slave) should be configurable at construction time by the game driver.

4. **Use ASIO TCP for transport**: Already in MAME's codebase, works well, proven by SS.

5. **Clean ring forwarding**: The forward flag (does this instance relay received data?) should be settable via a constructor parameter, not inferred from port number matching.

6. **`sci_int_w` relays the CPU interrupt only**: Any render state management belongs in the game driver's SCI IRQ handler, informed by the ROM ISR trace.

## Target Codebase

The starting point in MAME master is the existing stub at `src/mame/namco/namco_c139.cpp` (113 lines, non-functional). Each phase below replaces stub behaviour with hardware-accurate emulation, verified against the confirmed hardware notes in this document.

## Implementation Plan

### Phase 1 — Register Model

Replace the master stub's hardcoded `return 4` with a full 8-register model.

- [ ] Add `m_reg[8]` array with correct per-register masks: `{0x0F, 0x0F, 0x03, 0x03, 0xFF, 0xFF, 0x1FFF, 0x1FFF}`
- [ ] Set power-on defaults in `device_reset()`: REG_0=0x000C, REG_1=0x000F, REG_2–5=0x0000, REG_6=0x1000, REG_7=0x0000
- [ ] Enforce REG_6 hardware floor: `m_reg[REG_6] = std::max(value, u16(0x1000))` on write; OR 0x1000 on read
- [ ] Implement `reg_r()` / `reg_w()` dispatching all 8 offsets
- [ ] Wire up `regs_map` in each System board's address map (System 2, 21, 22, SS22)
- [ ] Verify against ROM watchpoint traces: ridgera2/raverace init sequence, finallap init sequence

### Phase 2 — Networking Infrastructure

Add ASIO TCP transport (modelled on SS, built fresh against master's ASIO API).

- [ ] Add ASIO `io_context`, background thread, async TCP acceptor/connector
- [ ] Add per-instance port configuration via `set_comm_port(u16 local, u16 remote)`
- [ ] Implement `send_data()`: write TX RAM words as `[lo_byte][0x00]` pairs; append sync bit on last word
- [ ] Implement `recv_data()`: populate RX RAM from received bytes; accumulate REG_4 and REG_6
- [ ] Add `comm_tick()` timer at 12MHz: evaluate mode register, fire IRQ callback when condition met
- [ ] Implement mode 0x0F (no interrupt / config state) and mode 0x0C (IRQ on sync bit received) first — these cover ridgeracf

### Phase 3 — ridgeracf / ridgerac3m Support (Mode 0x0C, 3-Screen Topology) ✅ COMPLETE (2026-04-12)

- [x] Add topology role enum: `role_t::CENTER`, `role_t::FORWARDER`, `role_t::SLAVE`
- [x] Add `set_role(role_t)` configuration method; called from `namcos22_state::machine_reset()`
      (NOTE: must be `machine_reset()`, not `machine_start()` — ports unreadable at init time)
- [x] Center role: transmit 242-word payload each frame; no relay
- [x] Forwarder role (right screen): relay received bytes immediately in ASIO callback without CPU
- [x] Slave role (left screen): receive only; no relay, no transmit
- [x] ridgeracf `PORT_MODIFY("DSW")` SW2:1/SW2:2 replaced with `PORT_CONFNAME` for PCB role
- [ ] Verify ridgeracf boots and center screen renders correctly with 3 MAME instances
- [ ] Verify ridgerac3m with DIP-switch role selection (SW2:1/SW2:2, `PORT_DIPNAME`, ports 15121–15123)

### Phase 4 — Side-Screen Rendering Fix ✅ RESOLVED BY TRACE (2026-04-12)

ISR trace confirmed the YES path (see §ISR Trace Result above). `sci_int_w()` must not
be modified. The rendering chain is architecturally correct — `pdp_begin_r()` fires
after the ISR writes correct side-camera data to polygon RAM.

- [x] Trace the ridgeracf SCI ISR — confirmed ISR entry 0x15432, DSP trigger at 0x9F7C
- [x] Determined: `sci_int_w()` fix is **not** required; existing path is correct
- [ ] Verify left and right screen road geometry clips correctly to the side-camera frustum
      (runtime test pending — if clipping still wrong, investigate DSP scheduling order)

### Phase 5 — Additional Game Modes

Once ridgeracf is solid, extend to the other mode groups:

- [ ] Mode 0x09 (TX interrupt on TXSIZE==0, sync bit in TX): Ace Driver, Victory Lap, Cyber Cycles, Armadillo Racing
- [ ] Mode 0x0D (RX interrupt on sync bit, sync bit in TX, possible auto-relay): Final Lap series, Tokyo Wars, Air Combat, Dirt Dash
- [ ] Mode 0x08 (TX interrupt, no sync bit): Ridge Racer 2, Rave Racer
- [ ] Verify each game's init sequence against the ROM watchpoint traces in the hardware notes
- [ ] Remove `MACHINE_NODEVICE_LAN` from each game as it becomes functional

### Phase 6 — MAME Mainline Submission

- [ ] No game-specific code anywhere in `namco_c139.cpp` — all game differences expressed via configuration
- [ ] All hardware-confirmed behaviours documented with source (hardware test, schematic, ROM trace)
- [ ] All approximated behaviours clearly marked as such (TCP transport, packet format, TX trigger condition)
- [ ] Submit as incremental PRs: register model first, then networking, then ridgeracf, then other games

---

## Summary of Confirmed vs. Approximated Hardware Behaviour

| Behaviour | Confirmed | Approximated |
|-----------|-----------|-------------|
| 9-bit UART, 9-N-1 encoding | ✓ | |
| 8 registers, masks 0x0F/0x03/0xFF/0x1FFF | ✓ | |
| REG_6 hardware floor ≥ 0x1000 | ✓ | |
| Mode bit-2 = RX interrupt, bit-3 = wait | ✓ | |
| Mode 0x0C IRQ on sync bit received | ✓ | |
| ridgeracf TX size = 242 words | ✓ | |
| Ring topology: center→right→left relay | ✓ | |
| Slave list at polygon RAM 0x02FF | ✓ | |
| Standard 0x15 viewport format | ✓ | |
| Extended 0x45/0x22 viewport (flag bits in len) | ✓ (observed) | |
| TCP socket transport | | ✓ |
| Exact TX condition (reg2==0x03 && reg3==0x00) | | ✓ |
| Wire packet format (linkid byte, size bytes) | | ✓ |
| Mode 0x0D auto-relay via REG_7 ≥ 0x1000 | Partial | |
| Side screen SCI ISR triggers pdp_begin_r() indirectly | ✓ (ROM trace 2026-04-12) | |
| ISR trigger mechanism: write #$1 to polygon RAM[0] (0x70000000) | ✓ (ROM trace 0x9F7C) | |
| ISR entry address (ridgeracf RRF2): 0x15432 | ✓ (ROM trace 2026-04-12) | |
