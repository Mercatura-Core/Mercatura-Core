// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
// Fixed-seed smoke runner for the actual permanent fuzz targets. This is not a
// coverage-guided fuzz campaign, and never opens sockets or uses production keys.
#include <test/fuzz/fuzz.h>
#include <map>
#include <random>
#include <iostream>
#include <vector>

static auto& Targets() { static std::map<std::string_view,TypeTestOneInput> targets; return targets; }
void FuzzFrameworkRegisterTarget(std::string_view name, TypeTestOneInput target, FuzzTargetOptions opts) { Targets().emplace(name,std::move(target)); }
int main()
{
    std::mt19937_64 random(0x50415051);
    for (auto& [name,target] : Targets()) {
        for (size_t i=0;i<1000;++i) {
            std::vector<uint8_t> input(random()%4500);
            for (auto& b : input) b=random();
            target(input);
        }
        std::cout << name << ": PASS 1000 fixed-seed inputs (smoke only)\n";
    }
}
