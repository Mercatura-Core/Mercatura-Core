# P5/P6/P7 implementation audit: initial hardening checkpoint

This implementation audit starts at verified clean `v0.1` commit
`4ae99795e7fb9ca6ec8ea22357826a0263a1e660`. It is the implementing agent's
source/coverage review, not an independent security audit. Authorization remains
isolated Stage A regtest. The final tested checkpoint and execution evidence are
recorded in `mercatura-pq-stageb-hardening-validation.md`; its requested filename
does not authorize Stage B public-testnet operation.

## Existing implementation and coverage map

| Boundary | Implementing functions/files | Baseline evidence and initial gap |
| --- | --- | --- |
| Fresh identity generation | `SecureRandom`, `Context::Run` in `net_pq.cpp`; `Identity::Generate/FromSeed/Sign` in `crypto/mercatura_p2p_crypto.cpp` | Per-connection generation, trusted strong RNG, separate wallet domains and RAII secret ownership present; needs concurrent/failure/recovery review |
| Persistent identity | `LoadIdentity` in `net_pq_identity.cpp` | Descriptor-based open/type/owner/mode checks, exact format/network/checksum/key comparison and atomic no-clobber creation present; initial automated coverage only eight assertions |
| Pins | CLI parsing in `init.cpp`, exact configured destination lookup in `MakeTransport`, `Handshake::CheckPeer` and signature checks | Correct/incorrect pins tested; explicit destination-port errors, alternate spellings and recovery/rotation need coverage |
| Handshake | `Handshake::Start/Advance/Schedule/Finish` in `net_pq_protocol.cpp` | Exact immutable Candidate 1 fixture and sampled mutations pass; systematic field and concurrent-session substitution matrix incomplete |
| Packets and commands | `PacketCipher`, private `DecodeCommand`, `PQTransport::ReceivedBytes/GetReceivedMessage` | Independent 1,348-packet rekey fixture present; authenticated malicious endpoint, command edge cases and maximum payload need stronger coverage |
| Admission | `Context::Admit/Release`, `SourceGroup`, `PQTransport` constructor | 64 global/4 known/16 anonymous, token buckets and bounded table present; inbound sockets initially charged only after valid header |
| Workers | `Context::Submit/Run/Stop`, `Work`, `PQTransport::Queue/Poll/Fail` | Two workers/32 queued, one job/session, release/acquire publication, cancellation-owned state and deadlines present; more stage-by-stage and full-adapter sanitizer/race coverage needed |
| Cleanup | `Secret`, native zeroization macros, `Handshake::Fail/Finish`, ChaCha destructors | RAII paths and upstream stack-buffer clearing present; destruction/move/failure regression and measured allocation retention need coverage |
| Status | `PQTransport::GetInfo`, `CNode::CopyStats`, `getpeerinfo` | Presented-key and pinned identity assurance separated; aggregate resource/rejection counters absent |
| Functional validation | `feature_pq_transport.py`, `feature_pq_transport_negative.py` | Real relay/reconnect and 24 wire negatives present; three-node IBD/reorg, stronger malicious authenticated peer and bounded soak absent |
| Fuzz/sanitizers | `pq_handshake`, `pq_packet`, fixed-seed smoke runner | Existing smoke is not guided fuzzing; identity/header/command/stream targets and full-adapter coverage needed; leak/race tools previously unqualified |

The corrected specification and existing candidate/primitive JSON fixtures were
read and checked. Candidate 1 SHA-256 remains
`6222abf5163ee34a987fe383a8e10b07e50abf1a3ab93f2a0e031a50dd9415f5`;
the corrected specification remains
`5960a8a1f032280f3a4bacb9aad07331eb1cae975e566e657e58d8d84ddc2e42`.
The prior audit/validation documents are historical checkpoints; their original
unrun statuses are not current qualification evidence.

## Prioritized initial findings

1. **Confirmed identity-creation defect:** umask `0777` produces mode `0000`,
   while creation reports success; immediate reload fails. Reproducer failed
   before correction. `6edc643` sets and validates mode 0600 on the opened inode
   before writing secrets. Atomic creation/type/recovery regressions pass.
2. **Confirmed admission-accounting gap:** silent inbound sockets are absent
   from the PQ pending/source/anonymous quotas until their header arrives.
   Existing socket limits and the five-second deadline still apply, but the
   PQ quotas do not cover that population. `c7cf7b8` tightens local admission
   to socket/transport construction without running crypto early or changing
   the wire. A reproducer and the adjacent adapter suite pass after correction.
3. **Coverage and measurement gaps:** protocol-field/concurrent-session attacks,
   file/pin/command fuzzability, aggregate resource measurements, full-adapter
   sanitizers, lifecycle stress and a reproducible multi-node soak are required
   before a defensible hardening checkpoint. Presence of existing bounds alone
   does not close these gaps.
4. **Unresolved external gates:** independent AKE/combiner analysis, genuinely
   independent security approval, sustained public-network qualification,
   cross-platform release testing, decentralized bootstrap and approved combined
   native-mining qualification remain outside this Stage A exercise.

An unpinned MITM using its own valid keys, a stolen pinned private key and a
compromised live endpoint are outside the expected-peer/endpoint guarantees.
The public checksum does not authenticate a maliciously rewritten seed file.
Owner-only directories and descriptor checks do not defend against a hostile
process with the same user privileges or root. Neither signatures nor pins grant
consensus trust, Sybil resistance or resource exemptions.
