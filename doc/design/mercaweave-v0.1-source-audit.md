# MercaWeave v0.1 source audit

Research base: freshly fetched `project/v0.1`,
`4b88b619fb1b08c7f30112a38eb471880b2a6af4`, verified against `git ls-remote`
and GitHub's live branch reference on 2026-11-02. Branch:
`research/mercaweave-v0.1`; sibling worktree:
`/workspace/Mercatura-MercaWeave-v0.1`. Original checkout remains clean on
`work` at that SHA. Added the previously absent `project` remote pointing
to the same canonical repository as `origin`; no push authorized or performed.

Authority: the user's self-contained Work Order 01 embeds the locked spec
and November 2 directive. The directive prohibits all activation work and
production integration. No protocol amendment is made by this prototype.

## Cryptography and serialization

- `src/crypto/mercatura_mldsa.{c,h}`: keypair-from-seed, sign, verify;
  return 1 on success. Sizes 32/32/1952/4032/3309 agree with the spec.
  `build_prefix` implements FIPS 204 pure mode `00 || ctxlen || ctx`.
- `src/crypto/mldsa_native/MERCATURA_PIN.txt`: v2.0.0,
  `834a90d5e846ffa1e1611bd24e160bb2e9b86d35`. Existing portable single
  compilation unit is reused without modification. Parameter set 65;
  `MLD_CONFIG_CORE_API_ONLY` for the implementation matches the existing build.
- `src/crypto/mercatura_pqhash.{cpp,h}`: `MercaturaPQHashWriter` prefixes
  CompactSize tag length and ASCII tag; `PQH384` returns raw 48-byte SHA3-384.
  Group domains must differ from existing `Mercatura/PQAuth/v1/*` domains.
- `src/crypto/mercatura_pqkey.cpp`: v2 commitment uses tagged SHA256 over PK
  only; v3 must separately commit genesis, PK, and nonce under its locked tag.
- `src/crypto/sha3.{cpp,h}`, `hkdf_sha3_384.{cpp,h}` and
  `mercatura_pqderive.cpp`: compatible SHA3-384/HMAC/HKDF-Expand32 primitives;
  v2 namespace and branch restriction stay unchanged. The research derivation
  uses the separate group salt/info and purposes 0/1/2.
- `src/primitives/transaction.{cpp,h}`: `SerializeTransaction` implements
  existing witness marker/flag and per-input vector serialization. `COutPoint`,
  `CTxOut`, and `CScript` serializers provide exact consensus bytes.
  Version is uint32_t; amounts are int64_t; `uint256` serializes raw bytes.
  Its display hexadecimal reverses bytes: do not hash display strings.

## Authorization and execution boundaries

- `src/script/interpreter.{cpp,h}`: `PrecomputedTransactionData::Init`
  precomputes five v2 PQ commitments when v2 inputs or `force` request them.
  `ComputeMercaturaPQAuthDigestV1` includes version/locktime, LE64 counts,
  genesis, all five commitments, and per-input index. Group hashes require
  separate domains and manifest; they cannot reuse cached v2 values.
- `GenericTransactionSignatureChecker::CheckMercaturaPQSignature` uses
  fixed `Mercatura/PQAuth/v1` context and calls the pinned wrapper directly.
  `CheckMercaturaPQWitnessStructureV1` requires two items, correct lengths,
  correct commitment, and rejects P2SH wrapping. Existing v2 is untouched.
- `VerifyWitnessProgram` reserves all v2 lengths. Unknown versions can
  succeed absent discouragement flags, so production's permissive unknown-v3
  behavior must never be mistaken for MercaWeave validation.
- `src/validation.cpp`, `CheckInputScripts`: authenticated outputs come
  from `CCoinsViewCache::AccessCoin` in vin order; one `CScriptCheck` per input.
  Block validation queues checks asynchronously with `CCheckQueueControl`.
  Followers cannot rely on anchor execution order. Future integration needs
  complete transaction-scoped group validation before independent script checks,
  with a fail-closed immutable result bound to witness, genesis, flags, and
  authenticated prevouts at every validation entrypoint.
- `src/script/sigcache.cpp`: existing signature cache overrides ECDSA and
  Schnorr verification; the PQ verifier currently calls ML-DSA directly.
  Do not assume an existing per-signature PQ cache. `CheckInputScripts`' full
  script cache binds wtxid, flags and optional PQ genesis; authenticated-view
  correctness is assumed before use. A future group cache needs explicit
  version/domain separation and `(digest, PK, signature)` binding. Current
  group ownership must be checked even on a signature-cache hit.

## Resource, wallet, PSBT and build review

- `src/consensus/consensus.h`: PQ cost is 4; block sigop cost limit 80,000;
  initial capacity 4 MiB. Locked group limits 4096/256/1024 and `4G+N`
  are additional research rules, not replacements for chain resource checks.
- `src/consensus/witness_limits.h`: 1003 stack items; item maximum 32 MiB;
  shared block witness parse budget based on capacity/256, with rollback guards.
  Group witnesses fit per-stack and per-item bounds; a standalone transaction
  does not establish remaining block parse budget.
- `src/policy/policy.{h,cpp}`: `MAX_STANDARD_TX_WEIGHT=400000`;
  `GetTransactionFeeWeight` charges complete serialized bytes times four,
  with sigop adjustment. `GetTransactionFeeSize` differs from inherited
  `GetVirtualTransactionSize`. `src/consensus/validation.h` supplies weight
  and full-byte capacity helpers. Neither is changed.
- `src/script/sign.cpp`: v2 solver/signing handling stays unchanged.
  `src/wallet/wallet.cpp` signs v2 using `ComputeMercaturaPQAuthDigestV1`
  and strong random signing bytes, with secret cleansing.
  `src/wallet/spend.cpp` estimates 41 base plus 5268 witness bytes per v2
  input; coin selection and fee estimation remain production v2 paths.
- `src/psbt.{cpp,h}`: existing `Mercatura` proprietary fields store fixed-size
  v2 PK/signature. Future `MercaWeave` fields require a separate namespace;
  no production PSBT edits in this work order.
- Existing PQ tests (`src/test/pq_assurance_tests.cpp` and other PQ tests),
  `src/bench/mercatura_pq.cpp`, and `src/crypto/CMakeLists.txt` establish
  wrapper configuration and test/benchmark patterns. A standalone CMake
  project beneath `src/test/mercaweave` avoids inherited application
  dependencies and is never included in production builds.

## Findings and limits

No normative contradiction was found in the audited domains, fixed bytes,
group grammar, or primitive interfaces. Independent Python reproduced all ten
locked Section 12 values and lengths 2030/296/2085. This is compatibility
evidence, not a security proof. Shared-key compromise, key-revelation linkability,
coordination of the complete manifest, and randomized wtxid variability remain
accepted trade-offs.

The standalone API requires caller-authenticated spent outputs; it cannot
establish maturity, chain membership, finality, or block-wide budgets. Non-v3
inputs must be independently validated through a callback; omission fails
closed. No cache is implemented, eliminating stale local cache acceptance.
Future production integrations, wallet/RPC/PSBT operations, reorgs and mempool
behavior require separate work and are not implied by prototype success.
