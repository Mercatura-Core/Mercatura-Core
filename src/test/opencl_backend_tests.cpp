// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
// Actual OpenCL host backend with a deterministic API double. No GPU executes.
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#include <dlfcn.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <mining/gpu_backend.h>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace fake_cl {
struct Mem { size_t bytes, origin{0}; Mem* parent{nullptr}; std::vector<unsigned char> data; };
struct Kernel { std::string name; std::map<unsigned, std::vector<unsigned char>> args; };
struct Launch { std::string name; size_t local, groups, origin; unsigned begin, end; };
struct Runtime {
    unsigned buffers{0}, kernels{0}, contexts{0}, queues{0}, programs{0}, pending{0}, peak_pending{0};
    unsigned allocations{0}, fail_allocation{0}, fail_launch{0}, finishes{0}, fail_finish{0};
    size_t kernel_limit{64}, axis_limit{64};
    bool freed_pending{false};
    std::vector<Launch> launches;
} runtime;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error{message}; }
template <typename T> cl_int Value(const T& value, size_t bytes, void* out, size_t* returned)
{
    if (returned) *returned = sizeof(T);
    if (out) { Require(bytes >= sizeof(T), "Capability buffer too small"); std::memcpy(out, &value, sizeof(T)); }
    return CL_SUCCESS;
}
cl_int Text(const char* text, size_t bytes, void* out, size_t* returned)
{
    const auto size{std::strlen(text) + 1};
    if (returned) *returned = size;
    if (out) { Require(bytes >= size, "Text buffer too small"); std::memcpy(out, text, size); }
    return CL_SUCCESS;
}
template <typename T> T Arg(Kernel* kernel, unsigned index)
{
    T value{};
    Require(kernel->args.at(index).size() == sizeof(T), "Wrong kernel argument size");
    std::memcpy(&value, kernel->args.at(index).data(), sizeof(T)); return value;
}
cl_int clGetPlatformIDs(cl_uint, cl_platform_id* ids, cl_uint* size)
{ if (ids) *ids = reinterpret_cast<cl_platform_id>(1); if (size) *size = 1; return CL_SUCCESS; }
cl_int clGetDeviceIDs(cl_platform_id, cl_device_type type, cl_uint, cl_device_id* ids, cl_uint* size)
{ Require(type == CL_DEVICE_TYPE_GPU, "Enumerated a CPU"); if (ids) *ids = reinterpret_cast<cl_device_id>(1); if (size) *size = 1; return CL_SUCCESS; }
cl_int clGetDeviceInfo(cl_device_id, cl_device_info field, size_t size, void* out, size_t* returned)
{
    switch (field) {
    case CL_DEVICE_VENDOR_ID: return Value(cl_uint{0x1002}, size, out, returned);
    case CL_DEVICE_NAME: return Text("Fake AMD host-contract device", size, out, returned);
    case CL_DEVICE_VERSION: return Text("OpenCL 1.2 fake", size, out, returned);
    case CL_DEVICE_EXTENSIONS: return Text("", size, out, returned);
    case CL_DEVICE_PROFILE: return Text("FULL_PROFILE", size, out, returned);
    case CL_DEVICE_GLOBAL_MEM_SIZE: return Value(cl_ulong{8ULL << 30}, size, out, returned);
    case CL_DEVICE_MAX_MEM_ALLOC_SIZE: return Value(cl_ulong{2ULL << 30}, size, out, returned);
    case CL_DEVICE_LOCAL_MEM_SIZE: return Value(cl_ulong{32768}, size, out, returned);
    case CL_DEVICE_MEM_BASE_ADDR_ALIGN: return Value(cl_uint{1024}, size, out, returned);
    case CL_DEVICE_ADDRESS_BITS: return Value(cl_uint{64}, size, out, returned);
    case CL_DEVICE_MAX_COMPUTE_UNITS: return Value(cl_uint{2}, size, out, returned);
    case CL_DEVICE_MAX_WORK_GROUP_SIZE: return Value(size_t{256}, size, out, returned);
    case CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS: return Value(cl_uint{3}, size, out, returned);
    case CL_DEVICE_MAX_WORK_ITEM_SIZES: return Value(std::array<size_t, 3>{runtime.axis_limit, 256, 256}, size, out, returned);
    case CL_DEVICE_AVAILABLE: case CL_DEVICE_COMPILER_AVAILABLE: return Value(cl_bool{CL_TRUE}, size, out, returned);
    default: throw std::runtime_error{"Unexpected capability query"};
    }
}
cl_context clCreateContext(const cl_context_properties*, cl_uint, const cl_device_id*, void(CL_CALLBACK*)(const char*, const void*, size_t, void*), void*, cl_int* error)
{ ++runtime.contexts; *error = CL_SUCCESS; return reinterpret_cast<cl_context>(1); }
cl_int clReleaseContext(cl_context) { --runtime.contexts; return CL_SUCCESS; }
cl_command_queue clCreateCommandQueue(cl_context, cl_device_id, cl_command_queue_properties properties, cl_int* error)
{ Require(properties == 0, "Queue must be in order"); ++runtime.queues; *error = CL_SUCCESS; return reinterpret_cast<cl_command_queue>(1); }
cl_int clReleaseCommandQueue(cl_command_queue) { --runtime.queues; return CL_SUCCESS; }
cl_mem clCreateBuffer(cl_context, cl_mem_flags, size_t bytes, void*, cl_int* error)
{
    if (++runtime.allocations == runtime.fail_allocation) { *error = CL_MEM_OBJECT_ALLOCATION_FAILURE; return nullptr; }
    auto* memory = new Mem{bytes, 0, nullptr, {}};
    if (bytes < (1U << 20)) memory->data.resize(bytes);
    ++runtime.buffers; *error = CL_SUCCESS; return reinterpret_cast<cl_mem>(memory);
}
cl_mem clCreateSubBuffer(cl_mem buffer, cl_mem_flags, cl_buffer_create_type, const void* info, cl_int* error)
{
    const auto& region{*static_cast<const cl_buffer_region*>(info)};
    auto* parent = reinterpret_cast<Mem*>(buffer);
    Require(region.origin + region.size <= parent->bytes, "Scratchpad view out of bounds");
    auto* view = new Mem{region.size, region.origin, parent, {}};
    ++runtime.buffers; *error = CL_SUCCESS; return reinterpret_cast<cl_mem>(view);
}
cl_int clReleaseMemObject(cl_mem buffer)
{ runtime.freed_pending |= runtime.pending != 0; delete reinterpret_cast<Mem*>(buffer); --runtime.buffers; return CL_SUCCESS; }
cl_program clCreateProgramWithSource(cl_context, cl_uint, const char**, const size_t*, cl_int* error)
{ ++runtime.programs; *error = CL_SUCCESS; return reinterpret_cast<cl_program>(1); }
cl_int clBuildProgram(cl_program, cl_uint, const cl_device_id*, const char*, void(CL_CALLBACK*)(cl_program, void*), void*) { return CL_SUCCESS; }
cl_int clGetProgramBuildInfo(cl_program, cl_device_id, cl_program_build_info, size_t, void*, size_t*) { return CL_SUCCESS; }
cl_int clReleaseProgram(cl_program) { --runtime.programs; return CL_SUCCESS; }
cl_kernel clCreateKernel(cl_program, const char* name, cl_int* error)
{ ++runtime.kernels; *error = CL_SUCCESS; return reinterpret_cast<cl_kernel>(new Kernel{name, {}}); }
cl_int clGetKernelWorkGroupInfo(cl_kernel, cl_device_id, cl_kernel_work_group_info field, size_t size, void* out, size_t* returned)
{ return field == CL_KERNEL_WORK_GROUP_SIZE ? Value(runtime.kernel_limit, size, out, returned) : Value(cl_ulong{4096}, size, out, returned); }
cl_int clReleaseKernel(cl_kernel kernel) { delete reinterpret_cast<Kernel*>(kernel); --runtime.kernels; return CL_SUCCESS; }
cl_int clSetKernelArg(cl_kernel kernel, cl_uint index, size_t size, const void* value)
{ const auto* bytes = static_cast<const unsigned char*>(value); reinterpret_cast<Kernel*>(kernel)->args[index] = {bytes, bytes + size}; return CL_SUCCESS; }
cl_int clFinish(cl_command_queue)
{ runtime.pending = 0; return ++runtime.finishes == runtime.fail_finish ? CL_OUT_OF_RESOURCES : CL_SUCCESS; }
cl_int clEnqueueWriteBuffer(cl_command_queue queue, cl_mem buffer, cl_bool blocking, size_t offset, size_t size, const void* input, cl_uint, const cl_event*, cl_event*)
{
    Require(blocking, "Header staging lifetime requires blocking write"); fake_cl::clFinish(queue);
    auto* memory = reinterpret_cast<Mem*>(buffer); Require(offset + size <= memory->data.size(), "Header upload out of bounds");
    std::memcpy(memory->data.data() + offset, input, size); return CL_SUCCESS;
}
cl_int clEnqueueNDRangeKernel(cl_command_queue, cl_kernel handle, cl_uint, const size_t*, const size_t* global, const size_t* local, cl_uint, const cl_event*, cl_event*)
{
    auto* kernel = reinterpret_cast<Kernel*>(handle);
    const bool init{kernel->name == "mh_v2_init"};
    const unsigned count{Arg<unsigned>(kernel, init ? 2 : 4)};
    auto* view = init ? nullptr : reinterpret_cast<Mem*>(Arg<cl_mem>(kernel, 2));
    runtime.launches.push_back({kernel->name, *local, *global / *local, view ? view->origin : 0, init ? 0 : Arg<unsigned>(kernel, 5), init ? 0 : Arg<unsigned>(kernel, 6)});
    Require(*global / *local == count, "Group count mismatch");
    if (runtime.launches.size() == runtime.fail_launch) return CL_OUT_OF_RESOURCES;
    ++runtime.pending; runtime.peak_pending = std::max(runtime.peak_pending, runtime.pending);
    if (kernel->name == "mh_v2_final") {
        auto* headers = reinterpret_cast<Mem*>(Arg<cl_mem>(kernel, 0));
        auto* outputs = reinterpret_cast<Mem*>(Arg<cl_mem>(kernel, 3));
        for (unsigned i{0}; i < count; ++i) std::memcpy(outputs->data.data() + i * 32, headers->data.data() + i * 80, 32);
    }
    return CL_SUCCESS;
}
cl_int clEnqueueReadBuffer(cl_command_queue queue, cl_mem buffer, cl_bool, size_t offset, size_t size, void* out, cl_uint, const cl_event*, cl_event*)
{ fake_cl::clFinish(queue); auto* memory = reinterpret_cast<Mem*>(buffer); Require(offset + size <= memory->data.size(), "Output read out of bounds"); std::memcpy(out, memory->data.data() + offset, size); return CL_SUCCESS; }
void* Open(const char*, int) { return reinterpret_cast<void*>(1); }
int Close(void*) { return 0; }
void* Symbol(void*, const char* name)
{
#define ENTRY(function) if (std::strcmp(name, #function) == 0) return reinterpret_cast<void*>(&function)
    ENTRY(clGetPlatformIDs); ENTRY(clGetDeviceIDs); ENTRY(clGetDeviceInfo); ENTRY(clCreateContext); ENTRY(clReleaseContext);
    ENTRY(clCreateCommandQueue); ENTRY(clReleaseCommandQueue); ENTRY(clCreateBuffer); ENTRY(clCreateSubBuffer); ENTRY(clReleaseMemObject);
    ENTRY(clCreateProgramWithSource); ENTRY(clBuildProgram); ENTRY(clGetProgramBuildInfo); ENTRY(clReleaseProgram);
    ENTRY(clCreateKernel); ENTRY(clGetKernelWorkGroupInfo); ENTRY(clReleaseKernel); ENTRY(clSetKernelArg);
    ENTRY(clEnqueueWriteBuffer); ENTRY(clEnqueueReadBuffer); ENTRY(clEnqueueNDRangeKernel); ENTRY(clFinish);
#undef ENTRY
    return nullptr;
}
}
// Override the loader only in this separate Linux test executable. The backend
// itself is compiled normally as its own translation unit, as in production.
extern "C" void* dlopen(const char* name, int flags) noexcept { return fake_cl::Open(name, flags); }
extern "C" void* dlsym(void* library, const char* name) noexcept { return fake_cl::Symbol(library, name); }
extern "C" int dlclose(void* library) noexcept { return fake_cl::Close(library); }

