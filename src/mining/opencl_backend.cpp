// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#include <algorithm>
#include <charconv>
#include <cstring>
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
        X(clCreateBuffer) X(clReleaseMemObject) X(clCreateProgramWithSource) X(clBuildProgram)                         \
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
class OpenClBackend final : public GpuBackend
{
    Api api;
    cl_context context{nullptr};
    cl_command_queue queue{nullptr};
    cl_program program{nullptr};
    cl_kernel init{nullptr}, stage{nullptr};
    cl_mem headers{nullptr}, states{nullptr}, scratch{nullptr}, outputs{nullptr};
    unsigned int contexts{0};
    uint64_t memory_bytes{0};

  public:
    OpenClBackend(const GpuDevice &selected, unsigned int requested)
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
            // Core OpenCL exposes total global memory, not universally free VRAM.
            // Budget conservatively and fail cleanly on allocation pressure.
            const auto budget{CalculateGpuBudget(Info<cl_ulong>(api, device, CL_DEVICE_GLOBAL_MEM_SIZE),
                                                 Info<cl_ulong>(api, device, CL_DEVICE_MAX_MEM_ALLOC_SIZE), requested,
                                                 GpuMemorySource::OPENCL_TOTAL)};
            contexts = budget.contexts;
            memory_bytes = budget.scratchpad_bytes;
            if (!contexts)
                throw std::runtime_error{"AMD GPU has insufficient memory for a safe MercaHash scratchpad"};
            cl_int error;
            context = api.clCreateContext(nullptr, 1, &device, nullptr, nullptr, &error);
            Check(error, "Create OpenCL context");
            queue = api.clCreateCommandQueue(context, device, 0, &error);
            Check(error, "Create OpenCL queue");
            const char *source{OPENCL_MERCAHASH_SOURCE};
            const size_t size{std::strlen(source)};
            program = api.clCreateProgramWithSource(context, 1, &source, &size, &error);
            Check(error, "Create OpenCL program");
            error = api.clBuildProgram(program, 1, &device, "-cl-std=CL1.2", nullptr, nullptr);
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
            auto allocate = [&](cl_mem &buffer, size_t bytes) {
                buffer = api.clCreateBuffer(context, CL_MEM_READ_WRITE, bytes, nullptr, &error);
                Check(error, "Allocate OpenCL mining buffer");
            };
            allocate(headers, contexts * size_t{80});
            allocate(states, contexts * size_t{128});
            allocate(scratch, static_cast<size_t>(memory_bytes));
            allocate(outputs, contexts * size_t{32});
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
    uint64_t MemoryBytes() const override
    {
        return memory_bytes;
    }
    std::vector<GpuHash> Hash(std::span<const GpuHeader> input, const std::function<bool()> &cancelled) override
    {
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
        for (cl_uint begin{0}; begin < 2097152; begin += 4096)
            if (!advance(0, begin, begin + 4096))
                return {};
        for (cl_uint begin{0}; begin < 32768; begin += 64)
            if (!advance(1, begin, begin + 64))
                return {};
        for (cl_uint begin{1}; begin <= 131072; begin += 1024)
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
    void Cleanup()
    {
        if (queue)
            api.clFinish(queue);
        for (auto buffer : {headers, states, scratch, outputs})
            if (buffer)
                api.clReleaseMemObject(buffer);
        headers = states = scratch = outputs = nullptr;
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
                entry.supported = SupportsOpenCl12(entry.capability) &&
                                  Info<cl_uint>(api, device, CL_DEVICE_ADDRESS_BITS) == 64 &&
                                  Info<cl_bool>(api, device, CL_DEVICE_AVAILABLE) &&
                                  Info<cl_bool>(api, device, CL_DEVICE_COMPILER_AVAILABLE) &&
                                  Text(api, device, CL_DEVICE_PROFILE) == "FULL_PROFILE" &&
                                  CalculateGpuBudget(entry).contexts > 0;
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
std::unique_ptr<GpuBackend> CreateOpenClBackend(const GpuDevice &device, unsigned int contexts)
{
    return std::make_unique<OpenClBackend>(device, contexts);
}
} // namespace mining
