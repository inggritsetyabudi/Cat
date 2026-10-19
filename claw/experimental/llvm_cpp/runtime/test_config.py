#!/usr/bin/env python3
"""Runtime packaging must not introduce C/SDK dependencies into default builds."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser()
p.add_argument('--cmake',required=True)
p.add_argument('--source-dir',required=True)
p.add_argument('--cxx-compiler',required=True)
p.add_argument('--generator',required=True)
a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='cat-runtime-config-') as tmp:
    root=Path(tmp)
    common=[a.cmake,'-S',a.source_dir,'-G',a.generator,'-DCMAKE_CXX_COMPILER='+a.cxx_compiler]
    for name,flags,expected in [
        ('default-off',['-DCLAW_ENABLE_RUNTIME_POC=OFF','-DCLAW_ENABLE_LLVM_CPP_POC=OFF','-DCLAW_ENABLE_LLD_POC=OFF','-DCMAKE_C_COMPILER=/__missing_c_compiler__'],0),
        ('runtime-only',['-DCLAW_ENABLE_RUNTIME_POC=ON','-DCLAW_ENABLE_LLVM_CPP_POC=OFF','-DCLAW_ENABLE_LLD_POC=OFF'],1),
        ('missing-lld',['-DCLAW_ENABLE_RUNTIME_POC=ON','-DCLAW_ENABLE_LLVM_CPP_POC=ON','-DCLAW_ENABLE_LLD_POC=OFF'],1),
    ]:
        build=root/name
        r=subprocess.run(common+['-B',str(build)]+flags,capture_output=True,text=True,timeout=60)
        if expected:
            assert r.returncode!=0 and 'CLAW_ENABLE_RUNTIME_POC requires' in r.stdout+r.stderr,(name,r.stdout,r.stderr)
        else:
            assert r.returncode==0,(name,r.stdout,r.stderr)
            assert not list(build.rglob('CMakeCCompiler.cmake')),'default enabled C compiler'
        print('PASS:',name)
