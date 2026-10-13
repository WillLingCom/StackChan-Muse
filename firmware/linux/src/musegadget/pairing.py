# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Device side of Muse Gadget BLE pairing, protocol version 5.

Community mode only: ``pairing_auth`` is ``"none"`` (no manufacturer proof)
and the policy is ``confirm_app``, so a valid client-finished record confirms
the session without a physical button. The wire format, transcript, key
schedule and record encryption match the ESP32 reference firmware so the
existing Muse apps pair with it unchanged.

Community pairing protects setup secrets from passive observers. It does not
authenticate the ECDH peer, so it cannot stop an active man-in-the-middle.
"""

from __future__ import annotations

import base64
import enum
import hashlib
import json
import os
import re
import threading
import time
from typing import Callable

from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.hkdf import HKDF, HKDFExpand

PAIRING_VERSION = 5
PAIRING_MODEL = "hatch_link"
PAIRING_SUITE = "p256-hkdf-sha256-aes-gcm-v1"
POLICY_BUTTON = "confirm_press"
POLICY_APP = "confirm_app"
AUTH_OFFICIAL = "fleet_ecdsa_p256_v1"
AUTH_COMMUNITY = "none"
BUTTON_CONFIRM_TIMEOUT_S = 60

RECORD_LABEL = "hatch-link ble setup v1"
SESSION_ID_LABEL = b"hatch-link session id v1"

CLIENT_FINISHED_TIMEOUT_S = 60
CONFIRMED_TIMEOUT_S = 120
PROVISIONING_TIMEOUT_S = 120

ERROR_INVALID_HELLO = "error_pairing_invalid_hello"
ERROR_DECRYPT = "error_pairing_decrypt"

_P256_POINT_BYTES = 65
_NONCE_BYTES = 16
_SESSION_ID_BYTES = 16
_TAG_BYTES = 16
_MAX_B64_CHARS = 4096
_MAX_CIPHERTEXT_B64_CHARS = 16384
_B64URL_RE = re.compile(r"[A-Za-z0-9_-]+")
_DECIMAL_RE = re.compile(r"[0-9]+")
_TO_DEVICE = 0
_FROM_DEVICE = 1


class PairingError(Exception):
    """A handshake step failed; ``status`` is the wire error to report."""

    def __init__(self, status: str) -> None:
        super().__init__(status)
        self.status = status


class PairingState(enum.Enum):
    IDLE = "idle"
    WAIT_CLIENT_FINISHED = "wait_client_finished"
    READY = "ready"
    PROVISIONING = "provisioning"


def b64url_encode(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def b64url_decode(text: object, max_chars: int = _MAX_B64_CHARS) -> bytes:
    """Decode unpadded base64url, rejecting anything the firmware rejects."""
    if not isinstance(text, str) or not 0 < len(text) <= max_chars:
        raise ValueError("invalid base64url length")
    if len(text) % 4 == 1 or not _B64URL_RE.fullmatch(text):
        raise ValueError("invalid base64url")
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def parse_counter(text: object) -> int:
    if not isinstance(text, str) or not _DECIMAL_RE.fullmatch(text):
        raise ValueError("invalid counter")
    value = int(text)
    if value >= 1 << 64:
        raise ValueError("counter overflow")
    return value


def build_transcript(
    *,
    community: bool,
    auth_epoch: int,
    policy: str,
    device_id: str,
    node_id: str,
    mac: str,
    firmware_version: str,
    mobile_pub: str,
    device_pub: str,
    mobile_nonce: str,
    device_nonce: str,
) -> str:
    """Canonical v5 transcript; SHA-256 of it is the ``transcript_hash``."""
    button = policy == POLICY_BUTTON
    if not (button or (community and policy == POLICY_APP)):
        raise ValueError(f"unsupported pairing policy: {policy!r}")
    if (auth_epoch != 0) if community else (auth_epoch <= 0):
        raise ValueError(f"invalid auth epoch: {auth_epoch}")
    fields = (device_id, node_id, mac, firmware_version,
              mobile_pub, device_pub, mobile_nonce, device_nonce)
    if not all(fields):
        raise ValueError("transcript fields must be non-empty")
    return "\n".join([
        f"hatch-link-pairing-v{PAIRING_VERSION}",
        f"version={PAIRING_VERSION}",
        "initiator_role=mobile",
        "responder_role=link",
        f"device_id={device_id}",
        f"node_id={node_id}",
        f"mac={mac}",
        f"model={PAIRING_MODEL}",
        f"firmware_version={firmware_version}",
        f"selected_cipher_suite={PAIRING_SUITE}",
        f"pairing_auth={AUTH_COMMUNITY if community else AUTH_OFFICIAL}",
        f"pairing_auth_epoch={auth_epoch}",
        f"pairing_policy={policy}",
        f"confirm_timeout_seconds={BUTTON_CONFIRM_TIMEOUT_S if button else 0}",
        f"mobile_pub={mobile_pub}",
        f"device_pub={device_pub}",
        f"mobile_nonce={mobile_nonce}",
        f"device_nonce={device_nonce}",
    ])


def derive_session_keys(
    ecdh_secret: bytes,
    mobile_nonce: bytes,
    device_nonce: bytes,
    transcript_hash: bytes,
) -> tuple[bytes, bytes, bytes]:
    """Return ``(mobile_tx_key, mobile_rx_key, session_id)``.

    ``mobile_tx_key`` decrypts records the device receives and
    ``mobile_rx_key`` encrypts records it sends.
    """
    salt = hashlib.sha256(mobile_nonce + device_nonce + transcript_hash).digest()
    session_secret = HKDF(
        algorithm=hashes.SHA256(), length=32, salt=salt,
        info=RECORD_LABEL.encode(),
    ).derive(ecdh_secret)
    mobile_tx = HKDFExpand(
        algorithm=hashes.SHA256(), length=32, info=b"mobile->device",
    ).derive(session_secret)
    mobile_rx = HKDFExpand(
        algorithm=hashes.SHA256(), length=32, info=b"device->mobile",
    ).derive(session_secret)
    session_id = hashlib.sha256(
        SESSION_ID_LABEL + transcript_hash + ecdh_secret,
    ).digest()[:_SESSION_ID_BYTES]
    return mobile_tx, mobile_rx, session_id


def record_nonce(direction: int, counter: int) -> bytes:
    return bytes([direction, 0, 0, 0]) + counter.to_bytes(8, "big")


def record_aad(session_id_b64: str, direction: int, counter: int) -> bytes:
    arrow = "m2d" if direction == _TO_DEVICE else "d2m"
    return f"{RECORD_LABEL}|{session_id_b64}|{arrow}|{counter}".encode()


def _generate_device_key() -> ec.EllipticCurvePrivateKey:
    return ec.generate_private_key(ec.SECP256R1())


class PairingSession:
    """One device's pairing state; safe to call from several threads.

    Methods that advance the handshake return a nonzero *generation* token.
    Deferred work (Wi-Fi joins, provisioning) should hold on to it and check
    :meth:`is_current` before acting, so work from an abandoned attempt can't
    act on a newer one.
    """

    def __init__(
        self,
        *,
        node_id: str,
        device_id: str,
        mac: str,
        firmware_version: str,
        sdk_token: str | None = None,
        clock: Callable[[], float] = time.monotonic,
        generate_key: Callable[[], ec.EllipticCurvePrivateKey] = _generate_device_key,
        random_bytes: Callable[[int], bytes] = os.urandom,
    ) -> None:
        self._node_id = node_id
        self._sdk_token = sdk_token
        self._device_id = device_id
        self._mac = mac
        self._firmware_version = firmware_version or "unknown"
        self._clock = clock
        self._generate_key = generate_key
        self._random_bytes = random_bytes
        self._lock = threading.Lock()
        self._generation = 0
        self._reset_locked()

    def device_info(self) -> dict:
        """Pairing fields for the ``get_device_info`` response."""
        return {
            "device_id": self._device_id,
            "mac": self._mac,
            "model": PAIRING_MODEL,
            "pairing_protocol": PAIRING_VERSION,
            "pairing_auth": AUTH_COMMUNITY,
            "pairing_auth_epoch": 0,
            "pairing_policy": POLICY_APP,
        }

    @property
    def state(self) -> PairingState:
        with self._lock:
            self._expire_locked()
            return self._state

    @property
    def confirmed(self) -> bool:
        return self.state in (PairingState.READY, PairingState.PROVISIONING)

    def is_current(self, generation: int) -> bool:
        with self._lock:
            return generation != 0 and generation == self._generation

    def reset(self) -> None:
        with self._lock:
            self._reset_locked()

    def handle_hello(self, message: dict) -> dict:
        """Start a session from ``pairing_client_hello``; returns ``pairing_ready``."""
        version = message.get("version")
        if (
            isinstance(version, bool)
            or not isinstance(version, (int, float))
            or version != PAIRING_VERSION
            or message.get("pairing_auth") != AUTH_COMMUNITY
            or message.get("pairing_policy") != POLICY_APP
        ):
            raise PairingError(ERROR_INVALID_HELLO)

        with self._lock:
            self._reset_locked()
            try:
                mobile_pub = b64url_decode(message.get("mobile_pub"))
                mobile_nonce = b64url_decode(message.get("mobile_nonce"))
                if (
                    len(mobile_pub) != _P256_POINT_BYTES
                    or mobile_pub[0] != 0x04
                    or len(mobile_nonce) != _NONCE_BYTES
                ):
                    raise ValueError("invalid hello key material")
                peer = ec.EllipticCurvePublicKey.from_encoded_point(
                    ec.SECP256R1(), mobile_pub,
                )
            except ValueError:
                self._reset_locked()
                raise PairingError(ERROR_INVALID_HELLO) from None

            device_key = self._generate_key()
            device_pub = device_key.public_key().public_bytes(
                serialization.Encoding.X962,
                serialization.PublicFormat.UncompressedPoint,
            )
            device_nonce = self._random_bytes(_NONCE_BYTES)
            transcript = build_transcript(
                community=True,
                auth_epoch=0,
                policy=POLICY_APP,
                device_id=self._device_id,
                node_id=self._node_id,
                mac=self._mac,
                firmware_version=self._firmware_version,
                mobile_pub=b64url_encode(mobile_pub),
                device_pub=b64url_encode(device_pub),
                mobile_nonce=b64url_encode(mobile_nonce),
                device_nonce=b64url_encode(device_nonce),
            )
            transcript_hash = hashlib.sha256(transcript.encode()).digest()
            ecdh_secret = device_key.exchange(ec.ECDH(), peer)
            rx_key, tx_key, session_id = derive_session_keys(
                ecdh_secret, mobile_nonce, device_nonce, transcript_hash,
            )

            self._rx = AESGCM(rx_key)
            self._tx = AESGCM(tx_key)
            self._session_id_b64 = b64url_encode(session_id)
            self._rx_counter = 0
            self._tx_counter = 0
            self._state = PairingState.WAIT_CLIENT_FINISHED
            self._deadline = self._clock() + CLIENT_FINISHED_TIMEOUT_S
            return {
                "type": "pairing_ready",
                "version": PAIRING_VERSION,
                "device_id": self._device_id,
                "node_id": self._node_id,
                "mac": self._mac,
                "model": PAIRING_MODEL,
                "firmware_version": self._firmware_version,
                "pairing_auth": AUTH_COMMUNITY,
                "pairing_auth_epoch": 0,
                "pairing_policy": POLICY_APP,
                "device_pub": b64url_encode(device_pub),
                "device_nonce": b64url_encode(device_nonce),
                "transcript_hash": b64url_encode(transcript_hash),
                "session_id": self._session_id_b64,
            }

    def decrypt(self, envelope: dict) -> str:
        """Open one mobile-to-device ``pairing_encrypted`` record.

        Any failure (wrong session, skipped or replayed counter, bad tag,
        expiry) clears the session, as the firmware does.
        """
        with self._lock:
            if self._expire_locked() or self._state is PairingState.IDLE:
                self._reset_locked()
                raise PairingError(ERROR_DECRYPT)
            try:
                if envelope.get("session_id") != self._session_id_b64:
                    raise ValueError("wrong session")
                counter = parse_counter(envelope.get("counter"))
                if counter != self._rx_counter:
                    raise ValueError("unexpected counter")
                ciphertext = b64url_decode(
                    envelope.get("ciphertext"), _MAX_CIPHERTEXT_B64_CHARS,
                )
                tag = b64url_decode(envelope.get("tag"))
                if len(tag) != _TAG_BYTES:
                    raise ValueError("invalid tag length")
                plaintext = self._rx.decrypt(
                    record_nonce(_TO_DEVICE, counter),
                    ciphertext + tag,
                    record_aad(self._session_id_b64, _TO_DEVICE, counter),
                ).decode("utf-8")
            except (ValueError, InvalidTag):
                self._reset_locked()
                raise PairingError(ERROR_DECRYPT) from None
            self._rx_counter += 1
            return plaintext

    def handle_client_finished(self, command: dict) -> int:
        """Confirm the session after the first decrypted record.

        ``command`` must be exactly ``{"action": "pairing_client_finished"}``
        and must have been the first record. Under ``confirm_app`` the app
        already collected consent, so this confirms directly. Returns the new
        generation, or 0 (after clearing the session) if the record is invalid.
        """
        with self._lock:
            ok = (
                command == {"action": "pairing_client_finished"}
                and not self._expire_locked()
                and self._state is PairingState.WAIT_CLIENT_FINISHED
                and self._rx_counter == 1
            )
            if not ok:
                self._reset_locked()
                return 0
            self._advance_generation_locked()
            self._state = PairingState.READY
            self._deadline = self._clock() + CONFIRMED_TIMEOUT_S
            return self._generation

    def mark_provisioning(self) -> int:
        """Enter provisioning from a confirmed session; returns its generation."""
        with self._lock:
            if not self._expire_locked() and self._state is PairingState.READY:
                self._advance_generation_locked()
                self._state = PairingState.PROVISIONING
                self._deadline = self._clock() + PROVISIONING_TIMEOUT_S
            if self._state is PairingState.PROVISIONING:
                return self._generation
            return 0

    def extend_provisioning(self, generation: int) -> bool:
        with self._lock:
            valid = self._provisioning_locked(generation)
            if valid:
                self._deadline = self._clock() + PROVISIONING_TIMEOUT_S
            return valid

    def commit_provisioning(self, generation: int, commit: Callable[[], bool]) -> bool:
        """Run ``commit`` under the pairing lock if the session is still valid.

        ``commit`` must only persist local state; no network or BLE calls.
        """
        with self._lock:
            return self._provisioning_locked(generation) and bool(commit())

    def encrypt_json(self, plaintext: str, generation: int = 0) -> dict | None:
        """Seal a device-to-mobile record.

        Returns ``None`` when there is no active session, or when a nonzero
        ``generation`` no longer matches (the work it belongs to is stale).
        """
        with self._lock:
            if (
                (generation and generation != self._generation)
                or self._expire_locked()
                or self._state is PairingState.IDLE
            ):
                return None
            counter = self._tx_counter
            sealed = self._tx.encrypt(
                record_nonce(_FROM_DEVICE, counter),
                plaintext.encode("utf-8"),
                record_aad(self._session_id_b64, _FROM_DEVICE, counter),
            )
            self._tx_counter += 1
            return {
                "type": "pairing_encrypted",
                "session_id": self._session_id_b64,
                "counter": str(counter),
                "ciphertext": b64url_encode(sealed[:-_TAG_BYTES]),
                "tag": b64url_encode(sealed[-_TAG_BYTES:]),
            }

    def encrypt_status(self, status: str, generation: int = 0) -> dict | None:
        message = {"type": "status", "status": status}
        # Apps read only type and status, so older ones ignore the token.
        if self._sdk_token and status == "pairing_confirmed":
            message["sdk_token"] = self._sdk_token
        plaintext = json.dumps(message, separators=(",", ":"))
        return self.encrypt_json(plaintext, generation)

    def _provisioning_locked(self, generation: int) -> bool:
        return (
            generation != 0
            and generation == self._generation
            and not self._expire_locked()
            and self._state is PairingState.PROVISIONING
        )

    def _advance_generation_locked(self) -> None:
        self._generation += 1

    def _reset_locked(self) -> None:
        self._advance_generation_locked()
        self._clear_locked()

    def _clear_locked(self) -> None:
        self._state = PairingState.IDLE
        self._deadline = 0.0
        self._rx: AESGCM | None = None
        self._tx: AESGCM | None = None
        self._session_id_b64 = ""
        self._rx_counter = 0
        self._tx_counter = 0

    def _expire_locked(self) -> bool:
        if self._state is PairingState.IDLE or self._clock() <= self._deadline:
            return False
        # Drop the keys but keep the generation, so the owner of the expired
        # session can still recognise it and close the connection.
        self._clear_locked()
        return True
