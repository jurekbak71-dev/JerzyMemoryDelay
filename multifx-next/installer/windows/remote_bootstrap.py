# Executed using python3 -c over authenticated SSH; upload_dir injected by Windows.
import hashlib,json,pathlib,subprocess,sys,tarfile
upload=pathlib.Path.home()/upload_dir
archive=upload/'payload.tar.gz'
dest=upload/'unpacked';dest.mkdir(exist_ok=False)
with tarfile.open(archive) as t:
    members=t.getmembers()
    if len(members)>512 or sum(m.size for m in members)>32*1024*1024:raise RuntimeError('Payload too large')
    for m in members:
        p=pathlib.PurePosixPath(m.name)
        if p.is_absolute() or '..' in p.parts or not (m.isfile() or m.isdir()):raise RuntimeError('Unsafe archive')
        target=dest.joinpath(*p.parts)
        if m.isdir():target.mkdir(parents=True,exist_ok=True)
        else:
            target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(t.extractfile(m).read())
            target.chmod(m.mode&0o777)
roots=list(dest.iterdir())
if len(roots)!=1:raise RuntimeError('Invalid payload root')
bundle=roots[0]
checks=json.loads((bundle/'SHA256.json').read_text())
for name,digest in checks.items():
    p=pathlib.PurePosixPath(name)
    if p.is_absolute() or '..' in p.parts:raise RuntimeError('Unsafe manifest')
    f=bundle.joinpath(*p.parts)
    if not f.is_file() or hashlib.sha256(f.read_bytes()).hexdigest()!=digest:raise RuntimeError('Checksum mismatch: '+name)
subprocess.run([sys.executable,str(bundle/'deploy_remote.py')]+sys.argv[1:],check=True)
