"""Bridge a WitMotion WT901BLE IMU into MayaFlux.

MayaFlux has no Bluetooth input, so this script reads the IMU over BLE and hands
the data on in one or both of two ways:

  OSC (default)  decoded values to MayaFlux's OSC input:
                 /imu/acc   ax ay az     in g
                 /imu/gyro  gx gy gz     in degrees per second
                 /imu/angle roll pitch yaw in degrees
  --pty PATH     the raw 20-byte packets into a pseudo-terminal linked at PATH,
                 for MayaFlux to open as a serial port and decode itself.

Run:  .venv/bin/python witmotion_bridge.py [--osc 127.0.0.1:8000] [--pty PATH]
      [--no-osc] [--rate HZ] [--print]
"""

import argparse
import asyncio
import os
import signal
import struct
import tty

from bleak import BleakClient, BleakScanner
from pythonosc.udp_client import SimpleUDPClient

NOTIFY = "0000ffe4-0000-1000-8000-00805f9a34fb"
WRITE = "0000ffe9-0000-1000-8000-00805f9a34fb"
RATE_CODES = {10: 0x06, 20: 0x07, 50: 0x08, 100: 0x09, 200: 0x0B}


def decode(packet: bytes):
    """One 0x55 0x61 packet to (acc g, gyro deg/s, angle deg), or None."""
    if len(packet) < 20 or packet[0] != 0x55 or packet[1] != 0x61:
        return None
    v = struct.unpack_from("<9h", packet, 2)
    acc = [x / 32768.0 * 16.0 for x in v[0:3]]
    gyro = [x / 32768.0 * 2000.0 for x in v[3:6]]
    angle = [x / 32768.0 * 180.0 for x in v[6:9]]
    return acc, gyro, angle


class PtyOut:
    """A pseudo-terminal whose other end MayaFlux opens like a serial port."""

    def __init__(self, link: str):
        self.master, self.slave = os.openpty()
        tty.setraw(self.slave)
        os.set_blocking(self.master, False)
        self.link = link
        if os.path.islink(link):
            os.unlink(link)
        os.symlink(os.ttyname(self.slave), link)
        print(f"virtual port: {link} -> {os.ttyname(self.slave)}")

    def write(self, packet: bytes):
        try:
            os.write(self.master, packet)
        except BlockingIOError:
            pass

    def close(self):
        if os.path.islink(self.link):
            os.unlink(self.link)


async def run(args):
    osc = None
    if not args.no_osc:
        host, port = args.osc.rsplit(":", 1)
        osc = SimpleUDPClient(host, int(port))
        print(f"OSC to {host}:{port}")
    pty_out = PtyOut(args.pty) if args.pty else None

    count = 0
    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, stop.set)

    def on_packet(_, data: bytearray):
        nonlocal count
        packet = bytes(data)
        if pty_out:
            pty_out.write(packet)
        values = decode(packet)
        if not values:
            return
        acc, gyro, angle = values
        if osc:
            osc.send_message("/imu/acc", acc)
            osc.send_message("/imu/gyro", gyro)
            osc.send_message("/imu/angle", angle)
        count += 1
        if args.print and count % 10 == 0:
            print("acc " + " ".join(f"{a:+.3f}" for a in acc)
                  + " | gyro " + " ".join(f"{g:+7.1f}" for g in gyro)
                  + " | angle " + " ".join(f"{a:+7.1f}" for a in angle))

    try:
        while not stop.is_set():
            device = await BleakScanner.find_device_by_filter(
                lambda d, _: (d.name or "").startswith(args.name), timeout=10.0)
            if not device:
                print(f"no {args.name}* found, scanning again")
                continue
            print(f"connecting to {device.name} {device.address}")
            disconnected = asyncio.Event()
            async with BleakClient(device, disconnected_callback=lambda _: disconnected.set()) as client:
                if args.rate:
                    code = RATE_CODES[args.rate]
                    await client.write_gatt_char(WRITE, bytes([0xFF, 0xAA, 0x69, 0x88, 0xB5]))
                    await client.write_gatt_char(WRITE, bytes([0xFF, 0xAA, 0x03, code, 0x00]))
                    print(f"rate set to {args.rate} Hz for this session (not saved)")
                await client.start_notify(NOTIFY, on_packet)
                print("streaming")
                while not stop.is_set():
                    try:
                        await asyncio.wait_for(disconnected.wait(), 0.5)
                    except asyncio.TimeoutError:
                        continue
                    if not client.is_connected:
                        break
                    disconnected.clear()
            print("stopped, disconnected" if stop.is_set() else "disconnected, reconnecting")
    finally:
        if pty_out:
            pty_out.close()


def main():
    parser = argparse.ArgumentParser(description="WitMotion WT901BLE to MayaFlux")
    parser.add_argument("--osc", default="127.0.0.1:8000", help="host:port of MayaFlux's OSC input")
    parser.add_argument("--no-osc", action="store_true", help="do not send OSC")
    parser.add_argument("--pty", help="also write raw packets to a virtual serial port linked at this path")
    parser.add_argument("--rate", type=int, choices=sorted(RATE_CODES), help="output rate for this session")
    parser.add_argument("--name", default="WT901", help="advertised name prefix")
    parser.add_argument("--print", action="store_true", help="print every tenth decoded packet")
    try:
        asyncio.run(run(parser.parse_args()))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
