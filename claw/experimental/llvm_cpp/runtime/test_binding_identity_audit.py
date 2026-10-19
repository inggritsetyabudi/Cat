#!/usr/bin/env python3
"""Evidence-only: do NOT change identity semantics or admit sibling owners."""
from __future__ import annotations
import argparse,hashlib,json,os,re,sys
from pathlib import Path
from test_strings import (runtime_argv,no_compiler,check_run,native_bytes,
                          profile,isolate,verify_exe,verify_target,HarnessError,
                          file_record,stamp,run,clean_ledger,SAN_ENV,sanitizer_argv)

BASELINE='e394800450593026426c3f56a18ac4473864d997'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('emitter','production','runtime','external','manifest','cc','artifacts','string-preparation','git'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--expected-sha',required=True)
    p.add_argument('--profile',required=True)
    p.add_argument('--ucrt-prefix',type=Path);p.add_argument('--sysroot',type=Path)
    p.add_argument('--readobj',type=Path)
    a=p.parse_args();root=a.artifacts.absolute();root.mkdir(parents=True,exist_ok=True)
    repo=Path(__file__).resolve().parents[4]
    report={'schema':'cat-binding-identity-audit/v1','status':'running','baseline_implementation':BASELINE,
            'head':a.expected_sha,'profile':a.profile,'decision':'STOP: no universal declaration/use/drop ID before LIR',
            'cases':[],'commands':[]}
    def execute(argv,label,env=None,ok=0):
        rec={'phase':label,**run(argv,env=env)};report['commands'].append(rec)
        if ok is not None and rec['exit']!=ok:raise HarnessError(label+': '+rec['stderr'])
        return rec
    try:
        actual=execute([a.git,'-C',repo,'rev-parse','HEAD'],'exact-head')['stdout'].strip()
        if actual!=a.expected_sha:raise HarnessError('source HEAD changed since configure')
        # This gate must not silently turn a stopped audit into implementation.
        lock=json.loads((Path(__file__).parent/'binding-identity-implementation-lock.json').read_text())
        if lock['baseline_implementation']!=BASELINE:raise HarnessError('wrong baseline implementation lock')
        for path,expected in lock['files'].items():
            if hashlib.sha256((repo/path).read_bytes()).hexdigest()!=expected:raise HarnessError('evidence-only audit changed protected implementation/oracle: '+path)
        tracked=execute([a.git,'-C',repo,'ls-files','--',*lock['protected_paths']],'tracked-implementation-files')['stdout'].splitlines()
        if set(tracked)!=set(lock['files']):raise HarnessError('protected implementation file set changed')
        report['baseline_code_lock']=lock
        manifest=json.loads(a.manifest.read_text());archive=Path(manifest['archive']['path']).resolve()
        if archive!=a.runtime.resolve():raise HarnessError('runtime archive/manifest mismatch')
        prep=json.loads(a.string_preparation.read_text())
        if prep['status']!='PASS':raise HarnessError('preceding String preparation unavailable')
        protected=dict(prep['protected_files']);protected.update({str(Path(manifest[k]['path']).resolve()):stamp(Path(manifest[k]['path']).resolve())for k in('source','object','archive')})
        report['preparation']=prep;prof=profile(a);report['crt_inputs']=prof['required_files']
        emitter,emit_env,emit_isolation=isolate(a.emitter.absolute(),'emitter',root/'isolated-emitter',a.readobj);no_compiler(emit_env,'identity-emission')
        lld,link_env,link_isolation=isolate(a.external.absolute(),'external',root/'isolated-linker',a.readobj);no_compiler(link_env,'identity-link')
        run_env=os.environ.copy();run_env['PATH']=''
        report['isolation']={'emitter':emit_isolation,'linker':link_isolation,'compiler_reachable':False}
        cases=json.loads((Path(__file__).parent/'binding-identity-cases.json').read_text())
        if len(cases)!=6 or sum(c['allocations']for c in cases)!=22:raise HarnessError('audit corpus altered')
        for c in cases:
            name=c['name'];d=root/name;d.mkdir(exist_ok=True);cat=d/(name+'.cat');cat.write_text(c['source'],encoding='utf-8')
            row={'name':name,'source':file_record(cat),'expected_stdout_hex':native_bytes(c['stdout'].encode()).hex(),
                 'allocations_per_execution':c['allocations'],'traces':{},'profiles':{}}
            for stage in('check','oir','lir','llvm'):
                record=execute([a.production,stage,cat],name+':'+stage);row['traces'][stage]=record
                (d/(stage+'.txt')).write_bytes(bytes.fromhex(record['stdout_hex']))
            for label,flags in(('O0',[]),('O2',['--release'])):
                exe=d/(label+'.production.exe')
                build=execute([a.production,'build',*flags,cat,exe],name+':'+label+':build')
                observed=execute([exe],name+':'+label+':native-run',env=run_env);check_run(observed,c['stdout'].encode())
                if sys.platform=='linux':
                    hdr=exe.read_bytes()[:64]
                    if hdr[:6]!=b'\x7fELF\x02\x01' or int.from_bytes(hdr[16:18],'little')not in(2,3)or int.from_bytes(hdr[18:20],'little')!=62:raise HarnessError('production exe is not native ELF64')
                else:verify_exe(exe)
                ir=exe.with_suffix('.ll');text=ir.read_text();allocas=[]
                for body in re.findall(r'^define[^\n]*\{(.*?)^\}',text,re.M|re.S):
                    tokens=re.findall(r'^\s*(%[^=\n]+?)\s*=\s*alloca\b',body,re.M)
                    if len(tokens)!=len(set(tokens)):raise HarnessError('duplicate alloca SSA token')
                    allocas.append(tokens)
                native_obj=d/(label+'.reference.o')
                # Reference-only native codegen, not experimental per-source emission/link.
                compiled=execute([a.cc,'-Wno-override-module','-O2'if label=='O2'else'-O0','-c',ir,'-o',native_obj],name+':'+label+':reference-object')
                variants={}
                for variant,lib in [('observer',prep['observer_archive']),*([('sanitized_runtime',prep['sanitized_archive'])]if prep.get('sanitized_archive')else[])]:
                    target=d/(label+'.'+variant+'.exe');argv=runtime_argv(lld,native_obj,target,prof,d/(label+'-'+variant),Path(lib['path']))
                    if variant=='sanitized_runtime':argv=sanitizer_argv(argv,prep)
                    linked=execute(argv,name+':'+label+':'+variant+':LLD',env=link_env)
                    result=execute([target],name+':'+label+':'+variant+':run',env={**run_env,**SAN_ENV}if variant=='sanitized_runtime'else run_env)
                    if variant=='observer':
                        if result['stdout_hex']!=native_bytes(c['stdout'].encode()).hex()or not clean_ledger(result['stderr'],c['allocations']):raise HarnessError('observer output/ledger mismatch')
                    else:check_run(result,c['stdout'].encode())
                    variants[variant]={'link':linked,'run':result}
                row['profiles'][label]={'build':build,'native':observed,'IR':file_record(ir),'alloca_tokens_by_function':allocas,'reference_compile':compiled,'variants':variants}
            obj=d/'program.o';ir=d/'program.ll';obj.unlink(missing_ok=True);ir.unlink(missing_ok=True)
            emitted=execute([emitter,cat,'--strings','--runtime-i32','--arithmetic-i32','-o',obj,'--emit-ir',ir],name+':PoC',env=emit_env,ok=None)
            row['poc']={'expected':c['baseline_poc'],'emit':emitted}
            if c['baseline_poc']=='reject':
                if emitted['exit']==0 or obj.exists()or ir.exists():raise HarnessError('unsupported sibling owner emitted output')
                if 'duplicate object binding'not in emitted['stderr']:raise HarnessError('wrong rejection boundary')
                row['poc']['no_object_or_ir']=True
            else:
                if emitted['exit']!=0:raise HarnessError(emitted['stderr'])
                row['poc']['target']=verify_target(obj,ir);exe=d/'program.exe'
                linked=execute(runtime_argv(lld,obj,exe,prof,d/'poc-link',archive),name+':PoC:LLD',env=link_env)
                observed=execute([exe],name+':PoC:run',env=run_env);check_run(observed,c['stdout'].encode());verify_exe(exe)
                row['poc'].update(link=linked,run=observed)
            report['cases'].append(row)
        for path,before in protected.items():
            if stamp(Path(path))!=before:raise HarnessError('immutable runtime changed '+path)
        report['immutable_inputs']=protected;report['status']='PASS'
        report['observer_total_per_profile']=22;report['observer_total_both_profiles']=44
        report['sanitizer_scope']='instrumented C runtime only; no universal emitted LLVM instrumentation; Windows availability in preparation probes'
    except Exception as e:
        report['status']='FAIL';report['error']=str(e);raise
    finally:(root/'testdata.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS evidence-only identity audit: 6 real Cat cases; production O0/O2; 4 PoC rejections + 2 controls; 44 alloc/free across both profiles')
if __name__=='__main__':main()
