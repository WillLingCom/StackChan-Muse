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

"""Chunked framing for messages larger than one BLE packet.

Both directions use the same frame: ``0xFE``, chunk index, total chunks,
then a payload fragment. A write or notification that does not start with
``0xFE`` is a complete, unchunked message.
"""

from __future__ import annotations

CHUNK_MAGIC = 0xFE
HEADER_BYTES = 3
MAX_PACKET_BYTES = 160
MAX_CHUNKS = 255
MAX_MESSAGE_BYTES = 8192
DEFAULT_ATT_MTU = 23
CHUNK_STAGGER_S = 0.05


def encode_chunks(data: bytes, mtu: int = DEFAULT_ATT_MTU) -> list[bytes]:
    """Split ``data`` into framed packets that fit one notification at ``mtu``."""
    notify_max = min(mtu - 3 if mtu > 3 else 20, MAX_PACKET_BYTES)
    usable = notify_max - HEADER_BYTES
    fragments = [data[i:i + usable] for i in range(0, len(data), usable)] or [b""]
    if len(fragments) > MAX_CHUNKS:
        raise ValueError(f"message needs {len(fragments)} chunks (max {MAX_CHUNKS})")
    total = len(fragments)
    return [bytes([CHUNK_MAGIC, i, total]) + frag for i, frag in enumerate(fragments)]


class ChunkAssembler:
    """Reassemble chunked writes, strictly in order.

    Index 0, or a change in the total, starts a new message. An out-of-order
    chunk or an oversize message discards what has been collected.
    """

    def __init__(self, max_bytes: int = MAX_MESSAGE_BYTES) -> None:
        self._max_bytes = max_bytes
        self.reset()

    def reset(self) -> None:
        self._buf = bytearray()
        self._total = 0
        self._next = 0

    def feed(self, packet: bytes) -> bytes | None:
        """Add one write; returns a complete message once one is available."""
        if len(packet) < HEADER_BYTES or packet[0] != CHUNK_MAGIC:
            return bytes(packet)
        index, total, fragment = packet[1], packet[2], packet[HEADER_BYTES:]
        if total == 0:
            self.reset()
            return None
        if index == 0 or total != self._total:
            self.reset()
            self._total = total
        if index != self._next or index >= self._total:
            self.reset()
            return None
        if len(self._buf) + len(fragment) > self._max_bytes:
            self.reset()
            return None
        self._buf += fragment
        self._next = index + 1
        if self._next < self._total:
            return None
        message = bytes(self._buf)
        self.reset()
        return message
