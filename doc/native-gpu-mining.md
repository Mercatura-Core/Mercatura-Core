# Native NVIDIA and AMD mining (M5 development)

GPU hashing uses the native `MiningController`, `WorkProvider` and Solo provider. Mining starts only on an explicit Start action. Software fees,
developer fees and automatic donations remain 0%. No consensus rules, wallet
signing operations, payout construction or blockchain databases are changed.

## Hardware and backend selection

The Mining page offers CPU, GPU, and CPU + GPU. Automatic GPU selection uses all
compatible NVIDIA and AMD devices; manual checkboxes support either vendor,
mixed vendors and multiple devices. CPU workers are selected manually within the detected CPU and memory limits.
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

The authoritative V1 implementation is `src/crypto/mercahash.cpp` and its frozen
V1 constants in `mercahash.h`: 128 MiB scratchpad, 2,097,152 fill lines, one binding
pass, 131,072 mixing steps, SHA3-512 checkpoints and SHA3-256 finalization. The
informal prompt's mixing-step summary is not used as an algorithm specification.
`src/crypto/mercahash.h` defines `LINE_COUNT = SCRATCHPAD_BYTES / LINE_BYTES`
and asserts 2,097,152 lines. Its frozen `V1_MIX_STEPS` is 131,072;
`HashV1` in `src/crypto/mercahash.cpp` passes that constant to `MixScratchpad`,
whose loop runs from 1 through the supplied count. The larger number describes
scratchpad lines and fill iterations, not V1 mixing steps or total memory accesses.

`mercahash_gpu.h` supplies portable arithmetic and the unchanged AMD V1 path.
The authoritative V2 implementation is `src/crypto/mercahash_v2.cpp`.
AMD V2 uses `mercahash_opencl_v2.h`, reusing portable arithmetic with scalar fill/mix
and a workgroup-local parallel binding reduction. CMake embeds the two AMD
sources separately. See [AMD V2 optimization and qualification](mining/MERCAHASH-V2-AMD-OPENCL-OPTIMIZATION.md)
for the exact source, tests and outstanding physical AMD checks.
NVIDIA continues to use the separately imported `mercahash_gpu_specialized.h`,
unchanged by the AMD work. See
[CUDA integration and qualification](mining/CUDA-WALLET-INTEGRATION.md) for
source checksums, the exact port adjustments, tests and hardware requirements.
Neither production backend delegates hashing to the C++ test execution path.

Each context owns sixteen state lanes and a contiguous private scratchpad:
128 MiB for V1, 16 MiB for V2. The following legacy execution details describe V1.
CUDA uses one thread per context during fill, then a full 32-thread warp per
context for binding and mixing. Binding distributes each block's 64 lines across
the warp and combines lane accumulators with XOR shuffles. Mixing retains the
read-before-write and write-publication warp barriers. AMD retains its existing
eight-item workgroups and cooperative stores, with barriers before and after
each complete line mutation. Initialization/finalization execute on the leader.

CUDA fill launches cover 16,384 lines and synchronize every 32 kernels; binding
launches cover 64 blocks and synchronize every 32 kernels; mixing launches cover
1,024 steps and synchronize every 16 kernels. Dependencies remain ordered on
one nonblocking stream. Cancellation is checked before queued kernels and after
synchronized groups. Cancellation and exceptions drain outstanding work before
host buffers expire or device buffers are reused/freed. Default CUDA cache
behavior is preserved. AMD retains 4,096-line fill chunks and its existing
per-launch synchronization for V1. AMD V2 retains 4,096-line fill, 64-block binding
and 256-step mixing chunks, but uses dedicated kernels and at most four ordered
kernel enqueues before an explicit finish. Its scalar stages use one work-item;
binding selects a supported power-of-two workgroup from 8 to 64 with an OpenCL
1.2 local-memory XOR reduction. No MercaHash workload is shortened. Actual AMD
runtime parity, throughput and Stop latency remain pending physical validation.

A driver call itself cannot be forcibly interrupted safely. Kernel duration,
watchdog behavior, sustained responsiveness and driver-reset behavior require
hardware qualification. The developer's measured benchmark performance is not
an integrated-wallet performance claim.

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

The default **Automatic (maximum safe capacity)** memory mode selects each GPU's
largest safe context count independently. CUDA uses measured free VRAM after
stream initialization, reserving max(512 MiB, 5% of measured free VRAM), plus
64 MiB of estimated runtime overhead and 4 KiB per context in addition to each
private 128 MiB scratchpad. There is no 16-context automatic cap or half-VRAM
CUDA reserve. **Manual (reduce VRAM use)** lets miners choose fewer contexts to
conserve memory, improve desktop responsiveness or tune efficiency.

For example, 6 GiB of measured free CUDA memory permits 43 contexts under the
current budget; 24 GiB permits 181. These are capacity calculations, not measured
throughput recommendations. A manual 36-context setting needs 4,608 MiB of
scratchpads plus overhead and reserve (at least 5,184 MiB + 144 KiB). Neither
36 nor any GPU model's benchmark setting is hard-coded as a default or maximum.

