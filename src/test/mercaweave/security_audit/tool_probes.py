#!/usr/bin/env python3
"""Separate sanitizer positive/negative controls; preserve exact incompatibilities."""
import json
import os
import pathlib
import subprocess

ROOT=pathlib.Path(__file__).resolve().parents[4]
WORK=ROOT/'work/wo08'

def main():
    rows=[]
    for sanitizer,arg in [('thread','safe'),('thread','race'),('leak','leak'),('address','leak')]:
        name=f'control-{sanitizer}-{arg}'; binary=WORK/name
        compile_cmd=['g++','-O1','-g','-pthread',f'-fsanitize={sanitizer}',
                     str(ROOT/'src/test/mercaweave/security_audit/tool_probe.cpp'),'-o',str(binary)]
        env=os.environ.copy()
        env['TSAN_OPTIONS']='halt_on_error=1:exitcode=66'
        env['LSAN_OPTIONS']='exitcode=23'
        env['ASAN_OPTIONS']='detect_leaks=1:halt_on_error=1'
        compiled=subprocess.run(compile_cmd,capture_output=True,text=True)
        row=dict(name=name,compile_command=compile_cmd,compile_exit=compiled.returncode)
        log=WORK/(name+'.log'); command=[str(binary),arg]; row['command']=command
        if compiled.returncode==0:
            try:
                with log.open('w') as stream:
                    result=subprocess.run(command,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=30)
                output=log.read_text(); row.update(exit_code=result.returncode,
                    race_diagnostic='WARNING: ThreadSanitizer: data race' in output,
                    leak_diagnostic='detected memory leaks' in output,
                    ptrace_incompatible='does not work under ptrace' in output,
                    runtime_fatal='FATAL:' in output or 'fatal error' in output)
            except subprocess.TimeoutExpired: row['timeout']=True
        else: log.write_text(compiled.stderr)
        row['log']=str(log.relative_to(ROOT)); rows.append(row)
    (WORK/'sanitizer-controls.json').write_text(json.dumps(rows,indent=2)+'\n')

if __name__=='__main__': main()
