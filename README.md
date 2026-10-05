# ve.direct-concentrator

Puts four VE.Direct serial ports on the trailer's RS-485 pair as four TCP
streams. Bare-metal SAMD21G18A firmware, no Arduino core.

```
VE.Direct 1..4 ──19200──▶ SAMD21 ──RS-485, TMP-v2 @ 500 kbaud──▶ recon (tmptun, tap0)
                          10.55.0.3                               10.55.0.1
```

The unit is the single TMP-v2 slave on the bus (address 0x01, the place the
RP2040 collector held). recon's `tmptun` polls it; every poll and reply can
carry one Ethernet frame, and the unit runs a small IP stack
([smallest_tcp](https://github.com/n9wxu/smallest_tcp)) behind that.

| Service | Where | What |
|---|---|---|
| VE.Direct port n | TCP 5000+n (5001–5004) | Raw bytes from the device, untouched; bytes written go to the device (HEX commands). One client per port. |
| Load switches | TCP 5005 | Line-based control of the two load switches; see below. One client. |
| Maintenance | UDP 5000 | `status`, `out <0-3>`, `app`, `bootloader` — see `tools/vedc.py` |
| 485 monitor | USB CDC, first port | Hex dump of every byte on the bus (`R` received, `T` transmitted) while a host has the port open |
| VE.Direct mock | USB CDC, second port | Bytes "from a device" for any VE.Direct port, for a bench without devices — see below |

Nothing is decoded on the unit: `nc 10.55.0.3 5001` shows exactly what the
first device sends, for recon to parse.

### Load switch stream (TCP 5005)

ASCII lines ending in LF (CRLF is accepted):

| Command | Effect |
|---|---|
| `status` | report the state |
| `out <0-3>` | set both switches: bit 0 = drive 1, bit 1 = drive 2 |
| `on <1\|2>`, `off <1\|2>` | switch one load |

Every line the unit sends is either the state, `out <0-3> sense <mV> <mV>`
(the outputs, then the current-sense voltages of load 1 on A0 and load 2 on
A3, in millivolts at the pin), or `error ...` answering a bad command. A state
line answers each good command, greets each new connection, follows any
output change (including a UDP `out`), and repeats every second while the
stream is otherwise quiet. A client therefore always holds the current state
and currents, and a client that has gone away is found by TCP within seconds. The switches
are off after any reset and keep their state when a client disconnects.

Load 1 is driven from D8 and its current measured on A0, load 2 from D9 and
A3. The readings are millivolts at the pin (12-bit ADC, 16 samples averaged,
0–3.3 V); the volts-to-amps scale of the sense circuit is not applied yet.

```sh
$ nc 10.55.0.3 5005
out 0 sense 2 1
on 1
out 1 sense 2 1
out 1 sense 418 1
```

### LEDs

Each VE.Direct LED blinks with its port's traffic, in either direction (a
device sending its once-a-second text block shows one blink a second): port 1
(RX D12) on A4, port 2 (RX D5) on D11, port 3 (RX SCL) on D13, port 4 (RX A2)
on D6.

The 485 LED (D7) blinks with Ethernet frames crossing the bus, either way; the
master's empty polls do not light it, so an idle link is dark. The USB LED
(the Zero's RX LED, PB03) blinks with bytes on either USB port. The health
LED (the Zero's TX LED, PA27) blinks at
1 Hz while recon polls the unit and at 4 Hz when no poll has come for 2 s;
dark or stuck means the application is not running (the bootloader leaves it
dark).

A lamp test replaces the application when built with
`-DCMAKE_C_FLAGS=-DCFG_LED_TEST=1`: the four port LEDs blink three times fast,
then VE1, VE2, VE3, VE4, 485, drive 1, drive 2, USB and health light in turn,
and the round repeats. The drives are the real load outputs.

### VE.Direct mock (bench)

`tools/vemock.py` (needs pyserial) stands in for the trailer's devices through
the unit's second USB port: SmartSolar MPPT 75/15s on ports 1 and 2, a Phoenix
inverter on port 4, port 3 empty. They share one simulated battery, sun,
clouds and AC load on a compressed day (`--day`, default 600 s). Each device
runs on its own clock — a period within 1 s ±3 %, its own phase, a little
jitter — and its bytes leave at 19200 baud, so the blocks drift past each
other as real ones do. The bytes enter each port's receive ring as if they had
come off the line and take the whole real path: LED, TCP stream, 485, recon.
What a client writes to a port comes back to the mock, which answers a HEX
Ping. `--corrupt 0.05` spoils the checksum of 5 % of blocks, to exercise the
parser on recon (the unit passes them on untouched, as it would a real line
error).

```sh
tools/vemock.py                 # finds the port; --port /dev/cu.usbmodem…3 otherwise
```

### Reply turnaround

The unit waits 1 ms after each poll before it drives the bus
(`CFG_485_TURNAROUND_US`), so the master's transceiver has let go of it.
recon's RSM485PHT needed about 80 µs with the RP2040; an FT232R USB-485
adapter with automatic direction control needs 150–300 µs. One image serves
both, so what goes to recon can be tested on the bench first.

## Building

Needs `arm-none-eabi-gcc`, CMake and Python 3. Dependencies (smallest_tcp,
TinyUSB, CMSIS headers) are fetched at configure time.

```sh
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
cmake --build build          # vedc-stage0.bin, vedc-boot.bin, vedc-app.bin, vedc-factory.bin
cmake --build build --target flash   # Atmel-ICE, whole chip (OpenOCD 0.12+)

# or with probe-rs:
probe-rs download --chip ATSAMD21G18A --chip-erase --binary-format bin \
    --base-address 0 --verify build/vedc-factory.bin && probe-rs reset --chip ATSAMD21G18A

cmake -B build-host && cmake --build build-host && ctest --test-dir build-host
```

The host tests run the 485 link and its network driver under the real IP stack
(ARP, ping, UDP, a TCP open through TMP-v2 frames), hold a whole session with
the load switch stream the same way, and check that an image stamped by
`mkimage.py` passes the firmware's own validity check.

Either way the whole chip is erased first: any Arduino bootloader, and the
staging slot, which the factory image does not reach (a bootloader left staged
there by an earlier update would be installed over the new one at the next
reset). `flash` also clears the BOOTPROT fuse an Arduino bootloader leaves set;
probe-rs cannot, so a chip that had one needs `flash` (or OpenOCD) once.

## Flash layout and updates

| Address | Size | Image | Updated by |
|---|---|---|---|
| 0x00000 | 2 KB | stage0 | SWD only |
| 0x00800 | 16 KB | bootloader | stage0, from the staging slot |
| 0x04800 | 222 KB | application | bootloader, by TFTP |
| 0x3C000 | 16 KB | staging (new bootloader) | application, by TFTP |

Each image carries its kind, length and version in the reserved vector slots
and a CRC-32 at its end (`tools/mkimage.py`).

**stage0** has no networking and is never replaced. On every reset it installs
a valid staged bootloader that differs from the installed one, then starts the
bootloader if valid, else the application. A power cut during the install is
repaired on the next reset, because the staged copy is still there.

**The bootloader** is a TMP-v2 slave with IP, UDP and a TFTP client. With a
valid application it waits 2 s for a `stay` command and then runs the
application; otherwise it fetches `vedc-app.bin` from 10.55.0.1 until the slot
holds a valid image.

**The application** runs under an 8 s watchdog, so one that hangs keeps coming
back through the bootloader's 2 s window. One that runs but has lost the link
— cannot hear the master, or cannot be heard — resets too: every 10 s it ARPs
for recon, and after 10 minutes without an answer (`CFG_LINK_LOSS_RESET_S`)
it resets into the same window. `status` shows the seconds since recon last
answered (`heard`). A reset leaves the loads off.

Updating from recon, with `vedc-tftp.service` (`trailer-monitor/ops/tunnel`)
serving `/srv/vedc` on `tap0` and the two `.bin` files copied there:

```sh
tools/vedc.py app          # application: reset into the bootloader, which fetches it
tools/vedc.py bootloader   # bootloader: the application stages it, stage0 installs it
tools/vedc.py rescue       # application crashing: catch the bootloader window
```

Update one at a time and check `status` in between: a working application can
replace a bad bootloader and a working bootloader can replace a bad
application. Only both being bad at once (or a bad stage0) needs SWD.

What keeps a unit without a debugger reachable:

- An image is checked before anything is erased (kind and length, from its
  first block) and as a whole before it is used (CRC-32). A wrong file is
  refused; a corrupted one fails the CRC and the bootloader fetches again.
- The bootloader fetches until the application slot is valid, every 5 s, so a
  transfer cut off by a power loss or a tunnel restart finishes later — keep
  the last good `vedc-app.bin` in `/srv/vedc`. If nothing was fetched and the
  old application is intact, it starts that instead.
- After a successful fetch both the bootloader and the application wait 3 s
  before resetting, answering a TFTP server that missed the last ACK.
- `rescue` works on any application: a running one resets 200 ms after the
  first `stay` (more `stay`s do not put it off) and the bootloader answers the
  rest; a crashed one is caught at its watchdog reset; one that has lost the
  link within 10 minutes.
- **The risk left is the bootloader.** A bootloader that passes its CRC but
  never starts the application, or never answers, cannot be replaced without
  SWD. Run any new bootloader on the bench (`tools/vemock.py`, a USB-485
  adapter as the master) before sending it to recon, and update the
  bootloader only when it has to change.

If the unit stops answering:

| `status` / what recon sees | What to do |
|---|---|
| `bootloader app-invalid` or `bootloader fetching` | Put a good `vedc-app.bin` in `/srv/vedc`; the bootloader takes it within 5 s |
| No answer, polls unanswered for over 10 min | Not the firmware: check tmptun (500 kbaud), the 485 wiring (A/B), power |
| No answer, unit resets every ~10 min | The application has lost the link: `vedc.py rescue`, with a good `vedc-app.bin` in `/srv/vedc` |
| Answers, but an update misbehaves | `vedc.py app` with the previous `vedc-app.bin` |

All of this was exercised on the bench over the 485 (2026-10-04): updates of
both images, a corrupted image refused and fetched again, a lost last ACK
answered, `rescue` of a running application (0.5 s), and an application that
could hear but not be heard resetting itself and being rescued.

## Configuration

`common/config.h` holds the addresses, ports and file names; `common/board.h`
the pin map (Arduino Zero numbering in the comments); `common/tmp_v2.h` the
wire format, identical to `trailer-monitor/protocol/tmp_v2.h`.

## recon

The link runs at 500 kbaud: `trailer-monitor/protocol/tmp_v2.h` and
`recon/tmptun/tmptun.c` (`B500000`) are set for it. Rebuild `tmptun` on recon
(`make` in `recon/tmptun`) and restart `tmptun.service`; the unit and recon
must change rate together.

Updates need the TFTP service on recon (`trailer-monitor/ops/README.md`:
`dnsmasq-base` and `vedc-tftp.service`) and `tools/vedc.py`. Before the unit
goes on the bus, put the images it runs (`build/vedc-app.bin` and
`build/vedc-boot.bin`, release 4) in `/srv/vedc`, so a fetch always finds a
known-good pair.

Still to change on recon:

- Whatever read the Duo2 through the tunnel at 10.55.0.2 no longer has a path:
  this unit replaces the RP2040 on the bus (`collector/link_health.py` on the
  Duo2 pings recon across it).
