"""Host-side BLE echo session for the XIAO ESP32-C3 link test.

The other computer needs Python 3 and a Bluetooth radio.

    pip install -r ble_echo_pio/host/requirements.txt
    python ble_echo_pio/host/ble_echo.py

The script scans once, connects to XIAO-BLE-ECHO, and stays connected.
Type a signal and press Enter to write it and wait for the echo. A blank
line or quit disconnects. Ctrl+C disconnects as well.
"""

import asyncio
import sys
import threading

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "XIAO-BLE-ECHO"
SERVICE_UUID = "8b1c0001-5b2a-4f3e-9c1d-6a7b8c9d0e01"
CHAR_UUID = "8b1c0002-5b2a-4f3e-9c1d-6a7b8c9d0e01"
SCAN_SECONDS = 10.0
ECHO_SECONDS = 5.0


def _echo_text(data):
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return data.hex()


def _start_stdin_reader(loop, lines):
    def reader():
        try:
            while True:
                line = sys.stdin.readline()
                loop.call_soon_threadsafe(lines.put_nowait, line)
                if line == "":
                    return
        except Exception as exc:
            loop.call_soon_threadsafe(lines.put_nowait, exc)

    threading.Thread(target=reader, name="stdin-reader", daemon=True).start()


def _drain(queue):
    while True:
        try:
            queue.get_nowait()
        except asyncio.QueueEmpty:
            return


async def _read_signal(lines):
    print("> ", end="", flush=True)
    item = await lines.get()
    if isinstance(item, Exception):
        raise item
    if not sys.stdin.isatty():
        shown = item if item.endswith("\n") or item == "" else item + "\n"
        sys.stdout.write(shown)
        sys.stdout.flush()
    if item == "":
        return None
    signal = item.strip()
    if signal == "" or signal == "quit":
        return None
    return signal


async def _send_signal(client, characteristic, incoming, signal):
    payload = signal.encode("utf-8")
    _drain(incoming)
    print(f"TX: {signal}")
    await client.write_gatt_char(characteristic, payload, response=True)
    try:
        received = await asyncio.wait_for(incoming.get(), timeout=ECHO_SECONDS)
    except asyncio.TimeoutError:
        print("FAIL: no echo")
        return False
    print(f"RX: {_echo_text(received)}")
    if received != payload:
        print("FAIL: echo mismatch")
        return False
    return True


async def _run():
    print(f"Scanning for {DEVICE_NAME}")
    device = await BleakScanner.find_device_by_name(
        DEVICE_NAME, timeout=SCAN_SECONDS
    )
    if device is None:
        print("FAIL: device not found")
        return 1

    print(f"Found {DEVICE_NAME} at {device.address}")

    loop = asyncio.get_running_loop()
    incoming = asyncio.Queue()
    lines = asyncio.Queue()
    _start_stdin_reader(loop, lines)

    def on_notify(_characteristic, data):
        loop.call_soon_threadsafe(incoming.put_nowait, bytes(data))

    async with BleakClient(device) as client:
        service = client.services.get_service(SERVICE_UUID)
        if service is None:
            raise RuntimeError(f"service {SERVICE_UUID} not found")
        characteristic = service.get_characteristic(CHAR_UUID)
        if characteristic is None:
            raise RuntimeError(f"characteristic {CHAR_UUID} not found")

        await client.start_notify(characteristic, on_notify)
        print(
            f"Connected to {DEVICE_NAME}. Type a signal and press Enter. "
            "Blank line or quit to stop."
        )

        failed = False
        while True:
            signal = await _read_signal(lines)
            if signal is None:
                break
            if not await _send_signal(client, characteristic, incoming, signal):
                failed = True

    return 1 if failed else 0


def main():
    try:
        return asyncio.run(_run())
    except KeyboardInterrupt:
        print()
        return 0
    except Exception as exc:
        detail = str(exc).strip() or exc.__class__.__name__
        print(f"FAIL: {detail}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
