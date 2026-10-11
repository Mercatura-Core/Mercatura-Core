# Native NVIDIA and AMD mining (M5 development)

GPU hashing extends the M4 `MiningController`, `WorkProvider`, Solo and native
pool paths. Mining starts only on an explicit Start action. Software fees,
developer fees and automatic donations remain 0%. No consensus rules, wallet
signing operations, pool protocol, accounting, payout construction or blockchain
databases are changed. P2Pool remains M6.

## Hardware and backend selection

The Mining page offers CPU, GPU, and CPU + GPU. Automatic GPU selection uses all
compatible NVIDIA and AMD devices; manual checkboxes support either vendor,
mixed vendors and multiple devices. CPU worker settings retain M4 semantics.
Device discovery and initialization run outside the Qt thread. Start settings
are fixed for the session; Stop is asynchronous and shutdown joins workers
before node and wallet teardown.

NVIDIA uses CUDA. AMD uses full-profile OpenCL 1.2 or later on actual GPU devices
whose AMD vendor ID is `0x1002`. CPU OpenCL devices and NVIDIA OpenCL devices are
excluded. AMD discovery requires a 64-bit device address space, an available
compiler/device and sufficient allocation capacity. Actual compatibility depends
on the installed GPU driver and must be tested on the intended card.

| AMD technology | Assessment for this implementation |
| --- | --- |
| HIP/ROCm | Strong CUDA-style C++ execution model; Linux support and Windows HIP SDK support cover different, restricted hardware sets. Requiring a supported ROCm/HIP installation would narrow desktop distribution. |
| Vulkan compute | Widely deployed Linux/Windows drivers; 64-bit integer shaders are an optional capability. Storage-buffer limits and shader toolchain/descriptor management add complexity for many private 128 MiB mutable scratchpads. |
| OpenCL | Linux and Windows AMD GPU drivers expose compute through an ICD, with native unsigned 64-bit operations and large mutable buffers. Runtime compilation targets the installed device. Full-profile/version/capacity checks and the reference self-test gate actual use. |

OpenCL provides the most practical initial Linux/Windows AMD deployment path for
this repository. It does not imply that every AMD card or every Mesa/ROCm driver
is supported. There is no HIP/ROCm-only distribution requirement and no CPU
hashing delegation in the AMD production backend. The OpenCL ICD is loaded
from the system at runtime, so a missing GPU runtime leaves CPU mining available.

## Hashing and work flow

The authoritative implementation is `src/crypto/mercahash.cpp` and its frozen
V1 constants in `mercahash.h`: 128 MiB scratchpad, 2,097,152 fill lines, one binding
pass, 131,072 mixing steps, SHA3-512 checkpoints and SHA3-256 finalization. The
informal prompt's mixing-step summary is not used as an algorithm specification.

`mercahash_gpu.h` contains the actual device arithmetic compiled by CUDA and
embedded into the OpenCL program. Its C++ execution path exists only for
permanent arithmetic tests; neither production GPU backend invokes that path.
Both production backends allocate device memory and launch real device kernels.

Each context owns sixteen state lanes and a contiguous, private scratchpad.
CUDA launches one full warp in a separate block per context; OpenCL launches an
eight-item workgroup per context. Eight lanes cooperatively store each complete
64-byte line. A CUDA warp memory barrier / OpenCL global-memory barrier before
each store lets every lane finish reading the old line before mutation; a second
barrier after the store publishes the complete line for subsequent reads.
Private state is replicated across lanes
to preserve the exact serial recurrence; only the leader persists state and
outputs. Initialization/finalization execute on the leader. Separate groups can
run on separate multiprocessors even at conservative context counts, rather than
packing every context into one CUDA block. Partitioning state/ARX arithmetic
across lanes remains a performance optimization to evaluate after hardware
correctness. This is a correctness baseline with no claimed performance or power
figures. Group barriers and real compiler execution require device parity tests.

A hash is split into initialization, chunks of 4,096 fill lines, 64 binding
blocks, 1,024 mixing steps, and finalization. Cancellation and driver errors are
checked between synchronized launches. Chunk splitting preserves the exact
algorithm; no shortened workloads are exposed to mining callers. A driver call
itself cannot be forcibly interrupted safely. Kernel duration, watchdog behavior,
sustained responsiveness and driver-reset behavior require hardware qualification.

Only 80-byte public headers and resulting 32-byte hashes cross the device
boundary. Scratchpads stay on the device. The host prefilters all returned hashes
with Core's unsigned 256-bit comparison; there is no lossy device target filter.
Every target hit is rehashed with an independent authoritative `PoWHashContext`.
Both exact digest equality and the target check must pass before the shared
bounded candidate queue and existing provider submission can accept it.

CPU and GPU workers share the same saturating 32-bit nonce-space allocator.
Ranges are allocated once, never separately by vendor. Providers retain M4's
extranonce/session namespaces, refreshed-job creation, job validity tokens,
expiry, continuous-share behavior and submission rules. Stale GPU outputs are
discarded after cancellation or replacement. Each GPU verifier's CPU scratchpad
is included in the host memory budget.

The GPU budget leaves at least 512 MiB and half of the reported device budget for
other uses, checks the maximum single allocation, and allows at most 256 contexts
per device because the shared work ranges contain 256 nonces. Automatic selection
caps contexts at 16 for responsiveness. This limits contexts, not the number of
GPUs. OpenCL has no portable free-VRAM query: its conservative budget uses reported
total global memory, and allocation/launch failures disable the affected device.
The interface never claims that this value is measured free VRAM.

