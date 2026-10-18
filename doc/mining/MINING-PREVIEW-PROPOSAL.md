# Mercatura Core — Mining Preview (proposal only)

Proposal only; do not publish a release or create a tag during this task.

Optional GitHub pre-release within Mercatura-Core/Mercatura-Core of the same
native wallet and blockchain, after reviewed integration and qualification.
Preserve application/network identity, consensus, ML-DSA rules, wallet format,
PQ rewards, native CPU/GPU/combined controls, safe budgets and permanent 0% fees.
Mining requires explicit Start. Preserve CPU fallback and AMD backend selection.
No clock/power/fan changes. No standalone miner, fork or extra chain storage.

Package only platforms and CUDA architectures built/tested on supported
compilers. Disclose each binary's enabled backends, driver requirements and
actual tested cards. Include source commit, build flags, checksums and CUDA
redistribution notices; use installed system drivers. Keep a CPU-only option.
Do not change datadir behavior; qualify using explicit fresh test datadirs.

Publication gates: original optimized source provenance; minimal reviewed port;
independent full/partial/repeated CPU parity; queued cancellation/error/cleanup
and UI lifecycle validation; accepted native GPU and combined regtest blocks
with owned native PQ rewards and spending; clean -j4/platform builds and focused
native/Qt/functional tests; stock RTX 3050 >=180s and longer stress at safe
production budgets. AMD physical claims require separate hardware results.

Release notes must distinguish developer benchmark (54.789 H/s, 36 contexts,
180s, 369/369 comparisons) from integrated wallet measurements. Never ship 36
as a universal default or promise benchmark speed under production headroom.
Choose version/tag only at a separate release decision. Explicit owner approval
is required before creating a release/tag, uploading assets or publishing.
No release workflow, tag, asset or publication was created during this task.
