from pathlib import Path
import json
import subprocess
import shlex
root=Path.cwd()
build=root/'build-m2'
out=root/'work/m2'
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands','bitcoin_crypto'],text=True).splitlines()
command=next(shlex.split(line) for line in commands if '-c '+str(root/'src/crypto/mercahash_v2.cpp') in line)
obj=out/'mercahash_v2_control_o3.o'
command[command.index('-o')+1]=str(obj)
for flag in ('-MF','-MT'):
    if flag in command:
        command[command.index(flag)+1]=str(obj)+('.d' if flag=='-MF' else '')
command.append('-O3')
link=next(row.copy() for row in json.loads((out/'baseline-build-commands.json').read_text()) if '-o' in row and row[row.index('-o')+1]==str(out/'mining-bench-original'))
link.insert(link.index('lib/libbitcoin_node.a'),str(obj))
link[link.index('-o')+1]=str(out/'mining-bench-original-hash-o3')
(out/'o3-control-commands.json').write_text(json.dumps([command,link],indent=2)+'\n')
with (out/'o3-control-build.log').open('w') as log:
    for args in (command,link):
        subprocess.run(args,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
# Retain the exact prior matrix protocol, now with both CPU hash implementations at O3.
s=(out/'benchmark-native.py').read_text().replace("work/m2/performance'", "work/m2/performance-o3'")
s=s.replace("[('cpu-original','cpu',True),('cpu-integrated','cpu',False),('gpu-original','gpu',True),('gpu-integrated','gpu',False),('combined-integrated','combined',False)]", "[('cpu-original-hash-o3','cpu',True),('cpu-integrated-o3','cpu',False),('combined-integrated-o3','combined',False)]")
s=s.replace("work/m2/mining-bench-original'", "work/m2/mining-bench-original-hash-o3'")
s=s.replace("C++ configured Release -O2; CUDA configured Release -O3 compute/sm86; exact baseline-build-commands.json", "Both CPU hash implementations -O3; controller/Core -O2; CUDA -O3 compute/sm86; see o3-control-commands.json and cpu-production-compile-command.txt")
(out/'benchmark-native-o3.py').write_text(s)
print('Built original hash O3 control; production consensus objects remain unchanged')
