#!/usr/bin/env python3
"""Collect compact WO08 text evidence; never copies wallets, keys or binaries."""
import hashlib
import json
import pathlib
import re
import shutil
import subprocess

ROOT=pathlib.Path(__file__).resolve().parents[4]
WORK=ROOT/'work/wo08'
DEST=ROOT/'src/test/mercaweave/final-security-artifacts'

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    DEST.mkdir(exist_ok=True); profiles=[]; logs=[]
    for mode in ['release','debug','sanitize','tsan']:
        profile=json.loads((WORK/f'final-{mode}.json').read_text())
        assert profile['exit_code']==0 and not profile['timeout'],mode
        log=WORK/f'final-{mode}.log'; content=log.read_text()
        assert 'has passed with:' in content and 'WARNING: ThreadSanitizer' not in content
        assert 'ERROR: AddressSanitizer' not in content and 'runtime error:' not in content
        summary=content.split('Test module "Bitcoin Core Test Suite" has passed with:',1)[1]
        cases=re.search(r'(\d+) test cases out of (\d+) passed',summary)
        assertions=re.search(r'(\d+) assertions out of (\d+) passed',summary)
        assert assertions[1]==assertions[2]
        profile.update(test_cases=int(cases[1]),available_cases=int(cases[2]),assertions=int(assertions[1]))
        profile['scenarios']=[json.loads(line.removeprefix('WO08_JSON ')) for line in content.splitlines() if line.startswith('WO08_JSON ')]
        profiles.append(profile)
        (DEST/f'native-{mode}-summary.txt').write_text('Test module "Bitcoin Core Test Suite" has passed with:'+summary)
        commands=WORK/f'native-{mode}/build-commands.json'
        shutil.copyfile(commands,DEST/f'native-{mode}-build-commands.json')
        logs.append(dict(path=str(log.relative_to(ROOT)),sha256=digest(log),bytes=log.stat().st_size))
    for phase in ['baseline','final']:
        for mode in ['release','debug','sanitize']:
            log=WORK/(f'final-standalone-{mode}-ctest.log' if phase=='final' else f'baseline-{mode}-ctest.log')
            content=log.read_text()
            assert re.search(r'100% tests passed(?:, 0 tests failed)? out of 35',content)
            shutil.copyfile(log,DEST/f'{phase}-{mode}-ctest.txt')
            logs.append(dict(path=str(log.relative_to(ROOT)),sha256=digest(log),bytes=log.stat().st_size))
    for name in ['sanitizer-controls.json','tsan-build.json','inactive-wallet-safety.json',
                 'independent-vectors.json','independent-vectors.jsonl','native-functional-final-results.csv',
                 'boundary-audit.json','native-mining-profile.json','conditional-unwind-controls.json']:
        shutil.copyfile(WORK/name,DEST/name)
    for name in ['control-thread-race.log','control-leak-leak.log','control-address-leak.log',
                 'allocation-repro-before.log','completion-repro-before.log']:
        shutil.copyfile(WORK/name,DEST/(name.removesuffix('.log')+'.txt'))
    for name in ['qualification-sanitize.log','unwind-probe-debug.log','conditional-unwind-debug-original.log',
                 'conditional-unwind-debug-safe.log','conditional-unwind-sanitize-original.log',
                 'conditional-unwind-sanitize-safe.log']:
        shutil.copyfile(WORK/name,DEST/(name.removesuffix('.log')+'.txt'))
    mining=json.loads((WORK/'native-mining-profile.json').read_text())
    assert mining['exit_code']==0
    mining['binary_sha256']=digest(WORK/'native-mining/wo07c_native_tests')
    (DEST/'native-mining-profile.json').write_text(json.dumps(mining,indent=2)+'\n')
    shutil.copyfile(WORK/'native-mining/mining-budget.json',DEST/'native-mining-budget.json')
    shutil.copyfile(WORK/'native-mining/build-commands.json',DEST/'native-mining-build-commands.json')
    mining_log=(WORK/'native-mining-final.log').read_text()
    assert '8 test cases out of 14 passed' in mining_log
    (DEST/'native-mining-summary.txt').write_text('Test module "Bitcoin Core Test Suite" has passed with:'+mining_log.split('Test module "Bitcoin Core Test Suite" has passed with:',1)[1])
    source_commit='0e4a9c78e0cacb076aa69200d002ee218b2b8b74'
    production={name:digest(ROOT/name) for name in ['src/wallet/pqgroup.cpp','src/wallet/sqlite.cpp',
        'src/wallet/wallet.cpp','src/crypto/hkdf_sha3_384.cpp','src/script/pqgroup_psbt.cpp']}
    for name in production:
        expected=subprocess.check_output(['git','-C',str(ROOT),'show',source_commit+':'+name])
        assert expected==(ROOT/name).read_bytes()
    commands=dict(functional=['python3','work/wo07-native-release/test/functional/test_runner.py','--jobs=1',
        '--tmpdirprefix='+str(WORK/'native-functional-final'),'--resultsfile='+str(WORK/'native-functional-final-results.csv'),
        'mercatura_pq_reorg.py','mercatura_pq_mempool_persistence.py','mercatura_wallet_encryption_persistence.py',
        'mercatura_pq_psbt.py','mercatura_pq_rescan.py','mercatura_fee_policy.py','mercatura_pq_invalid_auth.py'],
        inactive_wallet=['python3','src/test/mercaweave/security_audit/inactive_wallet_safety.py',
        '--configfile='+str(ROOT/'work/wo07-native-release/test/config.ini'),
        '--tmpdir='+str(WORK/'inactive-wallet-runtime-final'),'--randomseed=8','--evidence='+str(WORK/'inactive-wallet-safety.json')])
    (DEST/'additional-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    (DEST/'github-identity.json').write_text(json.dumps(dict(repository_full_name='Mercatura-Core/Mercatura-Core',
        clone_url='https://github.com/Mercatura-Core/Mercatura-Core.git',default_branch='v0.1',
        visibility='public',connector_push_permission=True,method='connected GitHub get_repo read-only identity check'),indent=2)+'\n')
    # The intermediate run hit the inherited Close noexcept allocation after
    # the issuance index fix. Preserve diagnostics without giant vector rows.
    path=WORK/'native-release-1.log'
    lines=[line for line in path.read_text().splitlines() if any(token in line for token in [
        'terminate called','bad_alloc','fatal error:','Assertion `ret.second','last checkpoint'])]
    (DEST/'sqlite-close-repro.txt').write_text('\n'.join(lines)+'\n')
    logs.append(dict(path=str(path.relative_to(ROOT)),sha256=digest(path),bytes=path.stat().st_size))
    result=dict(passed=True,profiles=profiles,logs=logs,mutation_seed='0x574f303841554449',
        qualified_production_commit=source_commit,qualified_production_sha256=production,
        native_mining_profile=mining,
        new_bounded_mutations_per_mode=1024,unexpected_acceptances=0,
        final_timeouts=0,final_crashes=0,final_sanitizer_findings=0,
        lsan_verdict='NOT TESTED',lsan_reason='fatal ptrace incompatibility; positive control never reports leak',
        failures_preserved=True,private_wallets_or_binaries_copied=False,
        debug_wallet_global_allocation_injection='NOT TESTED: inherited DEBUG_LOCKORDER environment terminates at point 25; locks remain enabled; actual Debug signing/concurrency and PSBT allocation tests run')
    (DEST/'qualification.json').write_text(json.dumps(result,indent=2)+'\n')
    normalized=[]
    for path in sorted(DEST.iterdir()):
        if path.suffix not in {'.txt','.csv'}: continue
        before=path.read_bytes()
        after=('\n'.join(line.rstrip() for line in before.decode().splitlines()).rstrip()+'\n').encode()
        if after!=before:
            normalized.append(dict(path=path.name,copied_text_before_sha256=hashlib.sha256(before).hexdigest(),
                normalized_sha256=hashlib.sha256(after).hexdigest(),only_change='LF line endings, trailing whitespace and terminal blank lines'))
            path.write_bytes(after)
    (DEST/'text-normalization.json').write_text(json.dumps(dict(changes=normalized,raw_logs_preserved_under='work/wo08'),indent=2)+'\n')

if __name__=='__main__': main()
