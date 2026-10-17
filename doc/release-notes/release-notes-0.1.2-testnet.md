# Mercatura Core v0.1.2 — Public Testnet

Mercatura Core v0.1.2 introduces native solo mining in the Qt wallet, wallet/network hardening, and a public-testnet consensus upgrade.

**New Mining tab — choose a mining mode:**
- **CPU Mining:** Native MercaHash solo mining using your processor.
- **GPU Mining (Beta):** Experimental NVIDIA CUDA and AMD OpenCL acceleration on supported **Windows/Linux GPU-enabled builds**.
- **CPU + GPU Mining (Beta):** Use CPU and compatible GPU devices in one solo-mining session.

Mining includes manual CPU-worker control, resource-aware limits, wallet-owned post-quantum reward addresses, and live hashrate/session statistics. **All native mining modes charge 0% mining software fees, 0% developer fees, and 0% automatic donations.** Mining never starts automatically at wallet launch.

**Help test the GPU beta!** NVIDIA/AMD GPU and combined CPU+GPU mining need **real-world hardware testing**. Compatible-device owners are invited to try the beta, report card/driver/OS details, hardware-detected status, reference-vector parity, performance, stability, thermal and power observations, and any bugs. CUDA/OpenCL are experimental; **compilation and simulated tests do not establish live GPU correctness or performance**. macOS ARM64 release is currently **CPU-only**.

**Other improvements:** Post-quantum wallet rescan and encrypted-wallet persistence hardening; networking, validation, mining-worker reliability, and block-storage safeguards.

**IMPORTANT — hard fork at public-testnet block 8,000:** Upgrade all testnet nodes and miners **before height 8,000**. From block 8,000, maximum public-testnet block size increases from **1 MiB** to **4 MiB (4,194,304 full serialized bytes)**. Earlier blocks retain the prior rule; unupgraded nodes can reject otherwise-valid larger blocks. Do not claim that a public-testnet change is a mainnet launch. Existing wallet and chain data should normally be preserved for an ordinary upgrade, with backups and graceful shutdown before replacing running software.

Testnet MCA has no monetary value. Verify downloads against `SHA256SUMS`.
