# Clavier Matrix Scan Bring-up Protocol

## Build and flash

Configure with Pico SDK and its ARM toolchain installed:

```powershell
cmake -S firmware/clavier -B build/clavier -DPICO_SDK_PATH=C:\path\to\pico-sdk -DPICO_BOARD=pico2
cmake --build build/clavier
```

Flash `build/clavier/clavier.uf2` through BOOTSEL. Open the USB serial port at
115200 baud, 8 data bits, no parity, 1 stop bit.

## Electrical checks

1. With no key pressed, verify L1 through L7 idle high and C1 through C12 high.
2. Probe a row while the firmware is running. It must pulse low once per scan;
   the low phase is at least 5 us. Confirm this for L1..L7.
3. Hold the switch at L1/C1. C1 must go low only while L1 is low. The terminal
   must print exactly one `position 1 note 68 pressed` line after at most 5 ms.
4. Release the same switch. The terminal must print exactly one
   `position 1 released` line after at most 5 ms.
5. Repeat step 3 and step 4 for positions 12, 13, 72, and 84. The reported
   position must equal `(row - 1) * 12 + column`.
6. Make and release the L1/C1 contact repeatedly during fewer than five scan
   cycles. No stable transition may be printed. Hold the contact for five or
   more scans and confirm one press followed by one release.
7. Hold three corners of a matrix rectangle, for example L1/C1, L1/C2, and
   L2/C1. L2/C2 must remain inactive. Each key has a diode with its anode at
   the column and cathode at the row, so the active-low row and pull-up column
   scan direction blocks a ghost-current path.

Record the observed row pulse width and scan period before approving the matrix
polarity for the next phase.