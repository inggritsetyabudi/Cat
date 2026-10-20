#!/usr/bin/env python3
"""TEST ONLY: validate committed source; never classify Windows exceptions.

The immutable baseline is checked in a separate checkout. Reviewed exceptions
are exact Git objects, not directories ignored from the source comparison.
Only the five named prototype-maintenance files are reviewed with this guard;
they are not production inputs and this check is not a substitute for review.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

SOURCE_HEAD = '6d697e88a8f679e26732f906f85ae831cdc52bca'
ADAPTER_HEAD = 'f2cd0d6d70572a736e0f3fa4186d1f4f010e673e'
PAIR_HEAD = 'ceff0e9eb32a9805e0293dbaa93084e9f12076e7'
MANIFEST_SHA256 = '2387a4bf8f0b87e8adcfaf5db5c081cfc43fcd5254d6ec69228bfcb3f1446565'
ADAPTER_PATH = 'claw/experimental/llvm_cpp'


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args])


def validate_provenance(root, manifest):
    if manifest.get('format') != 'immutable-stack-source-v1':
        raise ValueError('unexpected manifest format')
    def exact_entry(revision, path):
        raw = git(root, 'ls-tree', revision, '--', path).decode().strip()
        if not raw:
            raise ValueError('review provenance path missing: ' + path)
        metadata, actual_path = raw.split('\t', 1)
        if actual_path != path:
            raise ValueError('review provenance path mismatch')
        return tuple(metadata.split())
    if exact_entry(manifest['adapter_reviewed_head'], ADAPTER_PATH) != (
            '040000', 'tree', manifest['adapter_tree']):
        raise ValueError('adapter tree differs from reviewed commit')
    for path, item in manifest['reviewed_changes'].items():
        if exact_entry(item['reviewed_head'], path) != (item['mode'], item['type'], item['oid']):
            raise ValueError('blob differs from reviewed commit: ' + path)


def load_manifest(path):
    # Windows checkout may expand LF to CRLF. Hash the committed blob, not
    # checkout bytes; require the working copy to agree apart from CRLF only.
    root = Path(__file__).resolve().parents[1]
    raw = git(root, 'show', 'HEAD:tools/windows_stack_source_manifest.json')
    if hashlib.sha256(raw).hexdigest() != MANIFEST_SHA256:
        raise ValueError('source manifest digest differs from reviewed manifest')
    if Path(path).read_bytes().replace(b'\r\n', b'\n') != raw:
        raise ValueError('source manifest checkout differs from committed blob')
    result = json.loads(raw)
    if (result['source_head'], result['adapter_reviewed_head'],
        result['pair_reviewed_head'], result['adapter_path']) != (
            SOURCE_HEAD, ADAPTER_HEAD, PAIR_HEAD, ADAPTER_PATH):
        raise ValueError('unexpected source/review provenance')
    validate_provenance(root, result)
    return result


def tree_entries(root, revision):
    # -t includes tree entries, so the entire adapter is pinned by its tree OID.
    # All other directories are expanded: additions/deletions fail closed.
    entries = {}
    for record in git(root, 'ls-tree', '-r', '-t', '-z', revision).split(b'\0'):
        if not record:
            continue
        metadata, path = record.split(b'\t', 1)
        mode, kind, oid = metadata.decode('ascii').split()
        path = path.decode('utf-8')
        if path == ADAPTER_PATH or (kind != 'tree' and not path.startswith(ADAPTER_PATH + '/')):
            entries[path] = (mode, kind, oid)
    return entries


def validate(baseline, current, manifest):
    expected = dict(baseline)
    expected[ADAPTER_PATH] = ('040000', 'tree', manifest['adapter_tree'])
    for path, item in manifest['reviewed_changes'].items():
        expected[path] = (item['mode'], item['type'], item['oid'])
    errors = []
    for path in manifest['prototype_maintenance_paths']:
        # Exact paths only. No directory, symlink or executable exceptions.
        entry = current.get(path)
        if entry is None or entry[:2] != ('100644', 'blob'):
            errors.append(f'missing/non-regular prototype maintenance file: {path}')
        expected.pop(path, None)
    maintenance = set(manifest['prototype_maintenance_paths'])
    for path in sorted(set(expected) | set(current)):
        if path not in maintenance and expected.get(path) != current.get(path):
            errors.append(f'unrecognized source change: {path}')
    if errors:
        raise ValueError('\n'.join(errors))
    return {
        'format': 'immutable-stack-source-proof-v1',
        'source_head': SOURCE_HEAD,
        'adapter_reviewed_head': ADAPTER_HEAD,
        'adapter_tree': manifest['adapter_tree'],
        'pair_reviewed_head': PAIR_HEAD,
        'protected_entries': len(expected),
        'reviewed_exception_paths': sorted(manifest['reviewed_changes']),
        'prototype_maintenance_paths': sorted(maintenance),
        'historical_attempt_1_av_resolved': False,
        'current_adapter_covered_by_historical_binaries': False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', required=True)
    parser.add_argument('--root', default='.')
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    source_head = git(args.source_root, 'rev-parse', 'HEAD').decode().strip()
    if source_head != SOURCE_HEAD:
        raise ValueError('immutable source checkout is not the exact attempt-1 source')
    manifest = load_manifest(Path(__file__).with_name('windows_stack_source_manifest.json'))
    proof = validate(tree_entries(args.source_root, 'HEAD'), tree_entries(args.root, 'HEAD'), manifest)
    proof['test_head'] = git(args.root, 'rev-parse', 'HEAD').decode().strip()
    Path(args.output).write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')
    print('PASS: immutable production/original tests; exact reviewed adapter and pair blobs')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, subprocess.CalledProcessError) as error:
        print(f'SOURCE PREFLIGHT REJECTED: {error}', file=sys.stderr)
        sys.exit(1)
