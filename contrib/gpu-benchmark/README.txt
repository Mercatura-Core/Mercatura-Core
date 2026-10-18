Mercatura standalone MercaHash V1 GPU benchmark (development qualification tool)

Requires Windows x64 with a supported NVIDIA driver and CUDA compute capability
7.5 or newer. Neither a CUDA Toolkit installation nor the Mercatura wallet/node
is needed. Linux packages target Ubuntu 24.04 x86-64 or compatible system runtimes.
This tool uses synthetic inputs. It does not open node configuration, wallet or
blockchain data, connect to peers, submit blocks, or control another process.

Run in PowerShell from this extracted directory:
  .\mercatura-gpu-bench.exe --help
  .\mercatura-gpu-bench.exe --list
  .\mercatura-gpu-bench.exe --device cuda:0 --contexts 16 --duration 60 --warmup 5 --json > .\benchmark-results\rtx3050-c16.json
  .\mercatura-gpu-bench.exe --device cuda:0 --contexts 20 --duration 60 --warmup 5 --json > .\benchmark-results\rtx3050-c20.json
  .\mercatura-gpu-bench.exe --device cuda:0 --contexts 16 --duration 60 --warmup 5 --stage-timing --json > .\benchmark-results\rtx3050-c16-timed.json

Only attempt counts admitted by --list. Automatic mode (--contexts 0) is capped
at 16; manual counts remain subject to the existing VRAM reserve. A nominal
6GB card is not guaranteed to admit 20 contexts. No count is silently clamped.

Mandatory frozen-vector and CPU comparisons can take time before warmup begins.
They are excluded from measured throughput, as are setup and warmup. The final
batch finishes and counts in full; its overrun is included in elapsed time.
Progress goes to stderr; --json writes one JSON result or error to stdout.
Stage timing adds overhead: compare it to a separate run without --stage-timing.
Synchronization wait overlaps GPU execution and must not be added to GPU time.
Resource occupancy values are theoretical; local memory is not a measured spill
count. Use NVIDIA Nsight for achieved occupancy, spills and launch scheduling.
Exit codes: 0 success, 1 failure, 77 no usable NVIDIA device with --list.
Compilation or no-device checks do not qualify GPU correctness or performance.

See GPU-BENCHMARK.md for methodology, build instructions and reproducibility.
See notices/ for Mercatura, CUDA redistributable and dependency licenses.
