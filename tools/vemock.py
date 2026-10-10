#!/usr/bin/env python3
"""Mock VE.Direct devices for the concentrator, fed in over its USB port.

Models the trailer's devices on the unit's VE.Direct ports and sends their
text protocol to the unit's "VE.Direct mock" USB serial port, which puts the
bytes in each port's receive ring as if a device had sent them. From there
they take the real path: port LED, TCP stream 5000+n, the 485 link, recon.

  port 1  SmartSolar MPPT 75/15 #1   (200 W of panel)
  port 2  SmartSolar MPPT 75/15 #2   (150 W, partly shaded)
  port 3  nothing (--fill: a third MPPT, 100 W, so every port carries data)
  port 4  Phoenix Inverter 12 V 500 VA 120 V

One shared 12 V battery ties them together: the sun follows a compressed day
(--day seconds per 24 h), clouds drift, the inverter's AC load has a base, a
fridge compressor cycling and the odd spike, and the chargers move through
bulk, absorption and float as the battery fills.

Each device runs on its own clock, as real ones do: a fixed period drawn from
1 s +/-3 %, a random start, and a few milliseconds of jitter per block, so the
streams drift past one another. Bytes leave each port at 19200 baud.

Bytes a TCP client sends toward a device come back on the same USB port and
are shown; a HEX-protocol Ping (":154") is answered as the device would.

  vemock.py [--port /dev/cu.usbmodemXXXX3] [--day 600] [--seed N] [--corrupt 0.01]

USB packets, both directions: A5 <port 1-4> <length 1-255> <bytes> (app/vemock.h).
Needs pyserial.
"""
import argparse
import math
import random
import sys
import time

import serial
import serial.tools.list_ports

BAUD_BYTES_PER_S = 19200 / 10          # 8N1
SYNC = 0xA5


# ── The shared world ─────────────────────────────────────────────────

class World:
    """Sun, clouds, battery and AC load, advanced in simulated hours."""

    def __init__(self, rng, day_seconds):
        self.rng = rng
        self.day_seconds = day_seconds
        self.hour = 9.5                  # start mid-morning: sun on the panels
        self.cloud = 0.9
        self.soc = 0.55                  # 200 Ah bank
        self.capacity_ah = 200.0
        self.charge_a = 0.0              # into the battery from the chargers
        self.dc_load_a = 0.0             # out of it: inverter and load outputs
        self.ac_va = 30.0
        self.spike_hours = 0.0
        self.new_day = False

    def irradiance(self):
        if not 6.0 < self.hour < 18.0:
            return 0.0
        return math.sin(math.pi * (self.hour - 6.0) / 12.0) * self.cloud

    def voltage(self):
        # LiFePO4-ish rest voltage plus the drop/rise of the current.
        ocv = 12.9 + 0.5 * (self.soc - 0.5) + (0.6 * (self.soc - 0.97) / 0.03 if self.soc > 0.97 else 0)
        return ocv + 0.010 * (self.charge_a - self.dc_load_a)

    def charge_state(self):
        """Victron CS for the chargers: 3 bulk, 4 absorption, 5 float."""
        return 3 if self.soc < 0.90 else 4 if self.soc < 0.98 else 5

    def advance(self, seconds):
        hours = seconds * 24.0 / self.day_seconds
        self.hour += hours
        self.new_day = self.hour >= 24.0
        self.hour %= 24.0
        self.cloud = min(1.0, max(0.35, self.cloud + self.rng.gauss(0, 0.02)))
        # AC load: base, fridge compressor (about a third of the time), spikes.
        fridge = 65.0 if (self.hour * 3.0) % 1.0 < 0.35 else 0.0
        if self.spike_hours > 0:
            self.spike_hours -= hours
        elif self.rng.random() < 0.5 * hours:
            self.spike_hours = 0.05      # a kettle, a hair dryer: ~3 simulated minutes,
                                         # about one every two simulated hours
        spike = 280.0 if self.spike_hours > 0 else 0.0
        self.ac_va = 25.0 + fridge + spike + self.rng.uniform(-2, 2)
        self.soc = min(1.0, max(0.05, self.soc
                                + (self.charge_a - self.dc_load_a) * hours / self.capacity_ah))


# ── Devices ──────────────────────────────────────────────────────────

def block(fields):
    """One text-protocol block, its Checksum byte making the sum 0 mod 256."""
    text = "".join(f"\r\n{label}\t{value}" for label, value in fields) + "\r\nChecksum\t"
    data = text.encode("latin-1")
    return data + bytes([(-sum(data)) & 0xFF])