Automatic passes a separate zero/default request to every backend; a smaller
GPU never supplies a count or a context limit to a larger GPU. Only the shared
Manual field uses the tightest limit among selected GPUs. The UI shows cached
CUDA free-memory readings, per-device context counts, estimated allocation/headroom,
and a warning below
max(1 GiB, 10% of reported free memory) of remaining headroom. Refresh runs off
the GUI thread and retains explicit device selections. Allocation also runs off
the GUI thread: CUDA re-queries free memory immediately before allocation, checks
every allocation, and checks actual remaining memory against the reserve afterward.
Automatic first attempts that maximum. Explicit device-buffer memory exhaustion
or loss of the required CUDA reserve triggers a bounded binary search for the
highest allocatable smaller count (at most 20 attempts with the current scheduler).
Every probe releases all mining buffers before the next budget query; the retained
allocation is freshly checked. A driver error during cleanup stops fallback;
final teardown attempts any failed releases again. Stop and shutdown cancel between attempts. If even
one context cannot fit, or memory keeps changing beyond the retry bound, the device
fails cleanly. Manual makes one attempt at the exact requested count and never
silently reduces it. Driver, kernel, build and hash-verification errors do not
trigger allocation retries. Other applications can still consume VRAM later.

The Mining tab reports each GPU's actual active contexts independently. When
Automatic falls back, it shows the original estimate and final count, for example:
“RTX 3050 — Automatic: 39 contexts active (43 estimated; adjusted for available
VRAM).” Stopped devices are not reported as having active allocations.

Core OpenCL has no portable free-VRAM query. AMD Automatic therefore selects the
largest count within a conservative fallback: at most a quarter of reported
global memory, reduced by overhead and bounded by `CL_DEVICE_MAX_MEM_ALLOC_SIZE`
and host address space. Manual can reduce that count. This is a capability-based
allocation ceiling, not a claim that any portion is currently free. Every
allocation is checked. Only `CL_MEM_OBJECT_ALLOCATION_FAILURE` from device-buffer
creation permits the same bounded Automatic search, with the conservative budget
rechecked each time. Unknown free memory remains unknown; ambiguous driver/resource
and host-memory errors are not retried. An unrecoverable allocation/launch failure releases the affected backend's
resources and leaves healthy GPUs/CPU workers running. Its UI reports global
memory and explicitly warns that free VRAM is unknown; total or unallocated
budget is never presented as a measurement of free memory. These memory policies
do not change either backend's hash arithmetic.

### Remaining scheduler limit

Both modes currently allow at most **256 contexts per GPU** (32 GiB of
scratchpads). The shared CPU/GPU nonce allocator leases 256 nonces at a time;
larger buffers would be partly idle under this scheduler. Memory previews flag
devices whose usable capacity exceeds this bound. A static assertion couples the
backend count limit to the lease size, and boundary tests retain nonce exhaustion
without wraparound. Mixed-capacity regression tests use distinct full/partial
batches and check that devices never reuse each other's nonces.

Supporting more than 256 is deferred, rather than merely raising a constant.
It requires larger or variable GPU leases alongside CPU leases, no overlap or
overflow at the final 32-bit nonce, audited scratchpad/address/launch bounds,
and full/partial/repeated/cancelled/stale-work tests at counts above 256. Launch
group latency and independent verification also need qualification on hardware.

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
`mercahash_gpu_sync_tests` executes cooperative stores in a controlled host
thread model and rejects writes before every lane can read the old line. It
protects the pre-store barrier regression; it does not execute a GPU memory model.
`gpu_miner_tests` use explicitly simulated devices to exercise scheduling,
selection, budgets, cancellation, faults and authoritative candidate rejection.
They provide no evidence of actual GPU execution. `cuda_binding_tests` models
the imported fill and warp binding against the original portable arithmetic.
With CUDA enabled on Linux, `test_cuda_backend_host` links the real backend host
object to a fake runtime and checks all cancellation checkpoints, launch geometry,
full/partial batches, injected errors and cleanup without running a GPU. The
wallet GPU-controller acceptance test also uses an explicitly CPU-backed device
test double; it validates native PQ block submission, not hardware hashing.

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
encoded as 160 hexadecimal characters. The checker always generates six varied
batches adapted to the safe context count and compares every output independently
against Core `HashV1` across three repetitions. File inputs are also checked in
full batches and a final partial batch. Optional `--contexts N` requests a count
within the unchanged production budget, and `--repetitions N` controls repeated
checks. A request for 36 on a 6 GB card is expected to exceed that budget.

Before either vendor is release-ready, execute parity on each intended device,
then Solo and mixed CPU/NVIDIA/AMD mining, concurrent multi-device work,
nonce/extranonce exhaustion, tip/job changes, stale work, Stop/Start, wallet
lock/unload, shutdown, memory pressure, sustained mining, thermal/power behavior
and driver faults. Qualify Solo blocks through independent peers and native PQ
payout ownership, difficulty changes and reorganizations.
No actual GPU execution or release qualification is inferred from compilation
or software mocks.
