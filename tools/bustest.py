#!/usr/bin/env python3
"""Poll-level test of the 485 link, run on recon in tmptun's place.

Plays the master on the 485 serial port, so tmptun must be stopped (or pass
--manage-tmptun, which stops it and starts it again however the run ends).
Each poll is classified by what came back, which splits errors by direction:

  ok       a valid TMP_NET reply
  crcfail  a reply whose CRC failed          unit -> recon error
  partial  bytes, but no complete frame       unit -> recon error, or a recon
                                              UART overrun (see oe)
  silent   nothing at all                     the unit did not take the poll:
                                              recon -> unit error, or no unit

Its polls do not set ARQ (acknowledged delivery), so the slave answers best
effort and every error shows here instead of being repaired by the next poll.

Phases, in this order, each reported on its own:

  --listen S   listen to the idle line for S seconds without polling
  --empty N    N empty polls (13 bytes each way)
  --junk N     N polls carrying a full 1514-byte frame for another MAC, which
               the unit discards (load toward the unit)
  --echo N     N ICMP echo requests to 10.55.0.3 carrying random data, each
               chased with empty polls until its reply comes back; replies
               are compared byte for byte (load both ways). --size is the ICMP
               data per echo, at most 558: the unit sends no frame over 600 B
               (CFG_NET_FRAME).

The unit's ARP requests for 10.55.0.1 are answered, so its link check stays
satisfied. The UART's error counters (framing, parity, break, overrun) are
read from /proc/tty/driver/ttyAMA around each phase; that needs root.

A unit that is silent to every poll while the idle line is quiet is not
hearing the polls: check power, then A/B polarity.

  sudo tools/bustest.py --manage-tmptun                     # a short standard run
  sudo tools/bustest.py --manage-tmptun --empty 20000 --junk 2000 --echo 3000

Run it detached (nohup) when over SSH, so the tunnel returns if the session
drops. The unit's address and MAC are those of common/config.h.
"""
import argparse
import os
import random
import re
import select
import signal
import struct
import subprocess
import sys
import termios
import time
import zlib

UNIT_MAC = bytes.fromhex("025645444301")    # CFG_MAC_ADDR
RECON_MAC = bytes.fromhex("02000a370001")   # any; the unit answers what it is sent
JUNK_MAC = bytes.fromhex("02000a3700ff")    # nobody's
UNIT_IP = bytes([10, 55, 0, 3])
RECON_IP = bytes([10, 55, 0, 1])
TMP_POLL_TO_COLLECTOR = bytes([0x21, 0x01])  # VER 2 | TMP_POLL, address 0x01


def uart_counters(device):
    """The serial driver's counters for this port, or {} without root."""
    match = re.search(r"ttyAMA(\d+)$", device)
    if not match:
        return {}
    try:
        with open("/proc/tty/driver/ttyAMA") as file:
            for line in file:
                if line.startswith(match.group(1) + ":"):
                    fields = dict(item.split(":", 1) for item in line.split() if ":" in item)
                    return {key: int(fields.get(key, 0)) for key in ("fe", "pe", "brk", "oe")}
    except OSError:
        pass
    return {}


def open_port(device, baud):
    speed = getattr(termios, f"B{baud}", None)
    if speed is None:
        sys.exit(f"{baud} baud is not a standard termios rate")
    fd = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    attrs[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP
                  | termios.INLCR | termios.IGNCR | termios.ICRNL | termios.IXON)
    attrs[1] &= ~termios.OPOST
    attrs[2] &= ~(termios.CSIZE | termios.PARENB | termios.CSTOPB | termios.CRTSCTS)
    attrs[2] |= termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN)
    attrs[4] = attrs[5] = speed
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def tmp_poll(sequence, payload):
    body = TMP_POLL_TO_COLLECTOR + bytes([sequence, 0]) + struct.pack(">H", len(payload)) + payload
    return b"\xAA\x55" + body + struct.pack(">I", zlib.crc32(body))


def ip_checksum(data):
    if len(data) % 2:
        data += b"\0"
    total = sum(struct.unpack(f">{len(data) // 2}H", data))
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return ~total & 0xFFFF


