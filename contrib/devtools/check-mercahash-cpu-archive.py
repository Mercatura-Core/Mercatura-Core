#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Rebuild the frozen 1024-header/full-pad CPU verifier against native sources.

Reads the frozen CPU campaign without modifying it. Outputs only to --output.
The archived verifier exercises 64 alignments, guards, reuse, overlap, partial
batches and every full final scratchpad, in addition to immutable hash outputs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--campaign', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', default='/usr/bin/g++')
    parser.add_argument('--scalar', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    identities = {
        'scripts/bench-live-reference.cpp': 'de7106327b99ae737b13249e8f6f73fdebb11f1e667230f610f97f9dd1ea1016',
        'baseline/headers1024.bin': '195f4f788fc7642b2097c6932c35f0c7e09a79de4f733f168330b5b586092bd3',
    }
    for name, expected in identities.items():
        if hashlib.sha256((args.campaign / name).read_bytes()).hexdigest() != expected:
            raise RuntimeError(f'Frozen archive identity mismatch: {name}')
    out = args.output.resolve()
    campaign = args.campaign.resolve()
    if out == campaign or campaign in out.parents:
        parser.error('Output must be outside the frozen campaign')
    out.mkdir(parents=True, exist_ok=True)
    text = (campaign / 'scripts/bench-live-reference.cpp').read_text()
    text = text.replace('#include "batch_api.h"', '#include <mining/mercahash_cpu_batch.h>')
    text = text.replace('mercahash::FrozenHashV2', 'mercahash::HashV2')
    # Production rejects an empty batch; the frozen research API returned zero.
    text = text.replace('need(Batch({})==0,"empty batch count");',
        'bool rejected_empty=false;try{Batch({});}catch(const std::invalid_argument&){rejected_empty=true;}need(rejected_empty,"empty batch accepted");')
    begin = text.index('size_t hash(')
    end = text.index('\n#include <sys/mman.h>', begin)
    text = text[:begin] + text[end:]
    text = text[:text.index('void bench(')] + '''
int main(int argc,char**argv){try{
 need(argc==3,"fixture count required");verify(argv[1],std::stoul(argv[2]));
}catch(const std::exception& e){std::cerr<<e.what()<<'\\n';return 1;}}
'''
    harness = out / 'verify.cpp'
    harness.write_text(text)
    binary = out / 'verify'
    flags = ['-std=c++20', '-O2', '-DNDEBUG', '-pthread', f'-I{root / "src"}']
    if args.scalar:
        flags += ['-DCPU2_FORCE_SCALAR', '-DMERCAHASH_FORCE_SCALAR']
    if args.sanitize:
        flags += ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    command = [args.compiler, *flags, str(harness),
        *(str(root / 'src' / name) for name in (
            'mining/mercahash_cpu_batch.cpp', 'crypto/mercahash_v2.cpp', 'crypto/sha3.cpp')),
        '-o', str(binary)]
    (out / 'protocol.json').write_text(json.dumps({
        'command': command, 'archive_sha256': identities,
        'harness_sha256': hashlib.sha256(harness.read_bytes()).hexdigest(),
        'compiler': subprocess.check_output([args.compiler, '--version'], text=True),
    }, indent=2) + '\n')
    with (out / 'build.log').open('w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (out / 'result.json').open('w') as log, (out / 'stderr.log').open('w') as errors:
        subprocess.run([str(binary), str(campaign / 'baseline/headers1024.bin'), '1024'],
            stdout=log, stderr=errors, check=True)
    print((out / 'result.json').read_text(), end='')


if __name__ == '__main__':
    main()
