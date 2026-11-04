#!/usr/bin/env python3
"""Check WO07C dormant boundaries and preserve all earlier research evidence."""
import argparse
import hashlib
import json
import pathlib
import re
import shlex
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[4]
INITIAL = '0de90cccbb33c033edd5c3578a02d53967791682'
ORIGINAL = '4b88b619fb1b08c7f30112a38eb471880b2a6af4'


def git(*args):
    return subprocess.check_output(['git','-C',str(ROOT),*args])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=pathlib.Path,required=True)
    parser.add_argument('--output',type=pathlib.Path,required=True)
    args = parser.parse_args()
    build = args.build.resolve(strict=True)
    original = ROOT.parent/'Mercatura-Core'
    assert subprocess.check_output(['git','-C',str(original),'rev-parse','HEAD']).decode().strip() == ORIGINAL
    assert not subprocess.check_output(['git','-C',str(original),'status','--porcelain'])
    changed = git('diff','--name-only',INITIAL).decode().splitlines()
    forbidden = ['src/chainparams.cpp','src/consensus/params.h','src/pow.cpp','src/crypto/',
                 'src/kernel/chainparams.cpp','src/qt/','src/rpc/','src/policy/','src/node/miner.cpp',
                 'src/mining/','src/wallet/mining.cpp','src/wallet/test/mining_tests.cpp']
    assert not [name for name in changed if any(name == item or name.startswith(item) for item in forbidden)]
    preserved = []
    for name in git('ls-tree','-r','--name-only',INITIAL).decode().splitlines():
        evidence = (name.startswith('src/test/mercaweave/') and pathlib.Path(name).suffix in {'.json','.jsonl','.csv','.txt'})
        report = name.startswith('doc/design/mercaweave-') and name.endswith('.md')
        if not (evidence or report) or name.endswith('/CMakeLists.txt'):
            continue
        before = git('show',INITIAL+':'+name)
        current = (ROOT/name).read_bytes()
        assert before == current,name
        preserved.append(dict(path=name,sha256=hashlib.sha256(current).hexdigest(),bytes=len(current)))
    old_engine = git('show',INITIAL+':src/test/mercaweave/mercaweave.cpp').decode()
    engine = (ROOT/'src/script/pqgroup.cpp').read_text().replace('#include <script/pqgroup.h>','#include "mercaweave.h"')
    assert engine == old_engine,'locked engine bytes changed'
    commands = json.loads((build/'compile_commands.json').read_text())
    assert all('MERCATURA_MERCAWEAVE' not in row['command'] for row in commands)
    symbols = subprocess.check_output(['nm','-C',str(build/'bin/mercaturad')],text=True)
    assert 'ProductionPathTestAccess::' not in symbols
    assert 'IsolatedExecution::IsolatedExecution(' not in symbols
    for symbol in ['Chainstate::ConnectBlock(', 'Chainstate::DisconnectBlock(', 'CScriptCheck::operator()',
                   'CheckInputScriptsWithPQGroups(', 'wallet::CWallet::FillPQGroupPSBT(']:
        assert symbol in symbols,symbol
    flags = (build/'src/test/CMakeFiles/test_bitcoin.dir/flags.make').read_text()
    options = []
    for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:
        options.extend(shlex.split(re.search('^'+key+r' = (.*)$',flags,re.M)[1]))
    attempts = [
        ('guarded-test-access','#include <test/mercaweave/production_path/access.h>\n','forbidden in ordinary builds'),
        ('private-capability','#include <script/pqgroup_context.h>\nvoid f(){mercaweave::IsolatedExecution x(mercaweave::B32{});}\n','private'),
        ('private-wallet-issuance','#include <wallet/wallet.h>\nvoid f(wallet::CWallet& w){w.IssuePQGroupAddress(0,0);}\n','private'),
        ('private-chainstate-context','#include <validation.h>\nvoid f(Chainstate& c){c.m_group_execution.reset();}\n','private'),
    ]
    boundary = []
    for name,source,diagnostic in attempts:
        result = subprocess.run(['/usr/bin/c++',*options,'-fsyntax-only','-x','c++','-'],
                                input=source,text=True,capture_output=True)
        assert result.returncode != 0 and diagnostic in result.stderr,name
        boundary.append(dict(case=name,rejected=True,exit_code=result.returncode))
    output = dict(passed=True,initial_head=INITIAL,original_head=ORIGINAL,original_clean=True,
                  source_head=git('rev-parse','HEAD').decode().strip(),prior_evidence=preserved,
                  locked_engine_identical=True,ordinary_definitions_absent=True,
                  production_fixture_factory_symbols_absent=True,boundary=boundary,
                  changed_files=changed,network_activation_added=False,
                  ordinary_unknown_v3_is_group_authorization=False)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(output,indent=2)+'\n')
    print(json.dumps(dict(passed=True,preserved_files=len(preserved),compile_rejections=len(boundary))))


if __name__ == '__main__':
    main()
