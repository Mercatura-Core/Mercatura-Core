#!/usr/bin/env python3
"""Build WO08 audit TUs with the actual root native flags and libraries."""
import argparse
import json
import pathlib
import re
import shlex
import subprocess

ROOT=pathlib.Path(__file__).resolve().parents[4]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=pathlib.Path,required=True)
    parser.add_argument('--output',type=pathlib.Path,required=True)
    args=parser.parse_args()
    build=args.build.resolve(strict=True); out=args.output.resolve(); out.mkdir(parents=True,exist_ok=True)
    refresh=[str(ROOT/'work/tools/cmake/data/bin/cmake'),'--build',str(build),'--target',
             'bitcoin_node','bitcoin_wallet','bitcoin_common','test_util','bitcoin_cli',
             'bitcoin_consensus','minisketch','secp256k1','leveldb','crc32c','univalue',
             'bitcoin_util','bitcoin_crypto','bitcoin_clientversion','-j1']
    subprocess.run(refresh,check=True,cwd=ROOT)
    cwd=build/'src/test'
    flags=(cwd/'CMakeFiles/test_bitcoin.dir/flags.make').read_text()
    options=[]
    for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:
        options.extend(shlex.split(re.search('^'+key+r' = (.*)$',flags,re.M)[1]))
    if any('MERCATURA_MERCAWEAVE' in option for option in options):
        raise RuntimeError('ordinary native ABI required')
    sources=['src/test/main.cpp','src/test/mercaweave/fixtures.cpp','src/test/mercaweave/native_v2.cpp',
             'src/test/mercaweave/production_path/native_tests.cpp',
             'src/test/mercaweave/production_path/wallet_tests.cpp',
             'src/test/mercaweave/production_path/security_tests.cpp',
             'src/test/mercaweave/security_audit/native_tests.cpp',
             'src/test/mercaweave/security_audit/allocation.cpp',
             'src/test/mercahash_v2_tests.cpp']
    commands=[refresh]; objects=[]
    for index,source in enumerate(sources):
        obj=out/f'{index}-{pathlib.Path(source).stem}.o'
        deps=obj.with_suffix('.d'); saved=obj.with_suffix('.command.json')
        cmd=['/usr/bin/c++',*options,'-DMERCATURA_MERCAWEAVE_PRODUCTION_PATH_TEST_ONLY=1',
             '-MMD','-MF',str(deps),'-c',str(ROOT/source),'-o',str(obj)]
        fresh=False
        if obj.exists() and deps.exists() and saved.exists() and json.loads(saved.read_text())==cmd:
            names=shlex.split(deps.read_text().replace('\\\n',' ').split(':',1)[1])
            fresh=all((cwd/name).exists() and (cwd/name).stat().st_mtime_ns<=obj.stat().st_mtime_ns for name in names)
        if not fresh:
            subprocess.run(cmd,check=True,cwd=cwd); saved.write_text(json.dumps(cmd)+'\n')
        commands.append(cmd); objects.append(str(obj))
    original=shlex.split((cwd/'CMakeFiles/test_bitcoin.dir/link.txt').read_text())
    link=[]; index=0
    while index<len(original):
        arg=original[index]; index+=1
        if arg=='-o': index+=1; continue
        if arg.endswith('.o') or arg.startswith('-Wl,--dependency-file='): continue
        if arg.endswith(('.a','.so')): arg=str((cwd/arg).resolve(strict=True))
        link.append(arg)
    link[1:1]=objects; link.extend(['-o',str(out/'wo08_native_tests')])
    subprocess.run(link,check=True,cwd=cwd); commands.append(link)
    (out/'build-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    print(out/'wo08_native_tests')

if __name__=='__main__': main()
