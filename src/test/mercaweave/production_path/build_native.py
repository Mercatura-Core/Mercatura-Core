#!/usr/bin/env python3
"""Build the focused WO07C Boost runner with the actual native build's flags/libs.

No legacy research ABI definitions. Dependencies must first be rebuilt normally
in the selected native configuration. This never builds or launches a miner.
"""
import argparse
import json
import pathlib
import re
import shlex
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[4]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=pathlib.Path,required=True)
    parser.add_argument('--output',type=pathlib.Path,required=True)
    args = parser.parse_args()
    build = args.build.resolve(strict=True)
    out = args.output.resolve(); out.mkdir(parents=True,exist_ok=True)
    # Recheck dependencies after any ongoing configuration build: source edits
    # may have occurred after an early library target was visited by make.
    refresh = [str(ROOT/'work/tools/cmake/data/bin/cmake'),'--build',str(build),'--target',
               'bitcoin_node','bitcoin_wallet','bitcoin_common','test_util','-j1']
    subprocess.run(refresh,check=True,cwd=ROOT)
    (out/'refresh-command.json').write_text(json.dumps(refresh)+'\n')
    cwd = build/'src/test'
    flags = (cwd/'CMakeFiles/test_bitcoin.dir/flags.make').read_text()
    options = []
    for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:
        options.extend(shlex.split(re.search('^'+key+r' = (.*)$',flags,re.M)[1]))
    if any('MERCATURA_MERCAWEAVE' in option for option in options):
        raise RuntimeError('native libraries must have ordinary matching ABIs')
    objects,commands = [],[]
    sources = ['src/test/main.cpp','src/test/mercaweave/fixtures.cpp',
               'src/test/mercaweave/native_v2.cpp','src/test/mercaweave/production_path/native_tests.cpp',
               'src/test/mercaweave/production_path/wallet_tests.cpp',
               'src/test/mercaweave/production_path/security_tests.cpp',
               'src/wallet/test/mining_tests.cpp']
    for index,source in enumerate(sources):
        obj = out/f'{index}-{pathlib.Path(source).stem}.o'
        dependencies = obj.with_suffix('.d')
        recorded = obj.with_suffix('.command.json')
        command = ['/usr/bin/c++',*options,'-DMERCATURA_MERCAWEAVE_PRODUCTION_PATH_TEST_ONLY=1',
                   '-MMD','-MF',str(dependencies),'-c',str(ROOT/source),'-o',str(obj)]
        fresh = False
        if obj.exists() and dependencies.exists() and recorded.exists() and json.loads(recorded.read_text()) == command:
            names = shlex.split(dependencies.read_text().replace('\\\n',' ').split(':',1)[1])
            fresh = all((cwd/name).exists() and (cwd/name).stat().st_mtime_ns <= obj.stat().st_mtime_ns for name in names)
        commands.append(command)
        if not fresh:
            subprocess.run(command,check=True,cwd=cwd)
            recorded.write_text(json.dumps(command)+'\n')
        objects.append(str(obj))
    original = shlex.split((cwd/'CMakeFiles/test_bitcoin.dir/link.txt').read_text())
    link = []; index = 0
    while index < len(original):
        arg = original[index]; index += 1
        if arg == '-o': index += 1; continue
        if arg.endswith('.o') or arg.startswith('-Wl,--dependency-file='): continue
        if arg.endswith(('.a','.so')):
            arg = str((cwd/arg).resolve(strict=True))
        link.append(arg)
    link[1:1] = objects
    link.extend(['-o',str(out/'wo07c_native_tests')])
    commands.append(link); subprocess.run(link,check=True,cwd=cwd)
    (out/'build-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    print(out/'wo07c_native_tests')


if __name__ == '__main__':
    main()
