"""Optional cross-toolchain regression: host pkg-config metadata must not win over sysroot."""
import argparse,os,pathlib,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--cmake',default='cmake');p.add_argument('--sysroot',required=True);p.add_argument('--prefix',required=True);p.add_argument('--suffix',default='');a=p.parse_args()
source=pathlib.Path(__file__).parents[1]
with tempfile.TemporaryDirectory(prefix='cross-pkg-',dir=os.environ.get('TMPDIR')) as temp:
 temp=pathlib.Path(temp);fake=temp/'host';fake.mkdir()
 (fake/'jack.pc').write_text('Name: jack\nDescription: host contamination fixture\nVersion: 999.0\nLibs: -L/usr/lib/x86_64-linux-gnu -ljack\nCflags: -I/usr/include\n')
 env=os.environ.copy();env['PKG_CONFIG_PATH']=str(fake);env['PKG_CONFIG_LIBDIR']=str(fake);env['PKG_CONFIG_SYSROOT_DIR']='/'
 cmd=[a.cmake,'-S',str(source),'-B',str(temp/'build'),'-DCMAKE_TOOLCHAIN_FILE='+str(source/'cmake/arm64-linux.cmake'),'-DNEXT_ARM64_SYSROOT='+a.sysroot,'-DNEXT_ARM64_COMPILER_PREFIX='+a.prefix,'-DNEXT_ARM64_COMPILER_SUFFIX='+a.suffix,'-DBUILD_TESTING=OFF']
 run=subprocess.run(cmd,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 if run.returncode:print(run.stdout);raise SystemExit(run.returncode)
 cache=(temp/'build/CMakeCache.txt').read_text()
 if 'JACK_VERSION:INTERNAL=999.0' in cache:raise SystemExit('FAIL: host jack.pc contaminated cross configuration')
 if 'JACK_VERSION:INTERNAL=' not in cache:raise SystemExit('FAIL: JACK metadata was not discovered')
 print('PASS: target JACK metadata selected despite hostile host PKG_CONFIG_PATH/LIBDIR/SYSROOT_DIR')
