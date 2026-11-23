import subprocess, pathlib, json, shutil, argparse
root=pathlib.Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description='Compile and exercise historical DSP offline; no JACK or target-hardware test.')
parser.add_argument('--output',type=pathlib.Path,required=True)
out=parser.parse_args().output.resolve()
for directory in ['bin','modules','logs']:
    (out/directory).mkdir(parents=True,exist_ok=True)
results=[]
def run(name,args,env=None):
    p=subprocess.run(args,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env)
    (out/'logs'/f'{name}.log').write_text('$ '+ ' '.join(map(str,args))+'\n'+p.stdout)
    results.append({'name':name,'command':list(map(str,args)),'exit_code':p.returncode,'output':p.stdout[-3000:]})
    print(name,p.returncode,p.stdout[-150:],flush=True)
    return p.returncode==0
for name,src,inc in [('MemoryDelayDSP','tests/dsp_tests.cpp','source'),('MultiFXDSP','multifx/tests.cpp','multifx'),('MultiFX4DSP','multifx4/tests.cpp','multifx4')]:
    exe=out/'bin'/name
    if run('build-'+name,['g++','-std=c++20','-O1','-g','-I'+inc,src,'-o',str(exe)]): run(name,[str(exe)])
for source in sorted((root/'multifx2/modules').glob('*.cpp'))+[root/'multifx2/examples/lofi_crush.cpp']:
    key=source.stem
    args=['g++','-std=c++20','-O1','-g','-fPIC','-shared',str(source)]
    if key=='reel_echo': args+=['multifx2/vendor/reel/TapeEngine.cpp']
    run('build-'+key,args+['-o',str(out/'modules'/f'{key}.so')])
    manifest=root/('multifx2/examples' if key=='lofi_crush' else 'multifx2/manifests')/(key+'.json')
    shutil.copy(manifest,out/'modules'/manifest.name)
exe=out/'bin/ModuleTests'
if run('build-ModuleTests',['g++','-std=c++20','-O1','-g','multifx2/module_tests.cpp','-ldl','-o',str(exe)]):run('ModuleTests',[str(exe),str(out/'modules')])
run('metadata',['python3','multifx2/check_module_metadata.py',str(out/'modules')])
for folder in ['multifx','multifx2']:
    run(folder+'-panel',['python3',folder+'/test_panel.py'])
for name,src,inc in [('MemoryDelayDSP','tests/dsp_tests.cpp','source'),('MultiFXDSP','multifx/tests.cpp','multifx'),('MultiFX4DSP','multifx4/tests.cpp','multifx4')]:
    exe=out/'bin'/(name+'-asan')
    if run('build-'+name+'-asan',['g++','-std=c++20','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+inc,src,'-o',str(exe)]): run(name+'-asan',[str(exe)])
(out/'results.json').write_text(json.dumps(results,indent=2))
raise SystemExit(0 if all(item['exit_code']==0 for item in results) else 1)
