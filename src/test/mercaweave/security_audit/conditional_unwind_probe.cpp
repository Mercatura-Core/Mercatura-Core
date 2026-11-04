// Minimal separate-toolchain control for the WO08 witness construction failure.
// The unsafe form is deliberately retained ONLY in this standalone probe.
#include <test/mercaweave/security_audit/allocation.h>
#include <iostream>
#include <vector>
using Bytes=std::vector<unsigned char>;
int main()
{
    const Bytes key(1952),nonce(32),reference(4);
    const Bytes* signature=nullptr;
    size_t faults{0},successes{0};
    for (size_t point=0;point<16;++point) {
        std::vector<Bytes> witness;
        std::cout << "point " << point << std::endl;
        try {
            mercaweave::audit::FailAfter(point);
#ifdef WO08_SAFE_WITNESS_CONTROL
            witness.reserve(3);
            if (signature) witness.push_back(*signature);
            else witness.emplace_back(3309);
            witness.push_back(key); witness.push_back(nonce);
#else
            const bool anchor=true;
            witness=anchor ? std::vector<Bytes>{signature ? *signature : Bytes(3309),key,nonce} :
                             std::vector<Bytes>{nonce,reference};
#endif
            mercaweave::audit::Disarm(); ++successes;
        } catch (const std::bad_alloc&) { mercaweave::audit::Disarm(); ++faults; }
    }
    std::cout << "faults " << faults << " successes " << successes << '\n';
    return successes==0 || faults==0;
}