class Device:
    def __init__(self, port, name, rng, now):
        self.port, self.name = port, name
        self.period = 1.0 * (1.0 + rng.uniform(-0.03, 0.03))
        self.rng = rng
        self.next_start = now + rng.uniform(0.0, self.period)
        self.pending = b""               # the block on the line
        self.started = 0.0
        self.sent = 0
        self.blocks = 0

    def due(self, now, world, corrupt):
        """Bytes this device's line has carried since the last call."""
        if not self.pending and now >= self.next_start:
            self.pending = self.fields_block(world)
            if corrupt and self.rng.random() < corrupt:
                self.pending = self.pending[:-1] + bytes([(self.pending[-1] + 1) & 0xFF])
            self.started, self.sent = self.next_start, 0
            self.next_start += self.period + self.rng.uniform(-0.005, 0.005)
            self.blocks += 1
        if not self.pending:
            return b""
        upto = min(len(self.pending), int((now - self.started) * BAUD_BYTES_PER_S))
        out = self.pending[self.sent:upto]
        self.sent = upto
        if upto == len(self.pending):
            self.pending = b""
        return out

    def hex_command(self, line):
        """A HEX-protocol line from the client: answer a Ping."""
        if line.upper() == ":154":
            version = 0x4000 | int(str(self.firmware), 16)
            payload = [5, version & 0xFF, version >> 8]
            payload.append((0x55 - sum(payload)) & 0xFF)
            return (":5" + "".join(f"{b:02X}" for b in payload[1:]) + "\n").encode()
        return b""


class Mppt(Device):
    firmware = 164

    def __init__(self, port, name, rng, now, serial_number, panel_w, load_ma):
        super().__init__(port, name, rng, now)
        self.serial_number, self.panel_w, self.load_ma = serial_number, panel_w, load_ma
        self.ppv = 0.0
        self.total = 3649 + rng.randrange(500)   # H19, 0.01 kWh
        self.today, self.max_today = 0.0, 0
        self.yesterday, self.max_yesterday = 43, 123
        self.day = 150

    def update(self, world, seconds):
        """Power this charger can deliver now, and its yield bookkeeping."""
        available = self.panel_w * world.irradiance() * self.rng.uniform(0.97, 1.0)
        state = world.charge_state()
        if state == 4:
            available = min(available, 40.0 + self.panel_w * 0.1)
        elif state == 5:
            available = min(available, 8.0)
        self.ppv = min(available, 15.0 * world.voltage() / 0.97)   # 15 A charger
        hours = seconds * 24.0 / world.day_seconds
        self.today += self.ppv * hours / 10.0                     # 0.01 kWh units
        self.max_today = max(self.max_today, int(self.ppv))
        if world.new_day:
            self.total += int(self.today)
            self.yesterday, self.max_yesterday = int(self.today), self.max_today
            self.today, self.max_today = 0.0, 0
            self.day += 1

    def fields_block(self, world):
        v = world.voltage()
        night = self.ppv < 1.0
        i = self.ppv * 0.97 / v - self.load_ma / 1000.0
        vpv = self.rng.uniform(0.2, 2.0) if night else \
            (36.0 if world.charge_state() != 3 else 30.5 + 2.0 * world.irradiance())
        return block([
            ("PID", "0xA075"),
            ("FW", str(self.firmware)),
            ("SER#", self.serial_number),
            ("V", str(round(v * 1000))),
            ("I", str(round(i * 1000))),
            ("VPV", str(round(vpv * 1000))),
            ("PPV", str(round(self.ppv))),
            ("CS", "0" if night else str(world.charge_state())),
            ("MPPT", "0" if night else "2" if world.charge_state() == 3 else "1"),
            ("OR", "0x00000001" if night else "0x00000000"),
            ("ERR", "0"),
            ("LOAD", "ON" if self.load_ma else "OFF"),
            ("IL", str(self.load_ma)),
            ("H19", str(self.total + int(self.today))),
            ("H20", str(int(self.today))),
            ("H21", str(self.max_today)),
            ("H22", str(self.yesterday)),
            ("H23", str(self.max_yesterday)),
            ("HSDS", str(self.day)),
        ])


class Phoenix(Device):
    firmware = 114

    def __init__(self, port, name, rng, now, serial_number):
        super().__init__(port, name, rng, now)
        self.serial_number = serial_number

    def dc_amps(self, world):
        return (world.ac_va * 0.95 / 0.90 + 6.0) / world.voltage()   # pf, efficiency, idle

    def fields_block(self, world):
        v = world.voltage()
        low = v < 11.2
        ac_v = 120.0 + self.rng.uniform(-0.3, 0.3)
        return block([
            ("PID", "0xA259"),
            ("FW", f"{self.firmware:04d}"),
            ("SER#", self.serial_number),
            ("MODE", "2"),
            ("CS", "9"),
            ("AC_OUT_V", str(round(ac_v * 100))),
            ("AC_OUT_I", str(round(world.ac_va / ac_v * 10))),
            ("AC_OUT_S", str(round(world.ac_va))),
            ("V", str(round(v * 1000))),
            ("AR", "1" if low else "0"),
            ("WARN", "1" if low else "0"),
            ("OR", "0x00000000"),
        ])


