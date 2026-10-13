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

from __future__ import annotations

import pytest

from musegadget.ble_framing import ChunkAssembler, encode_chunks


@pytest.mark.parametrize("mtu, per_chunk", [(23, 17), (185, 157), (256, 157), (3, 17)])
def test_chunk_payload_follows_mtu(mtu, per_chunk):
    chunks = encode_chunks(b"x" * 1000, mtu)
    assert all(len(c) - 3 == per_chunk for c in chunks[:-1])
    assert b"".join(c[3:] for c in chunks) == b"x" * 1000


def test_chunk_headers():
    chunks = encode_chunks(b"a" * 40, 23)
    assert [c[:3] for c in chunks] == [b"\xfe\x00\x03", b"\xfe\x01\x03", b"\xfe\x02\x03"]


def test_empty_message_is_one_chunk():
    assert encode_chunks(b"") == [b"\xfe\x00\x01"]


def test_too_many_chunks_is_an_error():
    with pytest.raises(ValueError):
        encode_chunks(b"x" * (17 * 255 + 1), 23)


def test_round_trip():
    message = bytes(range(256)) * 10
    assembler = ChunkAssembler()
    results = [assembler.feed(c) for c in encode_chunks(message, 185)]
    assert results[-1] == message
    assert all(r is None for r in results[:-1])


def test_plain_write_passes_through():
    assert ChunkAssembler().feed(b'{"action":"get_device_info"}') == b'{"action":"get_device_info"}'


def test_out_of_order_chunk_discards_the_message():
    chunks = encode_chunks(b"y" * 60, 23)
    assembler = ChunkAssembler()
    assert assembler.feed(chunks[0]) is None
    assert assembler.feed(chunks[2]) is None
    assert assembler.feed(chunks[1]) is None
    assert assembler.feed(chunks[3]) is None


def test_index_zero_restarts_the_message():
    old = encode_chunks(b"o" * 60, 23)
    new = encode_chunks(b"n" * 30, 23)
    assembler = ChunkAssembler()
    assembler.feed(old[0])
    assembler.feed(old[1])
    assert [assembler.feed(c) for c in new][-1] == b"n" * 30


def test_changed_total_mid_message_discards_it():
    assembler = ChunkAssembler()
    assembler.feed(b"\xfe\x00\x03abc")
    assert assembler.feed(b"\xfe\x01\x02def") is None
    assert assembler.feed(b"\xfe\x02\x03ghi") is None


def test_zero_total_is_ignored():
    assert ChunkAssembler().feed(b"\xfe\x00\x00abc") is None


def test_oversize_message_is_discarded():
    assembler = ChunkAssembler(max_bytes=20)
    chunks = encode_chunks(b"z" * 30, 23)
    assert [assembler.feed(c) for c in chunks] == [None, None]
