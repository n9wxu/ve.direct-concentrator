#!/usr/bin/env python3
"""Capture the concentrator's VE.Direct streams, check and decode them.

Connects to every port's TCP stream (5001-5004) at once for a while, splits
each into VE.Direct text blocks, checks every block's checksum, and prints
per port: bytes, blocks good and bad, the block period, and the latest
values, decoded. Devices that split their fields over two blocks (an MPPT's
history in H19..HSDS) are merged. The first block of each stream is skipped,
as the connection most likely cut into it.

Each port takes one client, so a collector already connected keeps it and
the port shows here as refused.

  vecapture.py                     # 20 s, from recon (or anything routed to 10.55.0.3)
  vecapture.py --seconds 60 --json > capture.json
"""
import argparse
import json
import select
import socket
import time

CHARGE_STATE = {0: "off", 1: "low power", 2: "fault", 3: "bulk", 4: "absorption", 5: "float",
                6: "storage", 7: "equalize", 9: "inverting", 11: "power supply",
                245: "starting up", 246: "repeated absorption", 247: "recondition",
                248: "BatterySafe", 252: "external control"}
TRACKER = {0: "off", 1: "voltage or current limited", 2: "tracking"}
MODE = {1: "charger", 2: "inverter", 4: "off", 5: "eco", 0xFD: "hibernate"}
OFF_REASON = {0x1: "no input power", 0x2: "power switch", 0x4: "device mode", 0x8: "remote input",
              0x10: "protection", 0x20: "pay-as-you-go", 0x40: "BMS",
              0x80: "engine shutdown", 0x100: "analysing input voltage"}
ALARM = {0x1: "low voltage", 0x2: "high voltage", 0x4: "low SOC", 0x8: "low starter voltage",
         0x10: "high starter voltage", 0x20: "low temperature", 0x40: "high temperature",
         0x80: "mid voltage", 0x100: "overload", 0x200: "DC ripple", 0x400: "low AC out",
         0x800: "high AC out", 0x1000: "short circuit", 0x2000: "BMS lockout"}


def scaled(factor, unit):
    return lambda value: f"{int(value) * factor:.{max(0, len(str(factor)) - 2)}f} {unit}"


def named(table):
    return lambda value: table.get(int(value, 0), value)


def bits(table):
    def decode(value):
        number = int(value, 0)
        names = [name for bit, name in table.items() if number & bit]
        return ", ".join(names) if names else "none"
    return decode


FIELDS = {   # label: (name, decoder), in printing order
    "PID": ("product id", str), "FW": ("firmware", str), "SER#": ("serial", str),
    "MODE": ("mode", named(MODE)), "CS": ("state", named(CHARGE_STATE)),
    "MPPT": ("tracker", named(TRACKER)), "OR": ("off reason", bits(OFF_REASON)),
    "ERR": ("error", str), "AR": ("alarm", bits(ALARM)), "WARN": ("warning", bits(ALARM)),
    "V": ("battery", scaled(0.001, "V")), "I": ("battery current", scaled(0.001, "A")),
    "VPV": ("panel", scaled(0.001, "V")), "PPV": ("panel power", scaled(1, "W")),
    "LOAD": ("load output", str), "IL": ("load current", scaled(0.001, "A")),
    "AC_OUT_V": ("AC out", scaled(0.01, "V")), "AC_OUT_I": ("AC out current", scaled(0.1, "A")),
    "AC_OUT_S": ("AC out power", scaled(1, "VA")),
    "H19": ("yield total", scaled(0.01, "kWh")), "H20": ("yield today", scaled(0.01, "kWh")),
    "H21": ("max power today", scaled(1, "W")), "H22": ("yield yesterday", scaled(0.01, "kWh")),
    "H23": ("max power yesterday", scaled(1, "W")), "HSDS": ("day", str),
}


class Stream:
    def __init__(self, port):
        self.port = port
        self.buffer = bytearray()
        self.bytes = self.blocks = self.good = self.bad = 0
        self.times = []
        self.fields = {}

    def feed(self, data):
        self.bytes += len(data)
        self.buffer += data
        while True:
            at = self.buffer.find(b"Checksum\t")
            if at < 0 or len(self.buffer) < at + 10:
                return
            raw = bytes(self.buffer[:at + 10])
            del self.buffer[:at + 10]
            self.blocks += 1
            if self.blocks == 1:
                continue
            # A block runs from its first "\r\n" to the checksum byte; anything
            # before that (a HEX-protocol message) is not part of it.
            block = raw[raw.find(b"\r\n"):] if b"\r\n" in raw else raw
            if sum(block) % 256:
                self.bad += 1
                continue
            self.good += 1
            self.times.append(time.monotonic())
            for line in block.split(b"\r\n"):
                label, tab, value = line.partition(b"\t")
                if tab and label != b"Checksum":
                    self.fields[label.decode(errors="replace")] = value.decode(errors="replace")

    def period(self):
        if len(self.times) < 2:
            return None
        return (self.times[-1] - self.times[0]) / (len(self.times) - 1)


def capture(host, ports, seconds):
    streams, sockets, refused = {}, {}, []
    for port in ports:
        try:
            connection = socket.create_connection((host, port), timeout=5)
        except OSError as error:
            refused.append((port, str(error)))
            continue
        connection.setblocking(False)
        sockets[connection] = streams[port] = Stream(port)
    deadline = time.monotonic() + seconds
    while sockets and (left := deadline - time.monotonic()) > 0:
        for connection in select.select(list(sockets), [], [], min(left, 0.5))[0]:
            data = connection.recv(4096)
            if data:
                sockets[connection].feed(data)
            else:
                del sockets[connection]
    return streams, refused


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="10.55.0.3")
    parser.add_argument("--ports", default="1,2,3,4", help="VE.Direct ports, e.g. 1,2,4")
    parser.add_argument("--seconds", type=float, default=20.0)
    parser.add_argument("--json", action="store_true", help="raw fields as JSON")
    arguments = parser.parse_args()
    ports = [5000 + int(number) for number in arguments.ports.split(",")]

    streams, refused = capture(arguments.host, ports, arguments.seconds)

    if arguments.json:
        print(json.dumps({"seconds": arguments.seconds, "refused": dict(refused),
                          "ports": {port: dict(bytes=s.bytes, good=s.good, bad=s.bad,
                                               period=s.period(), fields=s.fields)
                                    for port, s in streams.items()}}, indent=1))
        return
    for port, error in refused:
        print(f"port {port - 5000} (TCP {port}): {error}")
    for port, stream in streams.items():
        period = stream.period()
        print(f"port {port - 5000} (TCP {port}): {stream.bytes} bytes, blocks {stream.good} good "
              f"{stream.bad} bad" + (f", every {period:.2f} s" if period else ""))
        for label, (name, decode) in FIELDS.items():
            if label in stream.fields:
                try:
                    shown = decode(stream.fields[label])
                except ValueError:
                    shown = stream.fields[label]
                print(f"    {name:20} {shown}")
        for label in stream.fields.keys() - FIELDS.keys():
            print(f"    {label:20} {stream.fields[label]}")


if __name__ == "__main__":
    main()
