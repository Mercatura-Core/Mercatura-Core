#!/usr/bin/env python3
"""Read-only review of research history, immutable evidence and publication scope.

Candidates are labels/paths only: never print a matched secret. Pattern scanning
does not prove absence of every possible credential; review the file inventory.
"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess

ROOT=pathlib.Path(__file__).resolve().parents[4]
START='43158f4eda51cfa5ec7f2fb2fcd50b68061f7080'
ORIGINAL='4b88b619fb1b08c7f30112a38eb471880b2a6af4'
EXPECTED='https://github.com/Mercatura-Core/Mercatura-Core.git'

def git(*args): return subprocess.check_output(['git','-C',str(ROOT),*args])

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=pathlib.Path,required=True)
    args=parser.parse_args()
    assert git('branch','--show-current').decode().strip()=='research/mercaweave-v0.1'
    remotes={name:git('remote','get-url',name).decode().strip() for name in ['origin','project']}
    assert all(url==EXPECTED for url in remotes.values())
    assert git('remote','get-url','--push','origin').decode().strip()==EXPECTED
    original=ROOT.parent/'Mercatura-Core'
    assert subprocess.check_output(['git','-C',str(original),'rev-parse','HEAD']).decode().strip()==ORIGINAL
    assert subprocess.check_output(['git','-C',str(original),'branch','--show-current']).decode().strip()=='work'
    assert not subprocess.check_output(['git','-C',str(original),'status','--porcelain'])
    historical=json.loads((ROOT/'src/test/mercaweave/final-security-artifacts/starting-state.json').read_text())['historical_evidence']
    for row in historical: assert hashlib.sha256((ROOT/row['path']).read_bytes()).hexdigest()==row['sha256'],row['path']
    changed=git('diff','--name-only',START).decode().splitlines()
    production={name for name in changed if not name.startswith(('src/test/','doc/'))}
    assert production<={'src/wallet/pqgroup.cpp','src/wallet/sqlite.cpp','src/wallet/wallet.cpp',
                        'src/crypto/hkdf_sha3_384.cpp','src/script/pqgroup_psbt.cpp'}
    old_hkdf=git('show',START+':src/crypto/hkdf_sha3_384.cpp').decode()
    expected_hkdf=old_hkdf.replace('#include <crypto/hkdf_sha3_384.h>',
        '#include <crypto/hkdf_sha3_384.h>\n#include <support/cleanse.h>').replace(
        '    std::copy_n(t1.begin(), OUTPUT_SIZE, output.begin());',
        '    std::copy_n(t1.begin(), OUTPUT_SIZE, output.begin());\n    memory_cleanse(t1.data(), t1.size());')
    assert (ROOT/'src/crypto/hkdf_sha3_384.cpp').read_text()==expected_hkdf,'only HKDF temporary cleansing is authorized here'
    assert git('show',START+':src/script/pqgroup.cpp')==(ROOT/'src/script/pqgroup.cpp').read_bytes()
    # Includes every distinct blob reachable only through WO01--WO08 commits,
    # including files deleted or superseded later in the research history.
    objects={line.split(' ',1)[0]:line.split(' ',1)[1] if ' ' in line else ''
             for line in git('rev-list','--objects',ORIGINAL+'..HEAD').decode().splitlines()}
    patterns={
        'private-key-envelope':rb'-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----',
        'aws-access-id':rb'\b(?:AKIA|ASIA)[A-Z0-9]{16}\b',
        'github-token':rb'\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})\b',
        'slack-token':rb'\bxox[baprs]-[A-Za-z0-9-]{20,}\b',
        'openai-token':rb'\bsk-(?:proj-|svcacct-)?[A-Za-z0-9_-]{32,}\b',
        'credential-in-url':rb'https?://[^\s/\"\']{2,}:[^\s/@\"\']{3,}@',
    }
    candidates=[]; blobs=[]
    def inspect(name,content,oid=None):
        for label,pattern in patterns.items():
            if re.search(pattern,content): candidates.append(dict(path=name,label=label,object=oid))
        if content.startswith((b'\x7fELF',b'MZ',b'SQLite format 3\x00')):
            candidates.append(dict(path=name,label='executable-or-wallet-database',object=oid))
        if pathlib.PurePosixPath(name).suffix.lower() in {'.dat','.db','.sqlite','.pem','.key','.p12','.pfx'}:
            candidates.append(dict(path=name,label='sensitive-file-extension',object=oid))
        return dict(path=name,object=oid,bytes=len(content),sha256=hashlib.sha256(content).hexdigest())
    for oid,name in objects.items():
        if git('cat-file','-t',oid).strip()==b'blob': blobs.append(inspect(name,git('cat-file','blob',oid),oid))
    current=[]
    names=set(git('diff','--name-only',START).decode().splitlines())
    names.update(git('ls-files','--others','--exclude-standard').decode().splitlines())
    for name in sorted(names):
        path=ROOT/name
        if path.is_file():
            row=inspect(name,path.read_bytes())
            if name!='src/test/mercaweave/final-security-artifacts/publication-preflight.json': current.append(row)
    workflows=[]
    for path in sorted((ROOT/'.github/workflows').glob('*.yml')):
        assert path.read_bytes()==git('show',START+':'+str(path.relative_to(ROOT)))
        workflows.append(dict(path=str(path.relative_to(ROOT)),sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    result=dict(passed=not candidates,head=git('rev-parse','HEAD').decode().strip(),remotes=remotes,
        original_head=ORIGINAL,original_branch='work',original_clean=True,historical_files_verified=len(historical),
        research_commits=int(git('rev-list','--count',ORIGINAL+'..HEAD')),history_blobs=blobs,
        current_files=current,candidates=candidates,workflows=workflows,locked_engine_unchanged=True,
        current_inventory_excludes_own_preflight_hash=True,
        production_files_changed=sorted(production),mercaweave_activation_added=False,
        mercahash_v2_sources_changed=False,
        workflow_review='ci.yml all-branch push runs build/test/lint; no release/deploy; other push allowlists exclude research; macOS manual only',
        scan_limit='Pattern scan and reviewed research inventory; no universal secret absence proof')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    assert not candidates,'publication candidates require review; values intentionally not printed'
    print(json.dumps(dict(passed=True,history_blobs=len(blobs),current_files=len(current),historical=len(historical))))

if __name__=='__main__': main()
