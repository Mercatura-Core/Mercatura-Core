// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
// Minimal entry point for the same permanent FUZZ_TARGET registrations, without
// linking unrelated node fuzzers. The engine, coverage and sanitizers are real.
#include <test/fuzz/fuzz.h>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <string>

namespace {
struct Target { TypeTestOneInput run; FuzzTargetOptions options; };
auto& Targets() { static std::map<std::string,Target> targets; return targets; }
TypeTestOneInput selected;
}
void FuzzFrameworkRegisterTarget(std::string_view name, TypeTestOneInput target, FuzzTargetOptions options)
{
    Targets().emplace(name,Target{std::move(target),std::move(options)});
}
extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    const char* name=std::getenv("FUZZ");
    auto found=Targets().find(name ? name : "");
    if (found==Targets().end()) {
        std::fprintf(stderr,"Set FUZZ to one of:");
        for (const auto& [key,value]:Targets()) std::fprintf(stderr," %s",key.c_str());
        std::fprintf(stderr,"\n"); std::abort();
    }
    found->second.options.init(); selected=found->second.run; return 0;
}
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    // Includes full identity files and a maximum handshake record, with bounded
    // room for a structured input stream. No arbitrary multi-MiB fuzz allocations.
    if (size<=16384) selected({data,size});
    return 0;
}
