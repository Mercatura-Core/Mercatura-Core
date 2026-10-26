# B1 secret ownership and erasure boundaries

This is an implementation inventory for Candidate 1, not a change to its key
schedule, domains, encodings, or trust model. See the final B1 validation report
for measured results and remaining qualification gates.

| Material | Owner and transfer | Cleanup boundary |
| --- | --- | --- |
| Identity generation seed (32 bytes) | Local move-only `Secret<32>` in `Identity::Generate`; protected identity-file creation/load uses a move-only 2127-byte `IdentityFile` | RAII calls `memory_cleanse` on normal return and exceptions; serialized seed buffer is never a general `Bytes` allocation |
| ML-DSA private identity (4032 bytes) | `Identity::priv`; production persistent loader returns `shared_ptr<const Identity>` to `Context`; worker-created ephemeral identity is shared only with its handshake | Final shared owner destroys the move-only secret; cancellation cannot destroy it while a worker is still signing; regeneration clears the previous key before RNG |
| ML-KEM private key (2400 bytes) | Worker-owned `Handshake::m_kem`; moved as part of the unique handshake ownership between transport and `Work` | `Schedule`, `Fail`, or destruction; failed worker jobs discard the handshake before completion publication |
| KEM generation/encapsulation coins (64/32 bytes), signing randomness (32 bytes), nonce scratch (32 bytes) | Local move-only `Secret` in crypto wrapper/handshake | Return or exception, including false/throwing RNG; nonce becomes public on the wire |
| Two KEM shared secrets (32 bytes each) | Handshake `m_ss_r`, `m_ss_i`; copied into a local `Secret<68>` length-prefixed IKM | IKM at scope exit; shared secrets and KEM private key at `Schedule`, or failure/destruction |
| HKDF PRK and confirmation keys (48 bytes each), expansion block (48 bytes) | Handshake `Secret` fields; expansion block is local `Secret<48>` | Confirmation fields at `Finish`/`Fail`/destruction; expansion block on every exit |
| HMAC pads, keyed SHA3 states, inner hash; identity checksum hash state | Local `CHMAC_SHA3_384` and checksum helper objects; HMAC is copyable but these call paths do not copy it | Existing explicit `memory_cleanse` of pads/hashes and HMAC destructor; checksum helper cleanses its SHA3 state |
| Four directional application keys (32 bytes each) | `SessionKeys`; `TakeKeys` moves and zeros the source; local moved keys initialize two unique packet ciphers | Moved-from secrets are zeroed; local keys on scope exit; all failure/unwind paths destroy owned keys |
| Packet key state and buffered keystream | Unique `PacketCipher` embeds existing noncopyable ChaCha/FSChaCha primitives | Underlying ChaCha destructors cleanse state and buffer; rekey cleanses temporary key; `PQTransport::Fail` destroys both ciphers |
| Native ML-KEM/ML-DSA internal temporaries | Pinned native implementation and its explicit cleanup routines | Retained upstream cleanup; B1 does not replace native crypto or prove complete register/stack-spill erasure |

`Secret` and `Identity` cannot be copied. Secret moves clear their source;
self-move leaves the existing value intact. A successful handshake releases its
identity reference after signatures and confirmation finish. The persistent
context retains its identity until the context itself is destroyed. `Stop`
joins workers and revokes use of transports; it does not promise to erase an
identity while a legitimate shared context owner still retains it.

Public identities, signatures, ciphertexts, transcript digests, nonces, session
IDs, fingerprints, and error classifications are not private key material.
Packet/message buffers can contain application plaintext; their ordinary
vector destruction is not a secure application-data erasure guarantee. PQ
identity creation, loading, pinning, and diagnostics do not use a wallet key,
wallet seed, consensus signature key, or wallet backup path.

The excluded Linux B1 harness observes `memory_cleanse` **after the real wipe
and while the storage remains valid**. It checks synthetic key/file buffers,
not freed memory. These observations support the stated ownership boundaries;
they do not prove whole-process erasure, operating-system swap/core protection,
filesystem secure deletion, or cleanup of every compiler-generated copy.

The immutable shared identity is safe for concurrent signing through the
production `shared_ptr<const Identity>` owners. An external caller retaining a
mutable alias must not mutate it concurrently. Parent-directory ownership and
mode/inode checks protect the selected storage location; hostile same-user/root
access, ancestor replacement, actual power-loss durability, and non-POSIX ACL
qualification remain separate limits. Windows persistent identity operations
retain their existing fail-closed behavior.
