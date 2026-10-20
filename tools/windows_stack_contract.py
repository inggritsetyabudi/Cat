#!/usr/bin/env python3
"""TEST ONLY: immutable PR36 attempt-1 PE/disassembly contract, not a gate fix."""
from __future__ import annotations
import hashlib
import re
import struct
from pathlib import Path

EXPECTED = {
    'baseline-O0': '3bafd2d9f4e5148f070c94bd62c48316fc452d307b5ed0c57d06bd81a406784b',
    'baseline-O2': '1efece78f12699e17b03e54f51cfb6ab8b012c5c8099753517bd339004541602',
    'fixed-O0': '4ffd6725db64f4184e6dcdcbdfb959fbd4042766491f89e4db44c249ec65a209',
    'fixed-O2': '4c13c128521ad4e214928f959c22b0f035eda9e646b12385838dbe2d99ba9e46',
}
SOURCE_HEAD = '6d697e88a8f679e26732f906f85ae831cdc52bca'
BASELINE_HEAD = '28d5e3b2ea554a3a663561fc606c4bae78497540'
ARCHIVE_SHA256 = '1e3ca92e235812654583885841a688a6bdf81512eae9ef0f12ef7aeae7e2bc8c'

class ContractError(ValueError):
    pass

def need(ok, why):
    if not ok:
        raise ContractError(why)

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def pe(path):
    data = Path(path).read_bytes()
    need(data[:2] == b'MZ', 'missing MZ')
    off = struct.unpack_from('<I', data, 0x3c)[0]
    need(data[off:off+4] == b'PE\0\0', 'missing PE')
    machine, count = struct.unpack_from('<HH', data, off+4)
    size = struct.unpack_from('<H', data, off+20)[0]
    opt = off+24
    need(machine == 0x8664 and struct.unpack_from('<H', data, opt)[0] == 0x20b, 'not x64 PE32+')
    base = struct.unpack_from('<Q', data, opt+24)[0]
    reserve, commit = struct.unpack_from('<QQ', data, opt+72)
    sections = []
    for i in range(count):
        vsize, rva, rawsize, raw = struct.unpack_from('<IIII', data, opt+size+i*40+8)
        sections.append((rva, rawsize, raw))
    def read(rva, n):
        for start, length, raw in sections:
            if start <= rva and rva+n <= start+length:
                return data[raw+rva-start:raw+rva-start+n]
        raise ContractError(f'RVA outside raw PE sections: {rva:#x}')
    return base, reserve, commit, read

def parse_disassembly(text):
    instructions, labels = {}, {}
    for line in text.splitlines():
        label = re.match(r'^\s*([0-9a-f]+) <(.+)>:$', line)
        if label:
            labels[label[2]] = int(label[1], 16)
            continue
        ins = re.match(r'^\s*([0-9a-f]+):\s*((?:[0-9a-f]{2}\s+)+)\s*(\S+)(?:\s+(.*))?$', line)
        if ins:
            instructions[int(ins[1],16)] = (bytes.fromhex(ins[2]), ins[3], (ins[4] or '').strip())
    return instructions, labels

def build_contract(exe, disassembly):
    need(digest(exe) == EXPECTED['baseline-O0'], 'baseline is not immutable attempt-1 binary')
    base, reserve, commit, read = pe(exe)
    ins, labels = parse_disassembly(disassembly)
    need(labels.get('___chkstk_ms') == base+0x3cc0, 'unexpected chkstk label/RVA')
    need(labels.get('revise_int16_range::main') == base+0x1590, 'unexpected main label/RVA')
    # All parsed instruction bytes must agree with the pinned PE, not just names.
    for address, (raw, _, _) in ins.items():
        need(read(address-base,len(raw)) == raw, f'disassembly/PE mismatch at {address-base:#x}')
    chk = bytes.fromhex('5150483d00100000488d4c241872194881e90010000048830900482d00100000483d0010000077e74829c1488309005859c3')
    need(read(0x3cc0,len(chk)) == chk, 'unexpected chkstk implementation')
    sites = [(0x1eec,0x1ef1,0x1ef6),(0x1f27,0x1f2c,0x1f31)]
    for mov, call, sub in sites:
        need(read(mov,5) == bytes.fromhex('b810000000'), 'allocation is not 16 bytes')
        need(read(call,1) == b'\xe8' and call+5+struct.unpack('<i',read(call+1,4))[0] == 0x3cc0, 'call target is not chkstk')
        need(read(sub,3) == bytes.fromhex('4829c4'), 'missing dynamic stack subtraction')
    # Explore the immutable final-loop CFG. Every path returning to its entry
    # executes both allocation sites, has net -32 bytes, and never restores rsp.
    start, back = base+0x1e5c, base+0x1f99
    addresses = sorted(ins)
    next_address = dict(zip(addresses, addresses[1:]))
    completed = []
    def walk(address, delta, seen_sites, path):
        if address == start and path:
            need(delta == -32 and seen_sites == (0x1ef6,0x1f31), 'loop backedge does not leak two allocations')
            completed.append(delta)
            return
        if not start <= address <= back:
            return  # Explicit loop exit; not a new iteration.
        need(address not in path and address in ins, 'unsupported internal loop or missing instruction')
        raw, op, arg = ins[address]
        if arg.split(',')[-1].strip() == '%rsp':
            if op == 'sub' and arg == '%rax,%rsp':
                need(address-base in (0x1ef6,0x1f31), 'unverified dynamic allocation')
                delta -= 16
                seen_sites += (address-base,)
            elif op in ('sub','add') and arg == '$0x20,%rsp':
                delta += -32 if op == 'sub' else 32
            else:
                raise ContractError('unrecognized stack-pointer write')
        need(op not in ('push','pop','leave','ret','enter'), 'unexpected stack operation in loop')
        successors = []
        if op.startswith('j'):
            target = re.match(r'([0-9a-f]+) <',arg)
            need(target is not None, 'indirect/unknown loop branch')
            successors.append(int(target[1],16))
            if op != 'jmp':
                successors.append(next_address[address])
        else:
            successors.append(next_address[address])
        for successor in successors:
            walk(successor,delta,seen_sites,path | {address})
    walk(start,0,(),set())
    need(completed, 'no verified path through backedge')
    return {
        'format':'windows-stack-contract-v2', 'exe_sha256':EXPECTED['baseline-O0'],
        'source_head':SOURCE_HEAD, 'baseline_source_head':BASELINE_HEAD,
        'image_base':base, 'stack_reserve':reserve, 'stack_commit':commit,
        'chkstk_push_rax_rva':0x3cc1, 'chkstk_bytes_from_push_rax':read(0x3cc1,32).hex(),
        'allocation_return_rvas':[0x1ef6,0x1f31],
        'loop_start_rva':0x1e5c, 'loop_backedge_rva':0x1f99,
        'loop_bytes_sha256':hashlib.sha256(read(0x1e5c,0x1f9e-0x1e5c)).hexdigest(),
        'verified_backedge_paths':len(completed), 'net_stack_delta_each_backedge':-32,
        'disassembly_sha256':hashlib.sha256(disassembly.encode()).hexdigest(),
    }