int main()
{
    using namespace fake_cl;
    try {
        auto device{mining::DiscoverOpenClDevices().devices.at(0)};
        device.pow_version = mercahash::Version::V2;
        const auto clean = [&] {
            Require(!runtime.buffers && !runtime.kernels && !runtime.contexts && !runtime.queues && !runtime.programs, "Resource leak");
            Require(!runtime.freed_pending && !runtime.pending, "Released live device resources");
        };
        for (size_t limit : {8U, 16U, 32U, 64U}) {
            runtime = {}; runtime.kernel_limit = limit;
            {
                auto backend{mining::CreateOpenClBackend(device, 7, [] { return false; })};
                std::vector<mining::GpuHeader> input(7);
                for (unsigned i{0}; i < input.size(); ++i) input[i].fill(i + 1);
                for (unsigned pass{0}; pass < 2; ++pass) {
                    const auto result{backend->Hash(input, [] { return false; })};
                    Require(result.size() == input.size(), "Wrong result count");
                    for (unsigned i{0}; i < input.size(); ++i) Require(result[i][0] == i + 1, "Window output order changed");
                }
                Require(runtime.peak_pending <= 4, "Unbounded queue");
                for (const auto& launch : runtime.launches) Require(launch.local == (launch.name == "mh_v2_bind" ? limit : 1), "Wrong scalar/binding geometry");
                Require(runtime.launches.size() == 8 * 194, "Missing full/partial execution windows");
                for (unsigned window{0}; window < 8; ++window) {
                    const auto offset{window * 194};
                    const auto origin{size_t{(window % 4) * 2} * mercahash::V2_SCRATCHPAD_BYTES};
                    Require(runtime.launches[offset].name == "mh_v2_init", "Window must initialize fresh state");
                    for (unsigned chunk{0}; chunk < 64; ++chunk) {
                        const auto& fill{runtime.launches[offset + 1 + chunk]};
                        const auto& bind{runtime.launches[offset + 65 + chunk]};
                        const auto& mix{runtime.launches[offset + 129 + chunk]};
                        Require(fill.name == "mh_v2_fill" && fill.begin == chunk * 4096 && fill.end == (chunk + 1) * 4096, "Fill chunk bounds changed");
                        Require(bind.name == "mh_v2_bind" && bind.begin == chunk * 64 && bind.end == (chunk + 1) * 64, "Binding chunk bounds changed");
                        Require(mix.name == "mh_v2_mix" && mix.begin == chunk * 256 + 1 && mix.end == (chunk + 1) * 256, "Mix chunk bounds changed");
                        Require(fill.origin == origin && bind.origin == origin && mix.origin == origin, "Resident windows overlap or skip contexts");
                    }
                    Require(runtime.launches[offset + 193].name == "mh_v2_final", "Missing finalization");
                }
                for (unsigned checkpoint : {1U, 3U, 65U, 128U, 193U}) {
                    runtime.launches.clear();
                    Require(backend->Hash(input, [&] { return runtime.launches.size() >= checkpoint; }).empty(), "Cancellation returned partial hashes");
                    Require(runtime.pending == 0, "Cancellation did not drain");
                    Require(backend->Hash(std::span{input}.first(1), [] { return false; }).size() == 1, "Restart after cancellation failed");
                }
                runtime.launches.clear(); runtime.fail_launch = 3;
                bool failed{false};
                try { backend->Hash(input, [] { return false; }); } catch (const std::runtime_error&) { failed = true; }
                Require(failed && runtime.pending == 0, "Launch error escaped without draining");
                runtime.fail_launch = 0;
                runtime.fail_finish = runtime.finishes + 2;
                failed = false;
                try { backend->Hash(input, [] { return false; }); } catch (const std::runtime_error&) { failed = true; }
                Require(failed && runtime.pending == 0, "Synchronization error escaped without draining");
                runtime.fail_finish = 0;
                Require(backend->Hash(std::span{input}.first(1), [] { return false; }).size() == 1, "Restart after synchronization error failed");
            }
            clean();
        }
        for (unsigned failure{1}; failure <= 4; ++failure) {
            runtime = {}; runtime.fail_allocation = failure;
            bool failed{false};
            try { mining::CreateOpenClBackend(device, 7, [] { return false; }); } catch (const std::runtime_error&) { failed = true; }
            Require(failed, "Manual allocation failure must be terminal"); clean();
        }
        runtime = {}; runtime.kernel_limit = 7;
        bool rejected{false};
        try { mining::CreateOpenClBackend(device, 7, [] { return false; }); } catch (const std::runtime_error&) { rejected = true; }
        Require(rejected, "Illegal geometry accepted"); clean();
        runtime = {}; runtime.axis_limit = 16;
        {
            auto backend{mining::CreateOpenClBackend(device, 1, [] { return false; })};
            Require(backend->Hash(std::vector<mining::GpuHeader>(1), [] { return false; }).size() == 1, "Axis-limited device failed");
            for (const auto& launch : runtime.launches) Require(launch.local <= 16, "Per-axis workgroup limit ignored");
        }
        clean();
        std::cout << "OpenCL host geometry, bounded queues, window order, cancellation, errors and cleanup PASS (fake ICD; no GPU execution)\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
