#!/usr/bin/env python3
"""Explicit, offline v0.4 guitar preset migration. Never loads plugin code."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile

NAME = re.compile(r'[a-z0-9_]{1,48}\Z')
IR_NAME = re.compile(r'[A-Za-z0-9_.-]{1,100}[.]wav\Z')
CAB_DEFAULTS = {'on':0.,'mix':1.,'blend':0.,'level':.5,'lowcut':.12,'highcut':.85,'ir0':'','ir1':''}
SYNTH_FIELDS = frozenset(('bass','pad','root','style','scale','mode','density','basslevel',
    'padlevel','level','pan','model','chord','engine1','engine2','evolution','bassfx','padfx','sync','freebpm'))
CAB_FIELDS = ('mix','blend','level','lowcut','highcut','on')
SYNTH_MIDI = frozenset('synth.'+k for k in ('bass','pad','root','style','scale','density',
    'basslevel','padlevel','level','pan','model','chord','engine1','engine2','evolution','bassfx','padfx','sync','freebpm'))


def object_fields(value, allowed, required=(), label='object'):
    if type(value) is not dict:
        raise ValueError(f'{label}: expected object')
    if set(value)-set(allowed):
        raise ValueError(f'{label}: unsupported fields: {sorted(set(value)-set(allowed))}')
    if set(required)-set(value):
        raise ValueError(f'{label}: missing fields: {sorted(set(required)-set(value))}')
    return value


def number(value, low=0., high=1., label='value'):
    if type(value) not in (int,float) or not low<=value<=high or not math.isfinite(value):
        raise ValueError(f'{label}: expected finite number in [{low}, {high}]')
    return float(value)


def integer(value, low, high, label):
    if type(value) is not int or not low<=value<=high:
        raise ValueError(f'{label}: expected integer in [{low}, {high}]')
    return value


def read_json(path):
    def unique(pairs):
        result={}
        for key,value in pairs:
            if key in result:
                raise ValueError(f'{path}: duplicate JSON key: {key}')
            result[key]=value
        return result
    with path.open(encoding='utf-8') as f:
        return json.load(f,object_pairs_hook=unique,
                         parse_constant=lambda s: (_ for _ in ()).throw(ValueError('non-finite JSON number')))


def readable_under(root, relative):
    root=Path(root).resolve(strict=True)
    path=(root/relative).resolve(strict=True)
    if not path.is_relative_to(root) or not path.is_file():
        raise ValueError(f'path escapes directory or is not a file: {relative}')
    with path.open('rb') as f:
        f.read(1)
    return path


def legacy_metadata(modules, key):
    # Installed metadata takes precedence. Repository fallback is the actual source
    # JSON used to construct v0.4's catalog, never an inferred parameter list.
    candidate=Path(modules)/(key+'.json')
    if candidate.exists() or candidate.is_symlink():
        path=readable_under(modules,key+'.json')
    else:
        root=Path(__file__).resolve().parents[2]/'multifx2/manifests'
        path=readable_under(root,key+'.json')
    meta=read_json(path)
    if type(meta) is not dict or meta.get('key')!=key or type(meta.get('params')) is not list or len(meta['params'])>48:
        raise ValueError(f'invalid legacy manifest: {path}')
    defaults=[]
    seen=set()
    for p in meta['params']:
        if type(p) is not dict or type(p.get('key')) is not str or p['key'] in seen:
            raise ValueError(f'invalid legacy parameter manifest: {path}')
        seen.add(p['key'])
        defaults.append(number(p.get('def'),label=f'{key} manifest default'))
    return defaults


def slot(ident, plugin, path, asset, params, mode='ACTIVE', latency=0):
    return {'id':ident,'plugin_id':plugin,'version':'1.0.0','path':str(path),'asset':str(asset),
            'mode':mode,'inputs':2,'latency':latency,'midi':False,'parameters':params,
            'ranges':{k:[0.,1.] for k in params}}


def convert(source, modules, plugins, irs, output, align_dry=False):
    old=object_fields(read_json(source),('chain','bpm','master','cab','synth','midi'),
                      ('chain','bpm','master'),'preset')
    if type(old['chain']) is not list or len(old['chain'])>16:
        raise ValueError('chain: expected at most 16 entries')
    result={'schema_version':1,'bpm':number(old['bpm'],20,300,'bpm'),
            'master':1.4*number(old['master'],label='master'),'automatic':True,
            'slots':[],'audio_edges':[],'midi_routes':[],'midi_bindings':[]}
    warnings=[]
    positions={}
    parameter_counts={}
    for position, entry in enumerate(old['chain']):
        e=object_fields(entry,('key','params','enabled','route'),('key','enabled'),f'chain[{position}]')
        key=e['key']
        if type(key) is not str or not NAME.fullmatch(key):
            raise ValueError('invalid module key')
        if type(e['enabled']) is not bool:
            raise ValueError('enabled must be boolean')
        route=integer(e.get('route',0),0,1,'route')
        defaults=legacy_metadata(modules,key)
        values=e.get('params',defaults)
        if type(values) is not list or len(values)!=len(defaults):
            raise ValueError(f'{key}: parameter count mismatch with manifest')
        values=[number(v,label=f'{key} parameter') for v in values]
        parameter_counts[position]=len(values)
        if route==1:
            warnings.append(f'excluded chain position {position} ({key}): BassAmbient route 1 is unsupported')
            continue
        module=readable_under(modules,key+'.so')
        plugin=readable_under(plugins,'legacy/plugin.so')
        ident=len(result['slots'])+1
        positions[position]=ident
        values=values+[.5]*(48-len(values))
        result['slots'].append(slot(ident,'legacy',plugin,module,
            {f'p{i}':v for i,v in enumerate(values)},'ACTIVE' if e['enabled'] else 'BYPASS'))
    synth=object_fields(old.get('synth',{}),SYNTH_FIELDS,label='synth')
    for key,value in synth.items():
        number(value,label='synth.'+key)
    if synth:
        warnings.append('synth settings excluded: BassAmbient is not converted to an instrument')
    cab=object_fields(old.get('cab',{}),CAB_DEFAULTS,label='cab')
    cab={**CAB_DEFAULTS,**cab}
    for key in CAB_FIELDS:
        cab[key]=number(cab[key],label='cab.'+key)
    ir_paths=[]
    for key in ('ir0','ir1'):
        value=cab[key]
        if type(value) is not str:
            raise ValueError('IR name must be a string')
        if value:
            if not IR_NAME.fullmatch(value) or not value.endswith('.wav') or value in ('.','..'):
                raise ValueError('invalid IR filename')
            ir_paths.append(str(readable_under(irs,value)))
        else:
            ir_paths.append('')
    cab_id=None
    assets={}
    if any(ir_paths):
        if len(result['slots'])>=16:
            raise ValueError('no free slot for CAB: maximum 16 slots')
        cab_id=len(result['slots'])+1
        asset=output.with_name(output.stem+'.cab.json')
        assets[asset]={'ir_a':ir_paths[0],'ir_b':ir_paths[1],
                       'dry_mode':'aligned' if align_dry else 'legacy'}
        result['slots'].append(slot(cab_id,'cab_dual',readable_under(plugins,'cab_dual/plugin.so'),asset,
            {('enabled' if k=='on' else k):cab[k] for k in CAB_FIELDS},latency=64))
        if align_dry:
            warnings.append('aligned dry explicitly selected: changes v0.4 partial-mix phase/timing')
    elif cab.get('on') or 'cab' in old:
        warnings.append('CAB has no IR: no CAB slot created; CAB MIDI targets will be excluded')
    rows=old.get('midi',[])
    if type(rows) is not list or len(rows)>128:
        raise ValueError('midi: expected at most 128 rows')
    used=set()
    for row in rows:
        row=object_fields(row,('channel','cc','target'),('channel','cc','target'),'midi row')
        channel=integer(row['channel'],1,16,'MIDI channel')
        cc=integer(row['cc'],0,127,'MIDI CC')
        if (channel,cc) in used:
            raise ValueError('duplicate channel/CC mapping')
        used.add((channel,cc))
        target=row['target']
        if type(target) is not dict or type(target.get('kind')) is not str:
            raise ValueError('invalid MIDI target')
        kind=target['kind']
        low,high,param=0.,1.,0
        if kind=='fx':
            object_fields(target,('kind','position','key','index'),('kind','position','key','index'),'FX MIDI target')
            position=integer(target['position'],0,len(old['chain'])-1,'MIDI position')
            if target['key']!=old['chain'][position]['key']:
                raise ValueError('MIDI FX key does not match chain position')
            param=integer(target['index'],0,parameter_counts[position]-1,'MIDI parameter index')
            ident=positions.get(position)
        else:
            object_fields(target,('kind',),('kind',),'MIDI target')
            if kind=='master':ident,high=0,1.4
            elif kind=='bpm':ident,low,high=-1,20.,300.
            elif kind.startswith('cab.') and kind[4:] in CAB_FIELDS:
                ident,param=cab_id,CAB_FIELDS.index(kind[4:])
            elif kind in SYNTH_MIDI:ident=None
            else:raise ValueError(f'unsupported MIDI kind: {kind}')
        if ident is None:
            warnings.append(f'excluded MIDI channel {channel} CC {cc} target {kind}: source is not migrated')
        else:
            result['midi_bindings'].append({'channel':channel,'cc':cc,'target':ident,'param':param,'min':low,'max':high})
    return result,assets,warnings


def publish_family(files):
    """Prepare fully written files, then publish without replacing any existing name.

    Hard links provide atomic no-clobber publication. Roll back links created by
    this invocation if any subsequent publication fails. Each JSON is atomic;
    the family is not a filesystem transaction against process termination.
    """
    temporaries=[]
    published=[]
    try:
        for path,value in files.items():
            if path.exists() or path.is_symlink():
                raise FileExistsError(f'refusing existing output: {path}')
            fd,name=tempfile.mkstemp(prefix='.'+path.name+'.',suffix='.tmp',dir=path.parent)
            temporaries.append(Path(name))
            with os.fdopen(fd,'w',encoding='utf-8') as f:
                json.dump(value,f,indent=2,allow_nan=False);f.write('\n');f.flush();os.fsync(f.fileno())
        for (path,_),temporary in zip(files.items(),temporaries):
            os.link(temporary,path)
            published.append(path)
    except BaseException:
        for path in reversed(published):
            path.unlink()
        raise
    finally:
        for temporary in temporaries:
            temporary.unlink(missing_ok=True)


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path,help='one v0.4 preset object, not a named-preset collection')
    parser.add_argument('--modules-dir',required=True,type=Path)
    parser.add_argument('--plugins-dir',required=True,type=Path)
    parser.add_argument('--ir-dir',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--align-dry',action='store_true',help='explicitly change CAB dry-path timing')
    args=parser.parse_args(argv)
    try:
        source=args.source.resolve(strict=True)
        output=args.output.absolute()
        if output.resolve()==source or output.exists() or output.is_symlink():
            raise ValueError('refusing source or existing output path')
        # Resolve parent before choosing asset names so saved asset references are absolute.
        output=output.parent.resolve(strict=True)/output.name
        result,assets,warnings=convert(source,args.modules_dir,args.plugins_dir,args.ir_dir,output,args.align_dry)
        if any(path.resolve()==source for path in assets):
            raise ValueError('asset output would overwrite source')
        publish_family({**assets,output:result})
        for warning in warnings:
            print('WARNING: '+warning,file=sys.stderr)
        print(f'Created {output}: {len(result["slots"])} slots, {len(result["midi_bindings"])} MIDI bindings')
        return 0
    except (OSError,ValueError,TypeError,KeyError) as error:
        print(f'ERROR: {error}',file=sys.stderr)
        return 2

if __name__=='__main__':
    raise SystemExit(main())
