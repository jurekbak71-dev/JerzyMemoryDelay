#!/usr/bin/env python3
"""User-local, offline source installer; never modifies system audio configuration."""
import argparse,hashlib,json,os,pathlib,shutil,subprocess,sys,tempfile

def main():
    root=pathlib.Path(__file__).resolve().parent
    meta=json.loads((root/'variant.json').read_text())
    parser=argparse.ArgumentParser(description='Jerzy MultiFX Next '+meta['backend']+' source installer. Default: check only.')
    parser.add_argument('--install',action='store_true',help='compile and install; does not start audio')
    parser.add_argument('--prefix',type=pathlib.Path,default=pathlib.Path.home()/'.local/opt/jerzy-multifx-next')
    parser.add_argument('--cmake',default='cmake')
    parser.add_argument('--jobs',type=int,default=2)
    args=parser.parse_args()
    manifest=json.loads((root/'SHA256.json').read_text())
    for name,digest in manifest.items():
        file=root/name
        if not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest()!=digest:
            raise RuntimeError('checksum mismatch: '+name)
    if not 1<=args.jobs<=16:raise RuntimeError('jobs must be 1..16')
    target=args.prefix.expanduser().resolve()/(meta['backend']+'-'+meta['source_id'])
    if target.exists():raise RuntimeError('destination already exists: '+str(target))
    subprocess.run([args.cmake,'--version'],check=True)
    subprocess.run(['pkg-config','--exists','jack' if meta['backend']=='jack2' else 'alsa'],check=True)
    print('Integrity and audio SDK metadata OK; compiler/build tools validated during configure. Target:',target,flush=True)
    if not args.install:return 0
    # Build before changing the installation prefix. Every variant compiles identical plugins.
    with tempfile.TemporaryDirectory(prefix='jerzy-build-') as tmp:
        build=pathlib.Path(tmp)/'build'
        subprocess.run([args.cmake,'-S',str(root/'source/multifx-next'),'-B',str(build),'-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=OFF','-DNEXT_JACK='+('ON' if meta['backend']=='jack2' else 'OFF'),'-DNEXT_ALSA='+('ON' if meta['backend']=='alsa' else 'OFF')],check=True)
        subprocess.run([args.cmake,'--build',str(build),'--parallel',str(args.jobs)],check=True)
        host='JerzyMultiFXNext' if meta['backend']=='jack2' else 'JerzyMultiFXNextALSA'
        subprocess.run([str(build/host),'--help'],check=True)
        args.prefix.expanduser().mkdir(parents=True,exist_ok=True)
        stage=pathlib.Path(tempfile.mkdtemp(prefix='.staging-',dir=target.parent))
        try:
            (stage/'bin').mkdir()
            shutil.copy2(build/host,stage/'bin'/host)
            shutil.copytree(build/'plugins',stage/'plugins')
            shutil.copytree(build/'legacy-catalog',stage/'legacy-catalog')
            shutil.copytree(root/'source/multifx-next/tools',stage/'tools')
            shutil.copy2(root/'README-PL.txt',stage/'README-PL.txt')
            shutil.copy2(root/'variant.json',stage/'variant.json')
            # Self-relative launcher permits moving the complete install directory.
            launcher=stage/'start.sh'
            launcher.write_text('#!/bin/sh\nset -eu\nHERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nexec "$HERE/bin/'+host+'" "$@"\n')
            launcher.chmod(0o755)
            shutil.copytree(root/'source/multifx-next/vendor',stage/'licenses-and-vendor')
            (stage/'UNINSTALL.txt').write_text('Stop this host, then remove only this version directory. No system files or services were installed. Save any presets first.\n')
            os.rename(stage,target)
        finally:
            if stage.exists():shutil.rmtree(stage)
    print('INSTALLED:',target)
    print('Start explicitly:',target/'start.sh')
    print('Plugins:',target/'plugins')
    return 0
if __name__=='__main__':
    try:sys.exit(main())
    except (OSError,RuntimeError,subprocess.CalledProcessError) as exc:
        print('INSTALL ERROR:',exc,file=sys.stderr);sys.exit(1)
