from pathlib import Path
import subprocess,shlex,json
root=Path.cwd(); build=root/'build-m2'; out=root/'work/m2'
commit='48a3a614b741db541d939acd50c3603d40f3ff2e'
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands','mining-bench','mercatura-gpu-check'],text=True).splitlines()
records=[]
def run(args):
    records.append(args)
    (out/'baseline-build-commands.json').write_text(json.dumps(records,indent=2)+'\n')
    with (out/'baseline-build.log').open('a') as log:subprocess.run(args,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
def compile_file(name,newname,content=None):
    original=root/'src'/name
    text=content if content is not None else subprocess.check_output(['git','show',f'{commit}:src/{name}'],text=True)
    source=out/newname;source.write_text(text)
    command=next(shlex.split(line) for line in commands if f'-c {original}' in line)
    output=out/(newname+'.o')
    command[command.index('-c')+1]=str(source)
    command[command.index('-o')+1]=str(output)
    for flag in ('-MF','-MT'):
        if flag in command:command[command.index(flag)+1]=str(output)+('.d' if flag=='-MF' else '')
    run(command)
    return output
cpu=compile_file('mining/cpu_miner.cpp','cpu_miner_original.cpp')
cuda=compile_file('mining/cuda_backend.cu','cuda_backend_original.cu')
# Modern instrumentation/CLI, but report the original controller's actual payload.
bench=compile_file('mining/mining_bench.cpp','mining_bench_original.cpp',(root/'src/mining/mining_bench.cpp').read_text().replace('mining::CpuScratchpadBytes(version)','mercahash::ScratchpadBytes(version)'))
for target in ('mining-bench','mercatura-gpu-check'):
    line=next(line for line in commands if f'-o bin/{target} ' in line)
    command=next(shlex.split(part.strip()) for part in line.split('&&') if f'-o bin/{target} ' in part)
    command[command.index('-o')+1]=str(out/(target+'-original'))
    command[command.index('lib/libmercatura_cuda.a')]=str(cuda)
    if target=='mining-bench':
        command[command.index('src/mining/CMakeFiles/mining-bench.dir/mining_bench.cpp.o')]=str(bench)
        command.insert(command.index('lib/libbitcoin_node.a'),str(cpu))
    run(command)
print('Built original CPU/CUDA controller and GPU checker with matched configured flags')
