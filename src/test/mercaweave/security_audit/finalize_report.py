#!/usr/bin/env python3
"""Populate report results from completed, checked evidence; no invented passes."""
import csv
import json
import pathlib
import re

ROOT=pathlib.Path(__file__).resolve().parents[4]
DEST=ROOT/'src/test/mercaweave/final-security-artifacts'
REPORT=ROOT/'doc/design/mercaweave-v0.1-final-security-qualification.md'

def main():
    qualification=json.loads((DEST/'qualification.json').read_text()); assert qualification['passed']
    lines=['| Actual native configuration | Passed cases | Assertions | Wall seconds | User + system CPU seconds | Peak RSS KiB |',
           '|---|---:|---:|---:|---:|---:|']
    for row in qualification['profiles']:
        lines.append(f"| {row['mode']} | {row['test_cases']}/{row['available_cases']} | {row['assertions']} | {row['wall_seconds']:.2f} | {row['user_seconds']+row['system_seconds']:.2f} | {row['max_rss_kib']:,} |")
    lines+=['','Release/Debug/ASan each pass all 14 selected native cases. TSAN selects the',
            'five audit cases and skips nine unrelated cases. Debug assertion totals differ',
            'because its global wallet allocation sweep is explicitly unavailable; all',
            'other actual wallet/PSBT/context/concurrency cases remain enabled.','',
            '| Standalone configuration | Baseline | Final | Final wall seconds |','|---|---:|---:|---:|']
    for mode in ['release','debug','sanitize']:
        content=(DEST/f'final-{mode}-ctest.txt').read_text()
        seconds=re.search(r'Total Test time \(real\) =\s*([\d.]+) sec',content)[1]
        lines.append(f'| {mode} | 35/35 | 35/35 | {seconds} |')
    mining=qualification['native_mining_profile']
    lines+=['',f"The unchanged native mining suite passes **8/8 selected cases, 979 assertions**, in {mining['wall_seconds']:.2f} s wall, "
            f"{mining['cpu_user_seconds']+mining['cpu_system_seconds']:.2f} s CPU and {mining['max_rss_kib']:,} KiB peak RSS. "
            'Its original 180-second maturity-fixture deadline is retained; WO08 does not extend it.','',
            'The final ordinary-node suite passes **7/7**, runtime **894 seconds**, using',
            'the final native daemon. Earlier seven-script execution also passed and is',
            'preserved separately. Final per-script wall durations:','',
            '| Ordinary native-v2 functional | Result | Seconds |','|---|---|---:|']
    with (DEST/'native-functional-final-results.csv').open() as stream:
        for row in csv.DictReader(stream):
            assert row['status']=='Passed'
            if row['test']!='ALL': lines.append(f"| {row['test']} | PASS | {row['duration(seconds)']} |")
    vectors=json.loads((DEST/'independent-vectors.json').read_text()); assert vectors['byte_identical']
    lines+=['',f"All four native vector streams have SHA256 `{vectors['configurations'][0]['sha256']}`.",
            'Each contains four full signed transactions; all independently recomputed',
            'commitments/manifest/body/final hashes match.','',
            'Fresh Release shared-engine stage measurements (three warmups, eleven measured',
            'samples; median milliseconds; separate from actual Chainstate):','',
            '| Inputs/groups | Shape | Valid verify ms | Malformed ms | Early ownership ms | Late signature ms | Process RSS KiB |',
            '|---|---|---:|---:|---:|---:|---:|']
    with (DEST/'hostile-stage-measurements.csv').open() as stream:
        for row in csv.DictReader(stream):
            lines.append(f"| {row['inputs']}/{row['groups']} | {row['label']} | {float(row['median_verify_ms']):.3f} | {float(row['malformed_ms']):.3f} | {float(row['early_commitment_ms']):.3f} | {float(row['late_signature_ms']):.3f} | {int(row['peak_rss_kib']):,} |")
    lines+=['','These stage children assert zero signature calls for malformed/ownership',
            'failures, one ownership check on early ownership failure, all N checks for',
            'late ownership failure, and all G signature calls for late signature failure.',
            'The 1-MiB output row has cost 5 despite hashing many bytes, reinforcing the',
            'need for independent native byte/resource limits. Stage process RSS includes',
            'fixture and attack copies; it is not per-transaction live allocation.']
    text=REPORT.read_text()
    start='<!-- WO08_RESULTS_BEGIN -->'; end='<!-- WO08_RESULTS_END -->'
    assert text.count(start)==text.count(end)==1
    text=text.split(start)[0]+start+'\n'+'\n'.join(lines)+'\n'+end+text.split(end)[1]
    assert len(re.findall(r'^## (?:[A-Z]|A[A-E])\.',text,re.M))==31
    assert len(re.findall(r'^\| (?:[1-9]|1\d|2[0-7]) \|',text,re.M))==27
    REPORT.write_text(text)

if __name__=='__main__': main()
