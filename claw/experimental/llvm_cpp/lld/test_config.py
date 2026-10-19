#!/usr/bin/env python3
"""Fail-closed configure contract for the optional linker; no link is attempted."""
import argparse
from pathlib import Path
import os
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    for name in ('cmake', 'source-dir', 'llvm-config', 'cxx-compiler', 'generator'):
        parser.add_argument('--'+name, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='cat-lld-config-') as temporary:
        root = Path(temporary)
        fake = root/('wrong-lld.cmd' if os.name == 'nt' else 'wrong-lld')
        fake.write_text('@echo LLD 23.1.30\n' if os.name == 'nt' else '#!/bin/sh\necho "LLD 23.1.30"\n')
        fake.chmod(0o755)
        cases = [
            ('off-no-sdk', False, False, root/'missing-lld', True, ''),
            ('requires-emitter-poc', False, True, root/'missing-lld', False, 'requires CLAW_ENABLE_LLVM_CPP_POC=ON'),
            ('on-missing-tool', True, True, root/'missing-lld', False, 'requires explicit existing CLAW_LLD_EXECUTABLE'),
            ('on-wrong-patch', True, True, fake, False, 'requires exact LLD 23.1.3'),
            ('lld-off-emitter-on', True, False, root/'missing-lld', True, ''),
        ]
        for name, emitter, linker, executable, success, diagnostic in cases:
            command = [args.cmake, '-S', args.source_dir, '-B', str(root/name),
                '-G', args.generator, '-DBUILD_TESTING=OFF',
                '-DCMAKE_CXX_COMPILER='+args.cxx_compiler,
                '-DCLAW_ENABLE_LLVM_CPP_POC='+('ON' if emitter else 'OFF'),
                '-DCLAW_ENABLE_LLD_POC='+('ON' if linker else 'OFF'),
                '-DCLAW_LLVM_CONFIG='+(args.llvm_config if emitter else str(root/'missing-sdk')),
                '-DCLAW_LLD_EXECUTABLE='+str(executable)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            text = result.stdout+result.stderr
            assert (result.returncode == 0) == success, f'{name}: {text}'
            assert not diagnostic or diagnostic in text, f'{name}: wrong rejection: {text}'
            print('[pass/lld-config]', name, flush=True)


if __name__ == '__main__':
    main()
