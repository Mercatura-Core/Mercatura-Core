from pathlib import Path
import ctypes,json,os,subprocess,time,statistics
root=Path.cwd(); out=root/'work/m2/performance-o3'; out.mkdir(exist_ok=True)
libc=ctypes.CDLL(None,use_errno=True)
def ordinary_pages():
    if libc.prctl(41,1,0,0,0)!=0:os._exit(125) # PR_SET_THP_DISABLE, inherited by exec.
protocol={'duration_seconds':15,'repetitions':3,'version':2,'cpu_workers':6,'gpu_contexts':286,'gpu_device':'cuda:0','gpu_yield_ms':10,'job_lifetime_seconds':2,'cpu_page_policy':'ordinary pages in both binaries via PR_SET_THP_DISABLE; production default unchanged','compiler_flags':'Both CPU hash implementations -O3; controller/Core -O2; CUDA -O3 compute/sm86; see o3-control-commands.json and cpu-production-compile-command.txt','no_cpu_or_system_power_inference':True}
results=[]
for repetition in range(3):
    for label,mode,original in [('cpu-original-hash-o3','cpu',True),('cpu-integrated-o3','cpu',False),('combined-integrated-o3','combined',False)]:
        binary=root/('work/m2/mining-bench-original-hash-o3' if original else 'build-m2/bin/mining-bench')
        command=[str(binary),'0' if mode=='gpu' else '6','15','1','2',mode,'286','cuda:0']
        prefix=out/f'{repetition+1}-{label}'
        samples=[];started=time.monotonic()
        with prefix.with_suffix('.json').open('w') as stdout,prefix.with_suffix('.err').open('w') as stderr:
            process=subprocess.Popen(command,stdout=stdout,stderr=stderr,preexec_fn=ordinary_pages)
            while process.poll() is None:
                if time.monotonic()-started>60:
                    process.terminate();process.wait(timeout=10);raise RuntimeError('Owned benchmark exceeded watchdog')
                sample={'seconds':time.monotonic()-started}
                try:
                    sample['process_status']={key:value.strip() for line in (Path('/proc')/str(process.pid)/'status').read_text().splitlines() if ':' in line for key,value in [line.split(':',1)] if key in ('VmRSS','VmHWM','VmSize','Threads','THP_enabled')}
                    sample['anon_huge_kib']=next(int(line.split()[1]) for line in (Path('/proc')/str(process.pid)/'smaps_rollup').read_text().splitlines() if line.startswith('AnonHugePages:'))
                    if sample['process_status'].get('THP_enabled') not in (None,'0') or sample['anon_huge_kib']!=0:raise RuntimeError('Ordinary-page comparison policy was not applied')
                except (FileNotFoundError,ProcessLookupError,StopIteration):pass
                telemetry=subprocess.run(['/usr/lib/wsl/lib/nvidia-smi','--query-gpu=memory.used,memory.free,utilization.gpu,temperature.gpu,power.draw,power.limit','--format=csv,noheader,nounits'],capture_output=True,text=True,timeout=5)
                sample['gpu_telemetry']=telemetry.stdout.strip();sample['gpu_telemetry_returncode']=telemetry.returncode
                samples.append(sample);time.sleep(.5)
            if process.returncode:raise RuntimeError(f'{label} failed with {process.returncode}: '+prefix.with_suffix('.err').read_text())
        data=json.loads(prefix.with_suffix('.json').read_text())
        run=data['samples'][0]
        if not run['hashes'] or (mode=='gpu' and run['cpu_hashes']) or (mode=='combined' and (not run['cpu_hashes'] or not run['gpu_hashes'])):raise RuntimeError('Required backend did not contribute completed work')
        row={'label':label,'repetition':repetition+1,'command':command,'wall_seconds':time.monotonic()-started,'data':data}
        results.append(row);prefix.with_suffix('.telemetry.json').write_text(json.dumps(samples,indent=2)+'\n')
        (out/'results.json').write_text(json.dumps({'protocol':protocol,'results':results},indent=2)+'\n')
        print(label,repetition+1,run['hashes_per_second'],flush=True)
summary={label:{'median_hps':statistics.median(row['data']['samples'][0]['hashes_per_second'] for row in results if row['label']==label),'median_cpu_hps':statistics.median(row['data']['samples'][0]['cpu_hashes_per_second'] for row in results if row['label']==label),'median_gpu_hps':statistics.median(row['data']['samples'][0]['gpu_hashes_per_second'] for row in results if row['label']==label)} for label in sorted({row['label'] for row in results})}
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
