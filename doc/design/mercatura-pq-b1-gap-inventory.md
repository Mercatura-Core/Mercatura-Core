# B1 initial identity/lifecycle gap inventory

Base: `validation/pq-v2-integrated-hardening` at `1380d473de01049404af2af99a81063f19789153`.
B1 branch: `hardening/pq-p2p-b1-identity-lifecycle`.
Initial remote inspection found no `qualification/pq-v2-round-a-ci` branch and no local Round A worktree/branch. B1 therefore proceeds independently. Potential future overlaps are `net_pq.h/.cpp`, identity storage and test CMake; broad networking decoupling and CI cleanup remain Round A work. Existing branches, build directories and evidence are preserved.

| Boundary | Implemented and inherited coverage | B1 gap / investigation |
| --- | --- | --- |
| Ephemeral/persistent identity ownership | Strong OS-backed per-connection RNG; immutable shared persistent identities; separate wallet domains. Reconnect/persistence/rotation functional coverage already exists. | Deterministic concurrent signing, owner lifetime during cancellation, sibling isolation and construction failures. |
| POSIX identity files | Descriptor-based ownership/type/mode/network/key checks; 0600 creation before secret writes; no-clobber publication; restrictive-umask, links/FIFO/corruption/concurrent-creation tests. | Controlled open/read/short-write/EINTR/fsync/link/unlink failures, post-publication recovery, residual-temporary diagnostics and descriptor cleanup. |
| Pins/status | Canonical full fingerprints and exact destination spelling; duplicate configuration rejection; pins/signatures/key confirmation gate completed assurance. | Broader parser/configuration edges, status after every failed/cancelled stage, unrelated-session isolation. No normalization or trust-policy changes. |
| Secret wrappers | Noncopyable `Secret<>`, cleansing destructor/moves, RAII seeds/coins/IKM/PRK/confirmation/traffic keys; prior move/failure cleanup cases. | Exception paths in fallible RNG callbacks; live ownership/destruction observations using a test-only cleansing observer; document copies/spills/erasure limitations. |
| Handshake failure | Fixed state/size/network/signature/confirmation checks; `Fail` clears owned secrets; frozen vectors and mutation tests. | Check cleanup at each state, moved key ownership, exception/late-result cleanup and safe status publication. Protocol formulas remain frozen. |
| Async context/transport | Two owned workers, bounded queue, generation checks, atomic release/acquire completion, cancellation, quota RAII and join-on-shutdown. Prior silent-admission and allocation-exception fixes are present. | Barrier-controlled cancellation/publication/destruction, stale generations, partial worker startup, repeated/concurrent Stop and allocation exceptions at transport API boundaries. |
| Sanitizers | Focused crypto/protocol build plus ASan/UBSan and TSan adapter runners exist; prior successful results are historical. | Extend to new deterministic lifecycle and filesystem fault cases, instrument first-party wrappers, document exact source/dependency scope and current runtime capability. |

Previously corrected defects are preserved: `6edc643` identity creation under restrictive umask; `c7cf7b8` silent inbound accounting; `184bb13` exception-safe admission leases; `ea389cb` early invalid-KEM rejection. Existing implementation is not treated as absent.

Suspicions are not findings until reproduced. Initial candidates are exception cleanup in key-generation APIs, unguarded allocations in nonthrowing transport methods, concurrent Stop/join ownership, and suppressed temporary-file cleanup errors. No production change will be made solely to create activity.

B1 excludes sustained saturation/churn/soak/RSS profiling (B2), broad authenticated-peer/attack matrices and independent AKE approval (B3), and deployment/platform/GPU expansion. New hooks will exist only in excluded test executables; ordinary nodes will have no runtime hook activation.
