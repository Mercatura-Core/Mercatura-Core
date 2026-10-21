// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#include <algorithm>
#include <charconv>
#include <cstring>
#include <crypto/mercahash.h>
#include <limits>
#include <mining/gpu_backend.h>
#include <mining/opencl_source.h>
#include <stdexcept>
#include <util/string.h>
#include <string_view>
#ifdef WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace mining
{
namespace
{
void Check(cl_int result, const char *operation)
{
    if (result != CL_SUCCESS)
        throw std::runtime_error{std::string{operation} + " failed (OpenCL " + util::ToString(result) + ")"};
}
bool SupportsOpenCl12(std::string_view version)
{
    // CL_DEVICE_VERSION is "OpenCL major.minor vendor information". Parse
    // bounded integers so malformed driver metadata cannot overflow a field.
    constexpr std::string_view prefix{"OpenCL "};
    if (version.size() > 4096 || !version.starts_with(prefix))
        return false;
    version.remove_prefix(prefix.size());
    unsigned int major{0}, minor{0};
    const char *end{version.data() + version.size()};
    const auto [major_end, major_error]{std::from_chars(version.data(), end, major)};
    if (major_error != std::errc{} || major_end == end || *major_end != '.')
        return false;
    const auto [minor_end, minor_error]{std::from_chars(major_end + 1, end, minor)};
    if (minor_error != std::errc{} || (minor_end != end && *minor_end != ' '))
        return false;
    return major > 1 || (major == 1 && minor >= 2);
}
// Load the system ICD at runtime. GPU-enabled packages can still run CPU mining
// on machines without OpenCL.dll/libOpenCL.so or an installed vendor driver.
class Api
{
#ifdef WIN32
    HMODULE library{nullptr};
#else
    void *library{nullptr};
#endif
  public:
#define CL_FUNCTIONS(X)                                                                                                \
    X(clGetPlatformIDs)                                                                                                \
    X(clGetDeviceIDs)                                                                                                  \
    X(clGetDeviceInfo) X(clCreateContext) X(clReleaseContext) X(clCreateCommandQueue) X(clReleaseCommandQueue)         \
        X(clCreateBuffer) X(clCreateSubBuffer) X(clReleaseMemObject) X(clCreateProgramWithSource) X(clBuildProgram)    \
            X(clGetProgramBuildInfo) X(clReleaseProgram) X(clCreateKernel) X(clReleaseKernel) X(clSetKernelArg)        \
                X(clEnqueueNDRangeKernel) X(clEnqueueWriteBuffer) X(clEnqueueReadBuffer) X(clFinish)
#define DECLARE(name) decltype(&::name) name{nullptr};
    CL_FUNCTIONS(DECLARE)
#undef DECLARE
    Api()
    {
#ifdef WIN32
        library = LoadLibraryExW(L"OpenCL.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
        library = dlopen("libOpenCL.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
        if (!library)
            throw std::runtime_error{
                "AMD OpenCL runtime is unavailable; install the system ICD loader and AMD GPU driver"};
        try {
#define LOAD(name) name = reinterpret_cast<decltype(name)>(Symbol(#name));
            CL_FUNCTIONS(LOAD)
#undef LOAD
        } catch (...) {
            Close();
            throw;
        }
    }
    ~Api()
    {
        Close();
    }
    Api(const Api &) = delete;
    Api &operator=(const Api &) = delete;

  private:
    void *Symbol(const char *name)
    {
#ifdef WIN32
        auto symbol{reinterpret_cast<void *>(GetProcAddress(library, name))};
#else
        auto symbol{dlsym(library, name)};
#endif
        if (!symbol)
            throw std::runtime_error{std::string{"OpenCL runtime lacks "} + name};
        return symbol;
    }
    void Close()
    {
        if (!library)
            return;
#ifdef WIN32
        FreeLibrary(library);
#else
        dlclose(library);
#endif
        library = nullptr;
    }
};
std::vector<cl_device_id> Devices(Api &api)
{
    cl_uint count{0};
    const auto status{api.clGetPlatformIDs(0, nullptr, &count)};
    if (status == -1001)
        return {}; // CL_PLATFORM_NOT_FOUND_KHR
    Check(status, "Enumerate OpenCL platforms");
    if (count > 1024)
        throw std::runtime_error{"OpenCL platform count exceeds discovery safety limit"};
    std::vector<cl_platform_id> platforms(count);
    if (count)
        Check(api.clGetPlatformIDs(count, platforms.data(), nullptr), "Read OpenCL platforms");
    std::vector<cl_device_id> devices;
    for (const auto platform : platforms) {
        cl_uint size{0};
        const auto result{api.clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &size)};
        if (result == CL_DEVICE_NOT_FOUND)
            continue;
        Check(result, "Enumerate OpenCL GPUs");
        if (size > 4096)
            throw std::runtime_error{"OpenCL device count exceeds discovery safety limit"};
        std::vector<cl_device_id> candidates(size);
        if (size)
            Check(api.clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, size, candidates.data(), nullptr),
                  "Read OpenCL GPUs");
        for (const auto device : candidates) {
            cl_uint vendor{0};
            Check(api.clGetDeviceInfo(device, CL_DEVICE_VENDOR_ID, sizeof(vendor), &vendor, nullptr),
                  "Read OpenCL vendor");
            // Never enumerate CPU devices or duplicate NVIDIA through OpenCL.
            if (vendor == 0x1002)
                devices.push_back(device);
        }
    }
    return devices;
}
template <typename T> T Info(Api &api, cl_device_id device, cl_device_info field)
{
    T value{};
    Check(api.clGetDeviceInfo(device, field, sizeof(value), &value, nullptr), "Read OpenCL capability");
    return value;
}
std::string Text(Api &api, cl_device_id device, cl_device_info field)
{
    size_t size{0};
    Check(api.clGetDeviceInfo(device, field, 0, nullptr, &size), "Read OpenCL text size");
    if (!size || size > 4096)
        throw std::runtime_error{"Invalid OpenCL device description"};
    std::string value(size, '\0');
    Check(api.clGetDeviceInfo(device, field, size, value.data(), nullptr), "Read OpenCL text");
    while (!value.empty() && value.back() == '\0')
        value.pop_back();
    return value;
}
std::optional<uint64_t> FreeMemory(Api& api, cl_device_id device)
{
    // cl_amd_device_attribute_query reports free memory in KiB. The core
    // OpenCL global-memory property is a capacity, never a free-memory query.
    const auto extensions{" " + Text(api, device, CL_DEVICE_EXTENSIONS) + " "};
    if (extensions.find(" cl_amd_device_attribute_query ") == std::string::npos) return {};
    cl_ulong free_kib[2]{};
    constexpr cl_device_info global_free_memory_amd{0x4039};
    if (api.clGetDeviceInfo(device, global_free_memory_amd, sizeof(free_kib), free_kib, nullptr) != CL_SUCCESS ||
        free_kib[0] > std::numeric_limits<uint64_t>::max() / 1024) return {};
    return std::min<uint64_t>(free_kib[0] * 1024, Info<cl_ulong>(api, device, CL_DEVICE_GLOBAL_MEM_SIZE));
}
class OpenClBackend final : public GpuBackend
{
    const mercahash::Version version;
    Api api;
    cl_context context{nullptr};
    cl_command_queue queue{nullptr};
    cl_program program{nullptr};
    cl_kernel init{nullptr}, stage{nullptr};
    cl_mem headers{nullptr}, states{nullptr}, scratch{nullptr}, outputs{nullptr};
    unsigned int contexts{0}, estimated{0};
    uint64_t memory_bytes{0};
    unsigned int execution_limit{0}, next_context{0};
    GpuMemoryBudget allocation_budget;

  public:
    OpenClBackend(const GpuDevice &selected, unsigned int requested, const std::function<bool()>& cancelled) : version{selected.pow_version}
    {
        try {
            const auto devices{Devices(api)};
            if (selected.ordinal >= devices.size())
                throw std::runtime_error{"Selected AMD device is no longer available"};
            const auto device{devices[selected.ordinal]};
            if (Text(api, device, CL_DEVICE_NAME) != selected.name)
                throw std::runtime_error{"AMD device enumeration changed; refresh device selection"};
            if (!SupportsOpenCl12(Text(api, device, CL_DEVICE_VERSION)))
                throw std::runtime_error{"AMD GPU requires OpenCL 1.2 or later"};
            if (version == mercahash::Version::V2) {
                const auto alignment{Info<cl_uint>(api, device, CL_DEVICE_MEM_BASE_ADDR_ALIGN)};
                if (!alignment || alignment % 8 || mercahash::V2_SCRATCHPAD_BYTES % (alignment / 8) ||
                    Info<cl_uint>(api, device, CL_DEVICE_ADDRESS_BITS) != 64 || Info<size_t>(api, device, CL_DEVICE_MAX_WORK_GROUP_SIZE) < 8)
                    throw std::runtime_error{"AMD V2 scratchpad alignment or execution geometry is unsupported"};
                execution_limit = GpuExecutionContexts(std::numeric_limits<unsigned int>::max(),
                    Info<cl_uint>(api, device, CL_DEVICE_MAX_COMPUTE_UNITS), std::numeric_limits<size_t>::max() / 8);
            }
            cl_int error;
            context = api.clCreateContext(nullptr, 1, &device, nullptr, nullptr, &error);
            Check(error, "Create OpenCL context");
            queue = api.clCreateCommandQueue(context, device, 0, &error);
            Check(error, "Create OpenCL queue");
            const char *source{OPENCL_MERCAHASH_SOURCE};
            const size_t size{std::strlen(source)};
            program = api.clCreateProgramWithSource(context, 1, &source, &size, &error);
            Check(error, "Create OpenCL program");
            const std::string options{version == mercahash::Version::V2 ? "-cl-std=CL1.2 -DMH_VERSION=2" : "-cl-std=CL1.2 -DMH_VERSION=1"};
            error = api.clBuildProgram(program, 1, &device, options.c_str(), nullptr, nullptr);
            if (error != CL_SUCCESS) {
                size_t length{0};
                api.clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &length);
                std::string log{"Build log unavailable or exceeds the 16 KiB safety limit"};
                if (length && length <= 16384) {
                    log.assign(length, '\0');
                    api.clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, length, log.data(), nullptr);
                }
                throw std::runtime_error{"AMD MercaHash OpenCL compilation failed (" + util::ToString(error) +
                                         "): " + log};
            }
            init = api.clCreateKernel(program, "mh_init", &error);
            Check(error, "Create OpenCL init kernel");
            stage = api.clCreateKernel(program, "mh_stage", &error);
            Check(error, "Create OpenCL stage kernel");
            const auto allocation{AllocateGpuContexts(requested, [&] {
                // OpenCL total memory is not measured free VRAM. Retain the
                // conservative global-pool and single-allocation limits.
                const auto free{version == mercahash::Version::V2 ? FreeMemory(api, device) : std::nullopt};
                return CalculateGpuBudget(free.value_or(Info<cl_ulong>(api, device, CL_DEVICE_GLOBAL_MEM_SIZE)),
                    Info<cl_ulong>(api, device, CL_DEVICE_MAX_MEM_ALLOC_SIZE), 0,
                    free ? GpuMemorySource::OPENCL_FREE : GpuMemorySource::OPENCL_TOTAL, version);
            }, [&](unsigned int count, const GpuMemoryBudget& budget) {
                contexts = count;
                memory_bytes = count * uint64_t{mercahash::ScratchpadBytes(version)};
                auto allocate = [&](cl_mem &buffer, size_t bytes) {
                    buffer = api.clCreateBuffer(context, CL_MEM_READ_WRITE, bytes, nullptr, &error);
                    if (error == CL_MEM_OBJECT_ALLOCATION_FAILURE || (version == mercahash::Version::V2 && error == CL_OUT_OF_RESOURCES))
                        throw GpuMemoryAllocationError{"Insufficient OpenCL device buffer memory"};
                    Check(error, "Allocate OpenCL mining buffer");
                };
                allocate(headers, contexts * size_t{80});
                allocate(states, contexts * size_t{128});
                allocate(scratch, static_cast<size_t>(memory_bytes));
                allocate(outputs, contexts * size_t{32});
                allocation_budget = budget;
                allocation_budget.contexts = count;
                allocation_budget.scratchpad_bytes = memory_bytes;
                allocation_budget.overhead_bytes = (64ULL << 20) + uint64_t{count} * 4096;
                allocation_budget.headroom_bytes += budget.scratchpad_bytes + budget.overhead_bytes - memory_bytes - allocation_budget.overhead_bytes;
                if (version == mercahash::Version::V2) {
                    if (const auto free{FreeMemory(api, device)}) {
                        allocation_budget.headroom_bytes = *free;
                        allocation_budget.headroom_measured = true;
                        if (*free < budget.reserved_bytes) throw GpuMemoryAllocationError{"AMD free VRAM fell below the required reserve"};
                    }
                }
            }, [&] { CleanupBuffers(true); }, cancelled, version)};
            estimated = allocation.estimated;
        } catch (...) {
            Cleanup();
            throw;
        }
    }
    ~OpenClBackend() override
    {
        Cleanup();
    }
    unsigned int Contexts() const override
    {
        return contexts;
    }
    mercahash::Version PoWVersion() const override { return version; }
    unsigned int EstimatedContexts() const override { return estimated; }
    unsigned int BatchContexts() const override { return version == mercahash::Version::V2 ? std::min({contexts, execution_limit, contexts - next_context}) : contexts; }
    GpuMemoryBudget Budget() const override { return allocation_budget; }
    uint64_t MemoryBytes() const override
    {
        return memory_bytes + (version == mercahash::Version::V2 ? uint64_t{contexts} * 240 : 0);
    }
    std::vector<GpuHash> Hash(std::span<const GpuHeader> input, const std::function<bool()> &cancelled) override
    {
        if (version == mercahash::Version::V2) return HashV2(input, cancelled);
        if (input.empty() || input.size() > contexts)
            throw std::runtime_error{"Invalid AMD hashing batch size"};
        if (cancelled())
            return {};
        const cl_uint count{static_cast<cl_uint>(input.size())};
        Check(
            api.clEnqueueWriteBuffer(queue, headers, CL_TRUE, 0, input.size_bytes(), input.data(), 0, nullptr, nullptr),
            "Upload OpenCL headers");
        auto argument = [&](cl_kernel kernel, unsigned index, const auto &value) {
            Check(api.clSetKernelArg(kernel, index, sizeof(value), &value), "Set OpenCL mining argument");
        };
        argument(init, 0, headers);
        argument(init, 1, states);
        argument(init, 2, count);
        const size_t local{8};
        const size_t global{count * local};
        auto launch = [&](cl_kernel kernel) {
            if (cancelled())
                return false;
            Check(api.clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, &global, &local, 0, nullptr, nullptr),
                  "Launch AMD MercaHash kernel");
            Check(api.clFinish(queue), "Synchronize AMD MercaHash kernel");
            return !cancelled();
        };
        if (!launch(init))
            return {};
        argument(stage, 0, headers);
        argument(stage, 1, states);
        argument(stage, 2, scratch);
        argument(stage, 3, outputs);
        argument(stage, 4, count);
        auto advance = [&](cl_uint phase, cl_uint begin, cl_uint end) {
            argument(stage, 5, phase);
            argument(stage, 6, begin);
            argument(stage, 7, end);
            return launch(stage);
        };
        for (cl_uint begin{0}; begin < mercahash::LineCount(version); begin += 4096)
            if (!advance(0, begin, begin + 4096))
                return {};
        for (cl_uint begin{0}; begin < mercahash::BindBlockCount(version); begin += 64)
            if (!advance(1, begin, begin + 64))
                return {};
        for (cl_uint begin{1}; begin <= mercahash::MixSteps(version); begin += 1024)
            if (!advance(2, begin, begin + 1023))
                return {};
        if (!advance(3, 0, 0))
            return {};
        std::vector<GpuHash> hashes(count);
        Check(api.clEnqueueReadBuffer(queue, outputs, CL_TRUE, 0, hashes.size() * sizeof(GpuHash), hashes.data(), 0,
                                      nullptr, nullptr),
              "Read AMD MercaHash output");
        if (cancelled())
            return {};
        return hashes;
    }

  private:
    std::vector<GpuHash> HashV2(std::span<const GpuHeader> input, const std::function<bool()>& cancelled)
    {
        if (input.empty() || input.size() > contexts) throw std::runtime_error{"Invalid AMD V2 hashing batch size"};
        std::vector<GpuHash> hashes(input.size());
        for (size_t completed{0}; completed < input.size();) {
            if (cancelled()) return {};
            const cl_uint count{static_cast<cl_uint>(std::min({size_t{BatchContexts()}, input.size() - completed, size_t{contexts - next_context}}))};
            // A view selects resident scratchpads without changing kernel math
            // or requiring a full-pool launch. Header/state/output slots are
            // local to this bounded execution window and reused after Finish.
            cl_buffer_region region{size_t{next_context} * mercahash::V2_SCRATCHPAD_BYTES,
                                    size_t{count} * mercahash::V2_SCRATCHPAD_BYTES};
            cl_int error;
            const auto view{api.clCreateSubBuffer(scratch, CL_MEM_READ_WRITE, CL_BUFFER_CREATE_TYPE_REGION, &region, &error)};
            Check(error, "Select AMD V2 resident scratchpads");
            struct DrainView {
                Api& api; cl_command_queue queue; cl_mem view;
                ~DrainView() { api.clFinish(queue); api.clReleaseMemObject(view); }
            } drain{api, queue, view};
            next_context = static_cast<unsigned int>((uint64_t{next_context} + count) % contexts);
            Check(api.clEnqueueWriteBuffer(queue, headers, CL_TRUE, 0, size_t{count} * 80, input.data() + completed, 0, nullptr, nullptr), "Upload AMD V2 headers");
            const auto argument = [&](cl_kernel kernel, unsigned index, const auto& value) {
                Check(api.clSetKernelArg(kernel, index, sizeof(value), &value), "Set AMD V2 argument");
            };
            argument(init, 0, headers); argument(init, 1, states); argument(init, 2, count);
            const size_t local{8}, global{size_t{count} * local};
            const auto launch = [&](cl_kernel kernel) {
                if (cancelled()) return false;
                Check(api.clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, &global, &local, 0, nullptr, nullptr), "Launch AMD V2 kernel");
                Check(api.clFinish(queue), "Synchronize AMD V2 kernel");
                return !cancelled();
            };
            if (!launch(init)) return {};
            argument(stage, 0, headers); argument(stage, 1, states); argument(stage, 2, view);
            argument(stage, 3, outputs); argument(stage, 4, count);
            const auto advance = [&](cl_uint phase, cl_uint begin, cl_uint end) {
                argument(stage, 5, phase); argument(stage, 6, begin); argument(stage, 7, end);
                return launch(stage);
            };
            for (cl_uint begin{0}; begin < mercahash::V2_LINE_COUNT; begin += 4096)
                if (!advance(0, begin, begin + 4096)) return {};
            for (cl_uint begin{0}; begin < mercahash::V2_BIND_BLOCK_COUNT; begin += 64)
                if (!advance(1, begin, begin + 64)) return {};
            for (cl_uint begin{1}; begin <= mercahash::V2_MIX_STEPS; begin += 256)
                if (!advance(2, begin, begin + 255)) return {};
            if (!advance(3, 0, 0)) return {};
            Check(api.clEnqueueReadBuffer(queue, outputs, CL_TRUE, 0, size_t{count} * 32, hashes.data() + completed, 0, nullptr, nullptr), "Read AMD V2 output");
            if (cancelled()) return {};
            completed += count;
        }
        return hashes;
    }
    void CleanupBuffers(bool checked = false)
    {
        cl_int failure{queue ? api.clFinish(queue) : CL_SUCCESS};
        for (auto buffer : {&headers, &states, &scratch, &outputs}) {
            if (!*buffer) continue;
            const auto error{api.clReleaseMemObject(*buffer)};
            if (failure == CL_SUCCESS) failure = error;
            if (error == CL_SUCCESS || !checked) *buffer = nullptr;
        }
        if (checked) Check(failure, "Clean up OpenCL allocation attempt");
    }
    void Cleanup()
    {
        CleanupBuffers();
        if (init)
            api.clReleaseKernel(init);
        if (stage)
            api.clReleaseKernel(stage);
        init = stage = nullptr;
        if (program)
            api.clReleaseProgram(program);
        program = nullptr;
        if (queue)
            api.clReleaseCommandQueue(queue);
        queue = nullptr;
        if (context)
            api.clReleaseContext(context);
        context = nullptr;
    }
};
} // namespace
GpuDiscovery DiscoverOpenClDevices()
{
    GpuDiscovery result;
    try {
        Api api;
        const auto devices{Devices(api)};
        for (unsigned int index{0}; index < devices.size(); ++index) {
            GpuDevice entry{GpuVendor::AMD_OPENCL};
            entry.ordinal = index;
            entry.id = "opencl:" + util::ToString(index);
            try {
                const auto device{devices[index]};
                entry.name = Text(api, device, CL_DEVICE_NAME);
                entry.capability = Text(api, device, CL_DEVICE_VERSION);
                entry.total_memory = Info<cl_ulong>(api, device, CL_DEVICE_GLOBAL_MEM_SIZE);
                // 0 denotes unknown free memory, never a claimed measurement.
                entry.max_allocation = Info<cl_ulong>(api, device, CL_DEVICE_MAX_MEM_ALLOC_SIZE);
                if (const auto free{FreeMemory(api, device)}) {
                    entry.available_memory_known = true;
                    entry.available_memory = *free;
                }
                entry.supported = SupportsOpenCl12(entry.capability) &&
                                  Info<cl_uint>(api, device, CL_DEVICE_ADDRESS_BITS) == 64 &&
                                  Info<cl_bool>(api, device, CL_DEVICE_AVAILABLE) &&
                                  Info<cl_bool>(api, device, CL_DEVICE_COMPILER_AVAILABLE) &&
                                  Text(api, device, CL_DEVICE_PROFILE) == "FULL_PROFILE" &&
                                  CalculateGpuBudget(entry.available_memory_known ? entry.available_memory : entry.total_memory,
                                      entry.max_allocation, 0, entry.available_memory_known ? GpuMemorySource::OPENCL_FREE : GpuMemorySource::OPENCL_TOTAL, mercahash::Version::V2).contexts > 0;
                if (!entry.supported)
                    entry.error =
                        "AMD device needs OpenCL 1.2 or later, 64-bit full-profile support, an available compiler and sufficient VRAM";
            } catch (const std::exception &error) {
                entry.error = error.what();
            }
            result.devices.push_back(std::move(entry));
        }
        if (devices.empty())
            result.errors.emplace_back("No AMD OpenCL GPU detected");
    } catch (const std::exception &error) {
        result.errors.emplace_back(error.what());
    }
    return result;
}
std::unique_ptr<GpuBackend> CreateOpenClBackend(const GpuDevice &device, unsigned int contexts, const std::function<bool()>& cancelled)
{
    return std::make_unique<OpenClBackend>(device, contexts, cancelled);
}
} // namespace mining
