"""Host-side BLE echo check for the XIAO ESP32-C3 link test.

The other computer needs Python 3 and a Bluetooth radio.

    pip install -r ble_echo_pio/host/requirements.txt
    python ble_echo_pio/host/ble_echo.py
    python ble_echo_pio/host/ble_echo.py hello
"""

import asyncio
import sys

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "XIAO-BLE-ECHO"
SERVICE_UUID = "8b1c0001-5b2a-4f3e-9c1d-6a7b8c9d0e01"
CHAR_UUID = "8b1c0002-5b2a-4f3e-9c1d-6a7b8c9d0e01"
SCAN_SECONDS = 10.0
ECHO_SECONDS = 5.0


def _message():
    if len(sys.argv) > 1:
        return sys.argv[1]
    return "ping"


def _echo_text(data):
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return data.hex()


async def _run():
    message = _message()
    payload = message.encode("utf-8")

    print(f"Scanning for {DEVICE_NAME}")
    device = await BleakScanner.find_device_by_name(
        DEVICE_NAME, timeout=SCAN_SECONDS
    )
    if device is None:
        print("FAIL: device not found")
        return 1

    print(f"Found {DEVICE_NAME} at {device.address}")

    echo = {"data": None}
    notified = asyncio.Event()

    def on_notify(_characteristic, data):
        if echo["data"] is None:
            echo["data"] = bytes(data)
            notified.set()

    async with BleakClient(device) as client:
        print("Connected")
        service = client.services.get_service(SERVICE_UUID)
        if service is None:
            raise RuntimeError(f"service {SERVICE_UUID} not found")
        characteristic = service.get_characteristic(CHAR_UUID)
        if characteristic is None:
            raise RuntimeError(f"characteristic {CHAR_UUID} not found")

        await client.start_notify(characteristic, on_notify)
        print(f"TX: {message}")
        await client.write_gatt_char(characteristic, payload, response=True)
        try:
            await asyncio.wait_for(notified.wait(), timeout=ECHO_SECONDS)
        except asyncio.TimeoutError:
            print("FAIL: no echo")
            return 1

    received = echo["data"]
    print(f"RX: {_echo_text(received)}")
    if received == payload:
        print("PASS: echo matched")
        return 0
    print("FAIL: echo mismatch")
    return 1


def main():
    try:
        return asyncio.run(_run())
    except Exception as exc:
        detail = str(exc).strip() or exc.__class__.__name__
        print(f"FAIL: {detail}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