Per-device statistics expose hashes, rate, context count, allocated scratchpad
memory and status. CPU hashes and rate are separate from GPU totals. Device
failure stops that device, while healthy selected GPUs/CPU workers continue. If
all workers fail, the session fails visibly. Stop and Start retry device discovery
and construction; Refresh updates manual device selection. No driver-wide reset,
root/admin operation, shell power command, fan control or hidden fee is used.
Optional batch rest intervals allow reduced pressure. Board power and thermal
telemetry are not presented as available without a supported vendor API.

## Build and deployment

CPU-only builds default to both options OFF and require neither SDK. The GPU
backend interface has no vendor headers. macOS should use this CPU-only build;
GPU options deliberately reject unsupported macOS configurations.

```sh
cmake -S . -B work/build-cpu -DBUILD_GUI=ON -DENABLE_IPC=OFF
cmake --build work/build-cpu -j4

cmake -S . -B work/build-gpu -DBUILD_GUI=ON -DENABLE_IPC=OFF \
  -DWITH_CUDA_MINING=ON -DWITH_OPENCL_MINING=ON \
  -DCMAKE_CUDA_ARCHITECTURES='75-real;75-virtual'
cmake --build work/build-gpu -j4
```

Use CUDA 12 or later with a toolkit-supported host compiler/platform. Default
binaries target Turing (7.5) and include forward-compatible PTX. With CUDA 12,
explicitly include architectures 52/60/61 to qualify older Maxwell/Pascal cards.
Discovery enforces the lowest configured architecture. Configuration requires
forward-compatible PTX at that minimum: use a plain numeric architecture or add
its `-virtual` entry alongside `-real`. For example, `52-real;52-virtual;75-real`
can cover devices from 5.2 upward; `52-real;75-virtual` is rejected because some
newer major architectures would lack either a compatible cubin or usable PTX.
CUDA 13 no longer supports
compiling those older architectures; do not silently force unsupported targets.

CUDA links its static runtime; deployed machines need the NVIDIA driver, not the
compiler toolkit. Include the applicable NVIDIA redistributable license notices
when packaging CUDA-enabled binaries. Never redistribute a GPU driver with the
wallet or publish this development build as a qualified mining release.

AMD builds need Khronos OpenCL headers and an ICD development library for CMake's
SDK discovery. Linux commonly supplies `ocl-icd-opencl-dev`; the vcpkg manifest
adds the optional `gpu-amd` feature for Windows. The deployed machine needs a
system ICD loader and an AMD GPU driver implementing full-profile OpenCL. The
wallet dynamically loads `OpenCL.dll` from Windows System32 or `libOpenCL.so.1`
on Linux and reports absent runtimes without breaking CPU startup.

For Windows CUDA, use a toolkit-supported MSVC compiler (for CUDA 12.8, a
compatible Visual Studio 2022/v143 installation). The existing VS2026 M4 presets
and static Qt/Schannel configuration remain unchanged. Do not use
`--allow-unsupported-compiler` to misrepresent an unsupported MSVC build as
validated. A separate supported Windows CUDA toolchain must be selected explicitly.
AMD-only and CPU-only builds can retain the M4 toolchain. Optional
`vs2022-gpu` / `vs2022-gpu-static` presets select VS2022 v143 14.40 explicitly,
retain the matching M4 vcpkg/Qt linkage and `--clean-after-build`, and enable both
backends. Install that toolset and CUDA 12.8, set `CUDA_PATH` to that toolkit,
including its Visual Studio integration files under
`extras/visual_studio_integration/MSBuildExtensions`, then configure with
`cmake --preset vs2022-gpu-static`. The generator's `cuda=` toolset field selects
that exact toolkit, as well as setting the SDK root/compiler cache entries.
These presets are build
preparation, not evidence of an executed Windows build.

This development environment is Debian 13, GCC 14, glibc 2.41. NVCC 12.8 fails
before project code with conflicting `sinpi/cospi/sinpif/cospif` exception
specifications in CUDA versus glibc headers. Clang 19 with CUDA 12.8 compiles the
actual source to PTX and a complete host/device object without modifying either
SDK or system headers. Ubuntu 24.04 is the NVCC 12.8 CI build platform.

## Automated and hardware checks

Permanent `mercahash_gpu_tests` execute the same arithmetic as C++ and compare
all four frozen vectors to Core. They test independent contexts, scratchpad guard
words/reuse, irregular stage boundaries, SHA3 padding and 64-bit rotations.
`gpu_miner_tests` use explicitly simulated devices to exercise scheduling,
selection, budgets, cancellation, faults and authoritative candidate rejection.
They provide no evidence of actual GPU execution.

Every production device runs all four frozen vectors across its context slots
before it can mine. A mismatch disables the device. `mercatura-gpu-check` is a
hardware parity runner built when tests and a GPU backend are enabled:

```sh
work/build-gpu/bin/mercatura-gpu-check --list
work/build-gpu/bin/mercatura-gpu-check cuda:0
work/build-gpu/bin/mercatura-gpu-check opencl:0 test/data/mercahash-gpu-headers.txt
```

Exit 77 on a device-free listing is an explicit skip, not successful hardware
validation. The optional file contains one canonical 80-byte header per line,
encoded as 160 hexadecimal characters; each is compared against `HashV1`.

Before either vendor is release-ready, execute parity on each intended device,
then Solo/Pool and mixed CPU/NVIDIA/AMD mining, concurrent multi-device work,
nonce/extranonce exhaustion, tip/job changes, stale work, Stop/Start, wallet
lock/unload, shutdown, memory pressure, sustained mining, thermal/power behavior
and driver faults. Qualify Solo blocks through independent peers and native PQ
payout ownership, difficulty changes and reorganizations. Pool beta still needs
real accepted shares/blocks and correct native payouts. No actual GPU execution
or release qualification is inferred from compilation or software mocks.
