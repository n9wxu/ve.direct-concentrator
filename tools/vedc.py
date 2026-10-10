#!/usr/bin/env python3
"""Talk to the VE.Direct concentrator's control port from recon.

  vedc.py status
  vedc.py out 0..3          switched outputs: bit 0 = drive 1, bit 1 = drive 2
  vedc.py app               update the application (bootloader fetches vedc-app.bin)
  vedc.py bootloader        update the bootloader (application stages vedc-boot.bin)
  vedc.py rescue            catch the bootloader's start-up window of a unit whose
                            application keeps crashing, and make it re-fetch

Updates need the TFTP server on recon's tap0 serving the .bin files from
/srv/vedc: trailer-monitor/ops/tunnel/vedc-tftp.service.
"""
import argparse
import socket
import sys
import time

PORT = 5000


def ask(sock, host, text, timeout):
    sock.settimeout(timeout)
    sock.sendto(text.encode(), (host, PORT))
    try:
        return sock.recvfrom(512)[0].decode(errors="replace").strip()
    except socket.timeout:
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="10.55.0.3")
    parser.add_argument("command", nargs="+")
    arguments = parser.parse_args()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    if arguments.command[0] == "rescue":
        print("sending 'stay' until the bootloader answers (ctrl-C to stop)")
        while True:
            answer = ask(sock, arguments.host, "stay", 0.1)
            if answer and answer.startswith("ok fetching"):
                print(answer)
                return 0
            time.sleep(0.05)

    answer = ask(sock, arguments.host, " ".join(arguments.command), 2.0)
    if answer is None:
        print("no answer", file=sys.stderr)
        return 1
    print(answer)
    return 0 if not answer.startswith("error") else 1


if __name__ == "__main__":
    sys.exit(main())
