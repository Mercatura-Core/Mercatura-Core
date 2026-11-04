#!/usr/bin/env python3
"""Dedicated WO08 instrumented native build; no runtime activation controls."""
import json
import os
import pathlib
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[4]
WORK = ROOT/'work/wo08'


def main():
    build = WORK/'native-tsan-build'
    sdk = ROOT/'work/wo06-tools/qt-sdk/usr'
    cmake = str(ROOT/'work/tools/cmake/data/bin/cmake')
    flags = '-O1 -g0 -fsanitize=thread -fno-omit-frame-pointer'
    commands = [
        [cmake,'-S',str(ROOT),'-B',str(build),'-DCMAKE_BUILD_TYPE=RelWithDebInfo',
         '-DBUILD_GUI=OFF','-DENABLE_IPC=OFF','-DBUILD_TESTS=ON','-DBUILD_BENCH=OFF',
         '-DBUILD_KERNEL_LIB=OFF','-DWITH_USDT=OFF','-DWITH_ZMQ=OFF','-DWITH_CCACHE=OFF',
         '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DCMAKE_PREFIX_PATH='+str(sdk),
         '-DCMAKE_CXX_FLAGS_RELWITHDEBINFO='+flags,'-DCMAKE_C_FLAGS_RELWITHDEBINFO='+flags,
         '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread',
         '-DCMAKE_CXX_ARCHIVE_CREATE=<CMAKE_AR> qcT <TARGET> <LINK_FLAGS> <OBJECTS>',
         '-DCMAKE_C_ARCHIVE_CREATE=<CMAKE_AR> qcT <TARGET> <LINK_FLAGS> <OBJECTS>'],
        [cmake,'--build',str(build),'--target','bitcoin_node','bitcoin_wallet','bitcoin_common','test_util','-j1'],
    ]
    env = os.environ.copy(); env['PKG_CONFIG_PATH'] = str(sdk/'lib/x86_64-linux-gnu/pkgconfig')
    results = []
    for index,command in enumerate(commands):
        log = WORK/f'tsan-build-step{index}.log'
        started = time.monotonic()
        with log.open('w') as stream:
            result = subprocess.run(command,env=env,cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT)
        results.append(dict(command=command,exit_code=result.returncode,seconds=time.monotonic()-started,
                            log=str(log.relative_to(ROOT))))
        (WORK/'tsan-build.json').write_text(json.dumps(results,indent=2)+'\n')
        print(index,result.returncode,flush=True)
        if result.returncode:
            raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
