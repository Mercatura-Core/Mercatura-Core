#!/usr/bin/env python3
"""Record WO08's starting tree and immutable historical evidence without secrets."""
import hashlib
import json
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[4]
START = '43158f4eda51cfa5ec7f2fb2fcd50b68061f7080'
DEST = ROOT/'src/test/mercaweave/final-security-artifacts'


def git(*args):
    return subprocess.check_output(['git','-C',str(ROOT),*args])


def main():
    DEST.mkdir(exist_ok=True)
    evidence = []
    for name in git('ls-tree','-r','--name-only',START).decode().splitlines():
        historic = name.startswith('doc/design/mercaweave-') and name.endswith('.md')
        artifact = name.startswith('src/test/mercaweave/') and pathlib.Path(name).suffix in {'.csv','.json','.jsonl','.txt'}
        if not (historic or artifact) or name.endswith('/CMakeLists.txt'):
            continue
        content = git('show',START+':'+name)
        assert (ROOT/name).read_bytes() == content,name
        evidence.append(dict(path=name,bytes=len(content),sha256=hashlib.sha256(content).hexdigest()))
    original = ROOT.parent/'Mercatura-Core'
    baseline = dict(starting_commit=START,branch=git('branch','--show-current').decode().strip(),
        original_head=subprocess.check_output(['git','-C',str(original),'rev-parse','HEAD'],text=True).strip(),
        original_clean=not subprocess.check_output(['git','-C',str(original),'status','--porcelain']),
        github_repository='https://github.com/Mercatura-Core/Mercatura-Core',
        publication_branch='research/mercaweave-v0.1',historical_evidence=evidence,
        existing_research_commits=int(git('rev-list','--count','4b88b619fb1b08c7f30112a38eb471880b2a6af4..'+START)))
    assert baseline['original_clean']
    baseline_path = DEST/'starting-state.json'
    if baseline_path.exists():
        assert json.loads(baseline_path.read_text()) == baseline,'baseline must not change'
    else:
        baseline_path.write_text(json.dumps(baseline,indent=2)+'\n')
    print(json.dumps(dict(passed=True,historical_files=len(evidence),starting_commit=START)))


if __name__ == '__main__':
    main()
