#!/usr/bin/env python3
"""Reproduce the isolated actual-library WO07C runner, without node activation."""
import argparse
import json
import os
import pathlib
import resource
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[4]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode',choices=['debug','sanitize'],required=True)
    args = parser.parse_args()
    evidence = ROOT/'work/wo07c'
    build = evidence/f'native-{args.mode}-build'
    output = evidence/f'native-{args.mode}'
    env = os.environ.copy()
    env['PKG_CONFIG_PATH'] = str(ROOT/'work/wo06-tools/qt-sdk/usr/lib/x86_64-linux-gnu/pkgconfig')
    env['ASAN_OPTIONS'] = 'detect_leaks=0'
    env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    cmake = str(ROOT/'work/tools/cmake/data/bin/cmake')
    commands = [
        [cmake,'-S',str(ROOT),'-B',str(build),'-DBUILD_GUI=OFF','-DENABLE_IPC=OFF',
         '-DBUILD_TESTS=ON','-DBUILD_BENCH=OFF','-DBUILD_KERNEL_LIB=OFF','-DWITH_USDT=OFF',
         '-DWITH_ZMQ=OFF','-DWITH_CCACHE=OFF','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
         '-DCMAKE_PREFIX_PATH='+str(ROOT/'work/wo06-tools/qt-sdk/usr'),
         '-DCMAKE_BUILD_TYPE='+('Debug' if args.mode == 'debug' else 'RelWithDebInfo'),
         *(['-DCMAKE_CXX_FLAGS_DEBUG=-O0 -ftrapv -g0 -D_GLIBCXX_ASSERTIONS=1',
            '-DCMAKE_C_FLAGS_DEBUG=-O0 -g0'] if args.mode == 'debug' else [
             '-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O1 -g0 -fsanitize=address,undefined -fno-omit-frame-pointer',
             '-DCMAKE_C_FLAGS_RELWITHDEBINFO=-O1 -g0 -fsanitize=address,undefined -fno-omit-frame-pointer',
             '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'])],
        [cmake,'--build',str(build),'--target','bitcoind','bitcoin-cli','test_util','-j1'],
        ['python3',str(pathlib.Path(__file__).with_name('build_native.py')),'--build',str(build),'--output',str(output)],
        [str(output/'wo07c_native_tests'),'--log_level=test_suite','--report_level=detailed','--color_output=no','--',
         '-testdatadir='+str(evidence/f'native-runtime-{args.mode}')],
    ]
    rows = []
    for index,command in enumerate(commands):
        started = time.monotonic()
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        log = evidence/f'native-{args.mode}-step{index}.log'
        with log.open('w') as stream:
            result = subprocess.run(command,cwd=ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT)
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
        rows.append(dict(command=command,exit_code=result.returncode,seconds=time.monotonic()-started,
                         cpu_seconds=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime,
                         max_rss_kib=after.ru_maxrss,log=str(log.relative_to(ROOT))))
        (evidence/f'native-{args.mode}-execution.json').write_text(json.dumps(rows,indent=2)+'\n')
        print(args.mode,index,result.returncode,flush=True)
        if result.returncode:
            raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
