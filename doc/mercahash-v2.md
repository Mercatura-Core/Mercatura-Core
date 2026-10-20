# MercaHash revision V2

V1 remains historical consensus: 134,217,728 scratchpad bytes, 2,097,152
64-byte lines, 131,072 mixing steps, one binding pass over 32,768 4096-byte
blocks. Its domains and output bytes are unchanged.

V2 uses 16,777,216 bytes, 262,144 64-byte lines (mask 0x3FFFF), 16,384
mixing steps, and one binding pass over 4096 4096-byte blocks (mask 0xFFF).
Both use alternating half-round fill, 16 unsigned 64-bit state lanes,
secondary touches every 16 steps, cross-lane mixing every 64 steps, SHA3
checkpoints every 1024 steps, and 256 final sampling reads. Arithmetic wraps
modulo 2^64. All external word encodings are little endian. Canonical header
serialization remains exactly 80 bytes; block identity remains SHA256d.

V2 domains are the exact ASCII sequences `MercaHash-v2/state/0`,
`MercaHash-v2/state/1`, `MercaHash-v2/checkpoint`, and `MercaHash-v2/final`,
without terminating NULs. Production HashV1/HashV2 freeze geometry, domains,
fill and workload independently of the development-only WorkParams API.

## Permanent vectors

`src/test/mercahash_v2_tests.cpp` fixes raw output bytes, without uint256
presentation reversal. Inputs are 80 zero bytes, byte[i]=i, 80 bytes of 0xFF,
and byte[i]=(37*i+11) mod 256. Three more vectors use the last pattern with
bytes 76..79 replaced by little-endian nonces 1, 2 and 3. Scratchpads have no
initial contents requirement and are fully filled on each invocation.

The first four V1 vectors remain unchanged. The staged device arithmetic
uses a separate SHA3 construction and separate integer/line implementation
for independent parity checks; CPU emulation is not physical GPU testing.