# ── USB transport ────────────────────────────────────────────────────

def find_port():
    """The unit's mock port: the second CDC interface of 1209:0001."""
    ours = [p for p in serial.tools.list_ports.comports() if (p.vid, p.pid) == (0x1209, 0x0001)]
    named = [p for p in ours if "mock" in f"{p.interface} {p.description}".lower()]
    if named:
        return named[0].device
    if len(ours) == 2:
        return sorted(p.device for p in ours)[1]
    sys.exit("cannot find the unit's VE.Direct mock port; give --port "
             f"(saw: {', '.join(p.device for p in ours) or 'no 1209:0001 ports'})")


def packets(port, data):
    for at in range(0, len(data), 255):
        piece = data[at:at + 255]
        yield bytes([SYNC, port, len(piece)]) + piece


class FromUnit:
    """Packets the unit sends back: bytes clients wrote toward a device."""

    def __init__(self):
        self.buffer = bytearray()
        self.lines = {}

    def feed(self, data):
        self.buffer.extend(data)
        while True:
            start = self.buffer.find(bytes([SYNC]))
            if start < 0 or len(self.buffer) - start < 3:
                return
            port, length = self.buffer[start + 1], self.buffer[start + 2]
            if len(self.buffer) - start < 3 + length:
                return
            payload = bytes(self.buffer[start + 3:start + 3 + length])
            del self.buffer[:start + 3 + length]
            yield port, payload

    def hex_lines(self, port, payload):
        text = self.lines.get(port, "") + payload.decode("latin-1")
        *done, rest = text.split("\n")
        self.lines[port] = rest
        return [line.strip() for line in done if line.strip()]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="the unit's VE.Direct mock USB port (found by default)")
    parser.add_argument("--day", type=float, default=600.0,
                        help="real seconds per simulated 24 h (default 600)")
    parser.add_argument("--seed", type=int, help="repeat a run: same clocks, clouds and loads")
    parser.add_argument("--corrupt", type=float, default=0.0,
                        help="fraction of blocks sent with a wrong checksum (default 0)")
    parser.add_argument("--duration", type=float, help="stop after this many seconds")
    parser.add_argument("--fill", action="store_true",
                        help="also put an MPPT on port 3, so every port carries data")
    arguments = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)

    rng = random.Random(arguments.seed)
    link = serial.Serial(arguments.port or find_port(), timeout=0)
    now = time.monotonic()
    world = World(rng, arguments.day)
    chargers = [Mppt(1, "MPPT 75/15 #1", rng, now, "HQ2216RK4QZ", 200, 100),
                Mppt(2, "MPPT 75/15 #2", rng, now, "HQ2216RK7TN", 150, 0)]
    if arguments.fill:
        chargers.append(Mppt(3, "MPPT 75/15 #3", rng, now, "HQ2216RK9WB", 100, 0))
    inverter = Phoenix(4, "Phoenix 12/500", rng, now, "HQ2218V9X3M")
    devices = chargers + [inverter]

    print(f"mock on {link.port}; a simulated day is {arguments.day:.0f} s")
    for device in devices:
        print(f"  port {device.port}: {device.name:15s} period {device.period * 1000:7.1f} ms")
    from_unit = FromUnit()
    last, last_report, started = now, now, now
    try:
        while arguments.duration is None or now - started < arguments.duration:
            time.sleep(0.004)
            now = time.monotonic()
            world.advance(now - last)
            for charger in chargers:
                charger.update(world, now - last)
            world.charge_a = sum(c.ppv * 0.97 for c in chargers) / world.voltage()
            world.dc_load_a = inverter.dc_amps(world) + sum(c.load_ma for c in chargers) / 1000.0
            last = now

            out = b"".join(packet for device in devices
                           for packet in packets(device.port, device.due(now, world, arguments.corrupt)))
            for port, payload in from_unit.feed(link.read(4096)):
                for line in from_unit.hex_lines(port, payload):
                    device = next((d for d in devices if d.port == port), None)
                    answer = device.hex_command(line) if device else b""
                    print(f"  port {port} <- {line}" + (f"   -> {answer.decode().strip()}" if answer else ""))
                    if answer:
                        out += b"".join(packets(port, answer))
            if out:
                link.write(out)

            if now - last_report >= 10.0:
                last_report = now
                hour = int(world.hour)
                print(f"{hour:02d}:{int((world.hour - hour) * 60):02d}  battery {world.voltage():5.2f} V "
                      f"{world.soc * 100:3.0f}%  "
                      + "  ".join(f"{c.name[-2:]} {c.ppv:3.0f} W" for c in chargers)
                      + f"  inverter {world.ac_va:3.0f} VA  blocks "
                      + "/".join(str(d.blocks) for d in devices))
    except KeyboardInterrupt:
        pass
    link.close()


if __name__ == "__main__":
    main()
