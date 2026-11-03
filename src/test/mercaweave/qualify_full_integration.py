#!/usr/bin/env python3
"""Collect WO07 qualification after the explicit bounded test commands pass.

Writes new evidence only under full-integration-artifacts. Starts no ordinary
node, native miner, GUI, external signer or network broadcaster. Benchmark and
persistence executables use public fixtures, including native SQLite crash tests.
"""
import argparse
import csv
import hashlib
import io
import json
import os
import pathlib
import platform
import re
import subprocess
ROOT = pathlib.Path(__file__).resolve().parents[3]
BASE = '6cc3925bd7431bd03a07d9b0b04de07550d95022'
STABLE_KEYS = {'wallet_checks','psbt_checks','qt_checks','reward_checks','qt_wallet_checks','native_qr_checks','cache_corpus','native_sqlite'}


def require(value,message):
    if not value:raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['release','debug','sanitize','native','functional_results','qt_logs','solo_logs','native_reconsider_log']:
        parser.add_argument('--'+name.replace('_','-'),type=pathlib.Path,required=True)
    parser.add_argument('--output',type=pathlib.Path,default=ROOT/'src/test/mercaweave/full-integration-artifacts')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    git=lambda *opts:subprocess.check_output(['git','-C',str(ROOT),*opts],text=True).strip()
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
    commands=[]
    def run(cmd):
        commands.append(list(map(str,cmd)))
        return subprocess.check_output(cmd,text=True,env=env,timeout=600)
    builds={name:getattr(args,name).resolve(strict=True) for name in ['release','debug','sanitize']}
    old=list(csv.DictReader((ROOT/'src/test/mercaweave/wallet-artifacts/release-workloads.csv').open()))
    prior={}
    for name in git('ls-tree','-r','--name-only',BASE,'doc/design','src/test/mercaweave').splitlines():
        p=ROOT/name
        if name.startswith('doc/design/mercaweave-') or p.suffix in ['.json','.csv','.jsonl'] or 'artifacts/' in name:
            original=subprocess.check_output(['git','-C',str(ROOT),'show',BASE+':'+name])
            require(p.read_bytes()==original,'previous evidence changed: '+name);prior[name]=sha(p)
    reference,measurements,records,parameters=None,{}, {},None
    persistence_reference=None
    expected_vector=json.loads((ROOT/'src/test/mercaweave/wallet-artifacts/qualification.json').read_text())['deterministic_test_output_sha256']['release']['mercaweave_tests']
    for mode,build in builds.items():
        log=(build/'Testing/Temporary/LastTest.log').read_text()
        counts={'passed':log.count('Test Passed.'),'failed':log.count('Test Failed.'),'skipped':log.count('Test did not run')}
        require(counts=={'passed':32,'failed':0,'skipped':0},'complete green 32-test checkpoint required: '+mode)
        summaries=[]
        for line in log.splitlines():
            try:item=json.loads(line)
            except ValueError:continue
            if isinstance(item,dict) and STABLE_KEYS.intersection(item):summaries.append(item)
        require(len(summaries)==9 and all(item.get('passed') for item in summaries),'missing unit summaries: '+mode)
        if reference is None:reference=summaries
        else:require(reference==summaries,'cross-build test summary divergence: '+mode)
        vector=run([build/'mercaweave_tests','--vectors'])
        require(hashlib.sha256(vector.encode()).hexdigest()==expected_vector,'locked vectors changed')
        workloads=list(csv.DictReader(io.StringIO(run([build/'mercaweave_wallet_bench']))))
        stable=lambda row:{k:v for k,v in row.items() if not k.endswith('_ms')}
        require(len(workloads)==24 and [stable(r) for r in old]==[stable(r) for r in workloads],'WO05 workload divergence')
        text=run([build/'mercaweave_node_bench']);(out/(mode+'-workloads.csv')).write_text(text)
        rows=list(csv.DictReader(io.StringIO(text)));require(len(rows)==14,'missing A-N workload')
        stable_new=lambda row:{k:v for k,v in row.items() if not k.endswith('_ms') and k!='maxrss_kib'}
        if measurements:require([stable_new(r) for r in rows]==[stable_new(r) for r in next(iter(measurements.values()))],'cross-build resource shape divergence')
        measurements[mode]=rows
        current_parameters=[json.loads(line) for line in run([build/'mercaweave_parameter_probe']).splitlines()]
        require(len(current_parameters)==30,'missing native parameter probes')
        if parameters is None:parameters=current_parameters
        else:require(current_parameters==parameters,'native parameter divergence')
        debug_flags=(build/'CMakeFiles/mercaweave_node_validation.dir/flags.make').read_text()
        require(mode!='debug' or '-D_GLIBCXX_ASSERTIONS=1' in debug_flags,'checked STL Debug build required')
        persistence=json.loads(run([build/'mercaweave_durable_tests']))
        require(persistence.get('passed') and persistence.get('assertions')==53,'final persistence status-bound regression required')
        if persistence_reference is None:persistence_reference=persistence
        else:require(persistence_reference==persistence,'final persistence cross-build divergence')
        records[mode]={'ctest':counts,'persistence_status_bound_followup':persistence,'unit_summaries':summaries,'locked_vector_stdout_sha256':expected_vector,'unchanged_wo05_workloads':24,'glibcxx_assertions':mode=='debug','asan_ubsan':mode=='sanitize','leak_detection':False if mode=='sanitize' else None}
        print(mode+': 32-test checkpoint + 53-assert persistence follow-up, unchanged vectors/24 WO05 rows, 14 A-N shapes',flush=True)
    native=args.native.resolve(strict=True);normal_flags=list(native.rglob('flags.make'))
    require(normal_flags and not any('MERCATURA_MERCAWEAVE_' in p.read_text() for p in normal_flags),'ordinary native flags contaminated')
    native_symbols={}
    for name in ['mercaturad','mercatura-cli','test_mining-qt']:
        binary=native/'bin'/name;symbols=run(['nm','-C',binary])
        require(not any(token in symbols for token in ['CheckMercaWeaveResearchAuthorization','node_research::','wallet_research::','attachIsolated']),'research symbol in normal binary: '+name)
        native_symbols[name]={'sha256':sha(binary),'research_symbols_absent':True}
    functional=list(csv.DictReader(args.functional_results.open()))
    require(len(functional)==8 and all(r['status']=='Passed' for r in functional),'seven native functional passes required')
    reconsider=args.native_reconsider_log.read_text()
    require('WO07_NATIVE_LIFECYCLE_PASS:' in reconsider and 'Tests successful' in reconsider and 'Traceback' not in reconsider,'native conflicting-branch/reconsideration/restart test required')
    qt=[]
    for path in sorted(args.qt_logs.glob('native-*.txt')):
        text=path.read_text();totals=re.search(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped, (\d+) blacklisted, (\d+)ms',text)
        require(totals and all(int(totals[i])==0 for i in [2,3,4]),'native Qt failure/skip: '+str(path))
        cases=re.findall(r'PASS\s+: MiningTests::([^\n]+)',text)
        qt.append({'file':path.name,'passed':int(totals[1]),'failed':0,'skipped':0,'duration_ms':int(totals[5]),'cases':cases})
    require(len(qt)>=2,'native linked Qt lifecycle evidence absent')
    solo=[]
    for path in sorted(args.solo_logs.glob('native-*.txt')):
        text=re.sub(r'\x1b\[[0-9;]*m','',path.read_text())
        require('*** No errors detected' in text,'native Solo unit failure: '+str(path))
        cases=re.findall(r'Entering test case "([^"]+)"',text)
        if cases:solo.append({'file':path.name,'passed_cases':cases,'count':len(cases)})
    require(sum(item['count'] for item in solo)==8,'all eight targeted native wallet/Solo/v2 cases required')
    original=pathlib.Path('/workspace/Mercatura-Core')
    require(not subprocess.check_output(['git','-C',str(original),'status','--porcelain'],text=True).strip(),'original worktree dirty')
    summary={'starting_head':BASE,'full_ctest_source_head':'8d56b7285974921d1e37d975def3c5421c528baf','source_head':git('rev-parse','HEAD'),'branch':git('branch','--show-current'),'platform':platform.platform(),'compiler':run(['g++','--version']).splitlines()[0],'builds':records,'native_release':{'normal_compile_flag_files':len(normal_flags),'binaries':native_symbols,'functional':functional,'additional_native_reconsideration_lifecycle':{'passed':True,'normal_v2_only':True,'synchronous_sqlite':True,'exact_utxo_hash_restored':True},'qt':qt,'unit':solo},'prior_evidence_sha256':prior,'commands':commands,'restrictions':['full grouped Chainstate, mempool and CWallet lifecycle remain unqualified','native node and Qt runtime Release only','headless Qt and CPU-reference GPU test doubles; no real GPU runtime','uncached grouped authorization; no experimental cache acceptance','no automatic signing or broadcast, no production durable timer','Linux GCC x86_64 only; no LSan, TSan, coverage-guided fuzzer or independent audit']}
    (out/'qualification.json').write_text(json.dumps(summary,indent=2)+'\n')
    (out/'active-native-parameters.json').write_text(json.dumps(parameters,indent=2)+'\n')
    print(f'PASS: 96 CTest results + 3 persistence follow-ups; 72 WO05 comparisons; 42 A-N rows; {len(prior)} earlier evidence files unchanged',flush=True)


if __name__=='__main__':
    main()
