#!/usr/bin/env python3
"""Bounded WO07B stage runner. Failed stages retain nodes/logs and earlier results."""
import argparse
import dataclasses
import json
import pathlib
import os
import signal
import subprocess
import sys
import time
ROOT=pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'src/test/mercaweave'))
from simulation.core import Manifest,write_json

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--counts',nargs='+',type=int,default=[100,500,1000])
    p.add_argument('--nodes',type=int,default=3);p.add_argument('--seed',type=int,default=7)
    p.add_argument('--custody',choices=['independent','reused','centralized'],default='independent')
    p.add_argument('--batch-size',type=int,default=20);p.add_argument('--epoch-size',type=int,default=1000)
    p.add_argument('--mining-interval',type=int,default=0);p.add_argument('--soak-seconds',type=int,default=0)
    p.add_argument('--max-soak-cycles',type=int,default=10);p.add_argument('--extended',action='store_true')
    p.add_argument('--no-disruptions',action='store_true');p.add_argument('--build',type=pathlib.Path,default=ROOT/'work/wo07-native-release')
    p.add_argument('--work',type=pathlib.Path,required=True);p.add_argument('--timeout',type=int,default=1800)
    args=p.parse_args();work=args.work.resolve();work.mkdir(parents=True,exist_ok=True)
    if (work/'stages.json').exists():raise RuntimeError('use a fresh campaign root; existing evidence is preserved')
    results=[]
    for count in args.counts:
        m=Manifest(seed=args.seed,transactions=count,nodes=args.nodes,epoch_size=args.epoch_size,batch_size=args.batch_size,custody=args.custody,mining_interval=args.mining_interval,soak_seconds=args.soak_seconds,max_soak_cycles=args.max_soak_cycles,disruptions=not args.no_disruptions,extended=args.extended).validate()
        stage=work/f'{count}-{args.seed}';manifest=stage/'manifest.json';write_json(manifest,dataclasses.asdict(m))
        cmd=[sys.executable,str(ROOT/'src/test/mercaweave/simulation/native.py'),'--configfile='+str(args.build.resolve()/'test/config.ini'),'--manifest='+str(manifest),'--output='+str(stage),'--tmpdir='+str(stage/'nodes'),'--randomseed='+str(m.seed)]
        start=time.monotonic()
        with (stage/'run.log').open('w') as log:
            process=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            try:code=process.wait(timeout=args.timeout)
            except (subprocess.TimeoutExpired,KeyboardInterrupt):
                os.killpg(process.pid,signal.SIGINT)
                try:process.wait(timeout=30)
                except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
                code=124
        results.append(dict(transactions=count,seed=m.seed,returncode=code,duration_seconds=time.monotonic()-start,command=cmd))
        write_json(work/'stages.json',results)
        if code:raise SystemExit(code)

if __name__=='__main__':main()