def echo_request(ident, sequence, data):
    icmp = struct.pack(">BBHHH", 8, 0, 0, ident, sequence) + data
    icmp = icmp[:2] + struct.pack(">H", ip_checksum(icmp)) + icmp[4:]
    ip = struct.pack(">BBHHHBBH4s4s", 0x45, 0, 20 + len(icmp), sequence, 0x4000, 64, 1, 0,
                     RECON_IP, UNIT_IP)
    ip = ip[:10] + struct.pack(">H", ip_checksum(ip)) + ip[12:]
    return UNIT_MAC + RECON_MAC + b"\x08\x00" + ip + icmp


def echo_reply_data(frame, ident, sequence):
    """The data of an ICMP echo reply to (ident, sequence) in frame, else None."""
    if len(frame) < 42 or frame[12:14] != b"\x08\x00" or frame[23] != 1:
        return None
    icmp = frame[14 + (frame[14] & 0xF) * 4:]
    if len(icmp) < 8 or icmp[0] != 0 or struct.unpack(">HH", icmp[4:8]) != (ident, sequence):
        return None
    return icmp[8:]


def arp_reply_to(frame):
    """Our answer when frame is an ARP request for recon, else None."""
    if len(frame) < 42 or frame[12:14] != b"\x08\x06" or frame[20:22] != b"\x00\x01" \
            or frame[38:42] != RECON_IP:
        return None
    sender_mac, sender_ip = frame[22:28], frame[28:32]
    arp = struct.pack(">HHBBH", 1, 0x0800, 6, 4, 2) + RECON_MAC + RECON_IP + sender_mac + sender_ip
    return sender_mac + RECON_MAC + b"\x08\x06" + arp


class Master:
    """Polls the unit as tmptun does and keeps per-phase counts."""

    def __init__(self, fd, baud, window):
        self.fd = fd
        self.baud = baud
        self.window = window
        self.sequence = 0
        self.pending = None     # an ARP reply for the next poll to carry
        self.reset()

    def reset(self):
        self.counts = dict(polls=0, ok=0, crcfail=0, partial=0, silent=0)
        self.bytes_down = self.bytes_up = 0

    def take_pending(self):
        frame, self.pending = self.pending, None
        return frame

    def exchange(self, payload=b""):
        """One poll; the reply's payload (b"" when empty), or None when none came."""
        self.sequence = (self.sequence + 1) & 0xFF
        wire = tmp_poll(self.sequence, payload)
        termios.tcflush(self.fd, termios.TCIFLUSH)
        os.write(self.fd, wire)
        self.bytes_down += len(wire)
        self.counts["polls"] += 1
        heard = bytearray()
        deadline = time.monotonic() + len(wire) * 10 / self.baud + self.window
        result = None
        while result is None:
            result = self.scan(heard)
            left = deadline - time.monotonic()
            if result is not None or left <= 0:
                break
            if select.select([self.fd], [], [], left)[0]:
                try:
                    heard += os.read(self.fd, 4096)
                except BlockingIOError:
                    pass
        self.bytes_up += len(heard)
        if result == "crcfail":
            self.counts["crcfail"] += 1
            return None
        if result is None:
            self.counts["partial" if heard.strip(b"\xff") else "silent"] += 1
            return None
        self.counts["ok"] += 1
        self.pending = arp_reply_to(result) or self.pending
        return result

    @staticmethod
    def scan(heard):
        start = heard.find(b"\xAA\x55")
        if start < 0 or len(heard) < start + 8:
            return None
        length = struct.unpack(">H", heard[start + 6:start + 8])[0]
        end = start + 8 + length + 4
        if len(heard) < end:
            return None
        body = bytes(heard[start + 2:start + 8 + length])
        if zlib.crc32(body) != struct.unpack(">I", heard[start + 8 + length:end])[0]:
            return "crcfail"
        return body[6:]


def report(name, master, started, before, after, extra=""):
    counts = master.counts
    delta = {key: after.get(key, 0) - before.get(key, 0) for key in after}
    print(f"{name}: {time.monotonic() - started:.1f} s, "
          f"{master.bytes_down * 10 / 1e6:.2f} Mbit down, {master.bytes_up * 10 / 1e6:.2f} Mbit up")
    print(f"  polls {counts['polls']}: ok {counts['ok']} crcfail {counts['crcfail']} "
          f"partial {counts['partial']} silent {counts['silent']}")
    if extra:
        print(f"  {extra}")
    if delta:
        print("  uart  " + " ".join(f"{key} {value}" for key, value in delta.items()))
    sys.stdout.flush()


