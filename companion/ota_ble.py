"""Discover paired M5Stack devices and read their temporary BLE OTA access."""

from __future__ import annotations

import asyncio
import sys
from dataclasses import dataclass
from ipaddress import IPv4Address

from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

from ble_mic_receiver import (
    Candidate,
    create_client,
    list_paired_ble_devices,
)


OTA_SERVICE_UUID = "e5d20001-3c9b-4f6a-9b2d-2a7f4510a002"
OTA_CREDENTIALS_UUID = "e5d20002-3c9b-4f6a-9b2d-2a7f4510a002"
_READY_TIMEOUT_SECONDS = 20.0
_SCAN_TIMEOUT_SECONDS = 6.0
_DEVICE_CONNECT_TIMEOUT_SECONDS = 15.0


@dataclass(frozen=True)
class OtaDevice:
    name: str
    address: str
    family: str
    host: str
    password: str

    @property
    def label(self) -> str:
        return f"{self.name} - {self.host} ({self.address})"


class OtaDiscoveryError(RuntimeError):
    """Raised when no compatible device can provide BLE OTA access."""


def parse_credentials(
    payload: bytes | bytearray | str, name: str, address: str
) -> OtaDevice:
    if isinstance(payload, (bytes, bytearray)):
        text = bytes(payload).decode("utf-8", errors="strict")
    else:
        text = payload
    fields = text.strip().splitlines()
    if len(fields) != 3:
        raise ValueError("The BLE OTA credential response has an invalid shape.")

    family, host, password = fields
    if family not in {"sticks3", "atoms3r"}:
        raise ValueError(f"Unsupported M5Stack device family: {family!r}")
    try:
        host = str(IPv4Address(host))
    except ValueError as error:
        raise ValueError(
            "The BLE OTA response contains an invalid IPv4 address."
        ) from error
    if not password or len(password) > 24 or not password.isascii():
        raise ValueError(
            "The BLE OTA response contains an invalid temporary password."
        )
    return OtaDevice(name, address, family, host, password)


def _looks_like_target(name: str) -> bool:
    normalized = name.casefold()
    return normalized.startswith(("stick", "atom"))


async def _read_access(candidate: Candidate) -> OtaDevice:
    client: BleakClient = create_client(
        candidate, _DEVICE_CONNECT_TIMEOUT_SECONDS
    )
    try:
        await client.connect()
        if client.services.get_service(OTA_SERVICE_UUID) is None:
            await client.disconnect()
            await asyncio.sleep(0.25)
            await client.connect()
            if client.services.get_service(OTA_SERVICE_UUID) is None:
                raise OtaDiscoveryError(
                    f"{candidate.name} does not expose the BLE OTA service."
                )
        try:
            response = await client.read_gatt_char(OTA_CREDENTIALS_UUID)
        except BleakError as initial_error:
            if not await client.pair():
                raise OtaDiscoveryError(
                    f"BLE pairing failed for {candidate.name}."
                ) from initial_error
            response = await client.read_gatt_char(OTA_CREDENTIALS_UUID)

        loop = asyncio.get_running_loop()
        deadline = loop.time() + _READY_TIMEOUT_SECONDS
        if bytes(response).decode("utf-8", errors="replace") == "WAIT_WIFI":
            print(
                f"{candidate.name} is connected over BLE; waiting for Wi-Fi "
                "and temporary OTA access...",
                flush=True,
            )
        while bytes(response).decode("utf-8", errors="replace") == "WAIT_WIFI":
            if loop.time() >= deadline:
                raise OtaDiscoveryError(
                    f"{candidate.name} is not ready for OTA; connect it to Wi-Fi."
                )
            await asyncio.sleep(0.5)
            response = await client.read_gatt_char(OTA_CREDENTIALS_UUID)

        return parse_credentials(response, candidate.name, candidate.address)
    finally:
        if client.is_connected:
            await client.disconnect()


async def discover_ota_devices(expected_family: str) -> list[OtaDevice]:
    """Find devices, pairing when needed, and keep only the requested family."""
    candidates: list[Candidate] = []
    if sys.platform == "win32":
        for paired in await list_paired_ble_devices():
            if _looks_like_target(paired.name):
                candidates.append(
                    Candidate(
                        address=paired.address,
                        name=paired.name,
                        source="connected" if paired.connected else "paired",
                        connected=paired.connected,
                    )
                )

    try:
        advertisements = await BleakScanner.discover(
            timeout=_SCAN_TIMEOUT_SECONDS, return_adv=True
        )
    except (BleakError, OSError, asyncio.TimeoutError) as error:
        if not candidates:
            raise OtaDiscoveryError(f"BLE scan failed: {error}") from error
        advertisements = {}

    known_addresses = {candidate.address.casefold() for candidate in candidates}
    for device, advertisement in advertisements.values():
        name = advertisement.local_name or device.name or "M5Stack device"
        service_uuids = {
            service_uuid.casefold()
            for service_uuid in getattr(advertisement, "service_uuids", ())
        }
        if not _looks_like_target(name) and OTA_SERVICE_UUID not in service_uuids:
            continue
        if device.address.casefold() not in known_addresses:
            candidates.append(
                Candidate(
                    address=device.address,
                    name=name,
                    ble_device=device,
                    source="advertising",
                )
            )
            known_addresses.add(device.address.casefold())

    if not candidates:
        raise OtaDiscoveryError(
            "No paired or advertising StickS3/AtomS3R BLE device was found."
        )

    compatible: list[OtaDevice] = []
    seen_families: set[str] = set()
    errors: list[str] = []
    for candidate in candidates:
        try:
            device = await _read_access(candidate)
        except (
            BleakError,
            OSError,
            asyncio.TimeoutError,
            NotImplementedError,
            OtaDiscoveryError,
            ValueError,
        ) as error:
            errors.append(f"{candidate.name}: {error}")
            continue
        seen_families.add(device.family)
        if device.family == expected_family:
            compatible.append(device)

    if compatible:
        return compatible
    if seen_families:
        families = ", ".join(sorted(seen_families))
        raise OtaDiscoveryError(
            f"Found BLE OTA device(s) for {families}, but "
            f"{expected_family} was selected."
        )
    details = "\n  ".join(errors)
    raise OtaDiscoveryError(
        "No compatible device provided BLE OTA credentials."
        + (f"\n  {details}" if details else "")
    )
