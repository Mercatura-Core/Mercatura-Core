#!/usr/bin/env python3
"""Collect completed WO07C evidence; refuse failed or missing final executions."""
import argparse
import csv
import hashlib
import json
import pathlib
import platform
import re
import os
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[4]
WORK = ROOT/'work/wo07c'
ARTIFACTS = ROOT/'src/test/mercaweave/production-path-artifacts'


def json_rows(text):
    rows = []
    for line in text.splitlines():
        if line.startswith('WO07C_JSON '):
            rows.append(json.loads(line.removeprefix('WO07C_JSON ')))
        elif line.startswith('{'):
            try: rows.append(json.loads(line))
            except json.JSONDecodeError: pass
    return rows


def boost(path, expected):
    text = path.read_text()
    summary = re.search(r'Test module .*? has passed with:\n\s+(\d+) test cases out of (\d+) passed\n\s+(\d+) assertions out of (\d+) passed',text)
    assert summary and int(summary[1]) == int(summary[2]) == expected,path
    assert summary[3] == summary[4],path
    return dict(cases=expected,assertions=int(summary[3]),log=str(path.relative_to(ROOT))),json_rows(text)


def write_csv(name,rows):
    with (ARTIFACTS/name).open('w',newline='') as stream:
        writer = csv.DictWriter(stream,fieldnames=list(rows[0]))
        writer.writeheader(); writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    ARTIFACTS.mkdir(exist_ok=True)
    results = {}
    contexts = []
    native_rows = []
    for mode in ['release','debug','sanitize']:
        ctest = WORK/f'ctest-{mode}-final.log'
        text = ctest.read_text()
        matched = re.search(r'100% tests passed(?:, 0 tests failed)? out of (\d+)',text)
        assert matched and int(matched[1]) == 35,ctest
        testlog = (ROOT/f'work/build-{mode}/Testing/Temporary/LastTest.log').read_text()
        rows = json_rows(testlog)
        timings = [dict(configuration=mode,**row) for row in rows if 'inline_us' in row]
        assert len(timings) == 13,(mode,'context scenarios')
        context_summary = [row for row in rows if row.get('assertions') == 1070 and row.get('mutations') == 1000]
        assert len(context_summary) == 1 and context_summary[0]['passed'],mode
        contexts.extend(timings)
        native = WORK/('native-release-latest.log' if mode == 'release' else f'native-{mode}-step3.log')
        native_result,rows = boost(native,14)
        budget = json.loads((WORK/f'native-{mode}'/'mining-budget.json').read_text())
        native_result['mining_fixture_timeout_seconds'] = budget['timeout_seconds']
        native_rows.extend(dict(configuration=mode,**row) for row in rows)
        results[mode] = dict(standalone_ctests=35,existing_standalone=34,new_context=1,
                             context_assertions=1070,native=native_result,
                             new_native_cases=6,existing_native_cases=8,
                             combined_asan_ubsan=mode == 'sanitize',
                             leak_detection='NOT TESTED' if mode == 'sanitize' else 'not applicable')
    functional = [row for row in csv.DictReader((WORK/'native-functional-results.csv').open()) if row['test'] != 'ALL']
    assert len(functional) == 7 and all(row['status'].lower() == 'passed' for row in functional),functional
    # This focused latest-storage regression is deliberately separate from the
    # earlier seven-script checkpoint, rather than inflating distinct counts.
    latest_functional = (WORK/'native-wallet-latest-functional.log').read_text()
    assert 'Tests successful' in latest_functional or 'Test successful' in latest_functional,latest_functional[-1000:]
    audit = json.loads((WORK/'boundary-audit-final.json').read_text()); assert audit['passed']
    profile = json.loads((WORK/'native-release-profile.json').read_text()); assert profile['exit_code'] == 0
    _,profile_rows = boost(WORK/'native-release-latest.log',14)
    qualifier = dict(initial_head='0de90cccbb33c033edd5c3578a02d53967791682',
                     source_head=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),
                     configurations=results,native_functionals=functional,
                     final_storage_followup='mercatura_wallet_encryption_persistence.py',
                     distinct_standalone_tests=35,distinct_native_boost_cases=14,
                     distinct_native_functional_scripts=7,
                     baseline_runs_not_added_to_final_totals=True,
                     profile=profile,real_node_grouped_v3='NOT TESTED',network_activation_added=False,
                     gap1='CLOSED WITH RESTRICTIONS: isolated genuine chainstate components',
                     gap2='CLOSED WITH RESTRICTIONS: isolated genuine CWallet components',
                     leak_sanitizer='NOT TESTED: ptrace limitation; detect_leaks=0',
                     production_allocation_failure_injection='NOT TESTED',
                     thread_sanitizer='NOT TESTED',original_worktree_unchanged=True,pushed=False)
    (ARTIFACTS/'qualification.json').write_text(json.dumps(qualifier,indent=2)+'\n')
    (ARTIFACTS/'boundary-audit.json').write_text(json.dumps(audit,indent=2)+'\n')
    (ARTIFACTS/'native-scenarios.jsonl').write_text(''.join(json.dumps(row,sort_keys=True)+'\n' for row in native_rows))
    (ARTIFACTS/'context-scenarios.jsonl').write_text(''.join(json.dumps(row,sort_keys=True)+'\n' for row in contexts))
    (ARTIFACTS/'profile-scenarios.jsonl').write_text(''.join(json.dumps(row,sort_keys=True)+'\n' for row in profile_rows))
    provenance = []
    for name in subprocess.check_output(['git','-C',str(ROOT),'diff','--name-only',
        '0de90cccbb33c033edd5c3578a02d53967791682'],text=True).splitlines():
        path = ROOT/name
        if path.suffix in {'.cpp','.h','.py'} or path.name == 'CMakeLists.txt':
            content = path.read_bytes()
            provenance.append(dict(configuration='shared-source',path=name,bytes=len(content),
                                   sha256=hashlib.sha256(content).hexdigest()))
    for path in sorted((ROOT/'src/test/mercaweave/production_path').iterdir()):
        if path.suffix in {'.cpp','.h','.py'} and str(path.relative_to(ROOT)) not in {row['path'] for row in provenance}:
            content = path.read_bytes()
            provenance.append(dict(configuration='shared-source',path=str(path.relative_to(ROOT)),bytes=len(content),
                                   sha256=hashlib.sha256(content).hexdigest()))
    for mode,build in [('release',ROOT/'work/wo07-native-release'),
                       ('debug',WORK/'native-debug-build'),('sanitize',WORK/'native-sanitize-build')]:
        for path in [build/'CMakeCache.txt',build/'compile_commands.json',build/'bin/mercaturad',
                     WORK/f'native-{mode}'/'build-commands.json',WORK/f'native-{mode}'/'wo07c_native_tests']:
            content = path.read_bytes()
            provenance.append(dict(configuration=mode,path=str(path.relative_to(ROOT)),bytes=len(content),
                                   sha256=hashlib.sha256(content).hexdigest()))
    write_csv('build-provenance.csv',provenance)
    build_commands = {}
    for mode in ['release','debug','sanitize']:
        build_commands[mode] = json.loads((WORK/f'native-{mode}'/'build-commands.json').read_text())
        if mode != 'release':
            execution = json.loads((WORK/f'native-{mode}-execution.json').read_text())
            for row in execution:
                row['rss_scope'] = 'cumulative maximum child RSS including earlier build steps; not isolated test RSS'
                if row['exit_code'] and row['log'].endswith('step3.log'):
                    row['preserved_failure_log'] = f'work/wo07c/native-{mode}-step3-attempt1.log'
                    assert (ROOT/row['preserved_failure_log']).is_file()
            (ARTIFACTS/f'native-{mode}-initial-execution.json').write_text(json.dumps(execution,indent=2)+'\n')
            final_profile = json.loads((WORK/f'native-{mode}-profile-final.json').read_text())
            assert final_profile['exit_code'] == 0
            (ARTIFACTS/f'native-{mode}-final-execution.json').write_text(json.dumps(final_profile,indent=2)+'\n')
        budget = json.loads((WORK/f'native-{mode}'/'mining-budget.json').read_text())
        (ARTIFACTS/f'native-{mode}-mining-budget.json').write_text(json.dumps(budget,indent=2)+'\n')
    (ARTIFACTS/'native-build-commands.json').write_text(json.dumps(build_commands,indent=2)+'\n')
    environment = dict(platform=platform.platform(),machine=platform.machine(),
        compiler=subprocess.check_output(['c++','--version'],text=True).splitlines()[0],
        cmake=subprocess.check_output([str(ROOT/'work/tools/cmake/data/bin/cmake'),'--version'],text=True).splitlines()[0],
        affinity_cpus=len(os.sched_getaffinity(0)),cpu_max=pathlib.Path('/sys/fs/cgroup/cpu.max').read_text().strip(),
        memory_max_bytes=pathlib.Path('/sys/fs/cgroup/memory.max').read_text().strip(),
        clang_available=bool(shutil.which('clang++')),afl_available=bool(shutil.which('afl-fuzz')),
        native_functional_jobs=1,positive_v3_network_process_tests=False,
        external_system_sdk_libraries_instrumented=False,
        standalone_sanitizer_flags='-O1 -g0 -fsanitize=address,undefined -fno-omit-frame-pointer',
        compact_symbols_reason='bounded disk; instrumentation retained',
        leak_detection_enabled=False)
    (ARTIFACTS/'environment.json').write_text(json.dumps(environment,indent=2)+'\n')
    print(json.dumps(dict(passed=True,configurations=3,ctests_per_configuration=35,
                         boost_cases_per_configuration=14,native_functionals=7,
                         prior_evidence_files=len(audit['prior_evidence']))))


if __name__ == '__main__':
    main()