def run(arguments):
    fd = open_port(arguments.port, arguments.baud)
    time.sleep(0.05)
    master = Master(fd, arguments.baud, arguments.window)
    rng = random.Random(1)
    print(f"{arguments.port} @ {arguments.baud}", flush=True)

    if arguments.listen:
        before = uart_counters(arguments.port)
        termios.tcflush(fd, termios.TCIFLUSH)
        heard = bytearray()
        deadline = time.monotonic() + arguments.listen
        while (left := deadline - time.monotonic()) > 0:
            if select.select([fd], [], [], left)[0]:
                try:
                    heard += os.read(fd, 4096)
                except BlockingIOError:
                    pass
        delta = {key: value - before.get(key, 0) for key, value in uart_counters(arguments.port).items()}
        print(f"idle {arguments.listen:g} s: {len(heard)} bytes {heard[:32].hex(' ')}")
        if delta:
            print("  uart  " + " ".join(f"{key} {value}" for key, value in delta.items()))

    if arguments.empty:
        master.reset()
        started, before = time.monotonic(), uart_counters(arguments.port)
        for _ in range(arguments.empty):
            master.exchange(master.take_pending() or b"")
        report(f"empty x{arguments.empty}", master, started, before, uart_counters(arguments.port))

    if arguments.junk:
        master.reset()
        started, before = time.monotonic(), uart_counters(arguments.port)
        for _ in range(arguments.junk):
            frame = master.take_pending() or (JUNK_MAC + RECON_MAC + b"\x88\xb5" + rng.randbytes(1500))
            master.exchange(frame)
        report(f"junk 1514 B x{arguments.junk}", master, started, before,
               uart_counters(arguments.port))

    if arguments.echo:
        master.reset()
        started, before = time.monotonic(), uart_counters(arguments.port)
        ident = os.getpid() & 0xFFFF
        echo = dict(ok=0, mismatch=0, missing=0)
        for number in range(arguments.echo):
            sequence = number & 0xFFFF
            data = rng.randbytes(arguments.size)
            payload, got = echo_request(ident, sequence, data), None
            for _ in range(1 + arguments.chase):
                reply = master.exchange(payload)
                payload = master.take_pending() or b""
                got = echo_reply_data(reply, ident, sequence) if reply else None
                if got is not None:
                    break
            echo["missing" if got is None else "ok" if got == data else "mismatch"] += 1
        report(f"echo {arguments.size} B x{arguments.echo}", master, started, before,
               uart_counters(arguments.port),
               f"echo  ok {echo['ok']} mismatch {echo['mismatch']} missing {echo['missing']}")

    os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default="/dev/ttyAMA3", help="recon's 485 UART (tmptun's)")
    parser.add_argument("--baud", type=int, default=500000)
    parser.add_argument("--listen", type=float, default=0.0)
    parser.add_argument("--empty", type=int, default=0)
    parser.add_argument("--junk", type=int, default=0)
    parser.add_argument("--echo", type=int, default=0)
    parser.add_argument("--size", type=int, default=558)
    parser.add_argument("--window", type=float, default=0.1,
                        help="reply wait after the poll has gone out, s (tmptun's 100 ms)")
    parser.add_argument("--chase", type=int, default=6,
                        help="empty polls to wait for an echo reply")
    parser.add_argument("--manage-tmptun", action="store_true",
                        help="stop tmptun.service for the run and start it again after")
    arguments = parser.parse_args()
    if not (arguments.listen or arguments.empty or arguments.junk or arguments.echo):
        arguments.listen, arguments.empty, arguments.junk, arguments.echo = 1.0, 2000, 200, 300
    if not 0 <= arguments.size <= 558:
        parser.error("--size is 0..558: the unit sends no frame over 600 bytes")

    if not arguments.manage_tmptun:
        run(arguments)
        return
    # Turn SIGHUP/SIGTERM (an SSH session dropping) into an exception, so the
    # finally below brings the tunnel back.
    def stop(signum, frame):
        raise SystemExit(f"stopped by signal {signum}")
    signal.signal(signal.SIGHUP, stop)
    signal.signal(signal.SIGTERM, stop)
    subprocess.run(["systemctl", "stop", "tmptun.service"], check=True)
    try:
        run(arguments)
    finally:
        subprocess.run(["systemctl", "start", "tmptun.service"], check=False)
        state = subprocess.run(["systemctl", "is-active", "tmptun.service"],
                               capture_output=True, text=True).stdout.strip()
        print(f"tmptun {state}", flush=True)


if __name__ == "__main__":
    main()
