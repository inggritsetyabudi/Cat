"""Bounded shell stdout evidence checks, without executing repository shell code.

This is deliberately not a general Bash interpreter. Only top-level assertions
and assertions inside the existing positive sanitizer availability probes qualify.
Unsupported wrappers fail closed; names/commands, never source line numbers,
identify the checks. Full native execution remains a separate acceptance gate.
"""
from __future__ import annotations

import re

STDOUT_IF = re.compile(r'^if \[\[ "\$(\w*stdout)" != (.+) \]\]; then$')
SANITIZER_IF = re.compile(
    r'^if (?:printf .*\| clang -fsanitize=[^;]+; then|'
    r'\[\[ -x "\$CWD_TEST_DIR/asan_probe" \]\]; then)$'
)


def _commands(lines: list[str]):
    """Logical commands with heredoc bodies excluded, including quoted heredocs."""
    index = 0
    while index < len(lines):
        start = index
        command = lines[index].strip()
        while command.endswith('\\') and index + 1 < len(lines):
            index += 1
            command = command[:-1] + lines[index].strip()
        index += 1
        if not command or command.startswith('#'):
            continue
        yield start, command
        # The runners' embedded Python is data, not executable shell evidence.
        match = re.search(r"<<(-?)\s*(['\"]?)([A-Za-z_]\w*)\2", command)
        if match:
            delimiter = match.group(3)
            while index < len(lines):
                value = lines[index].lstrip('\t') if match.group(1) else lines[index]
                index += 1
                if value == delimiter:
                    break


def stdout_check_errors(lines: list[str], assertion: str) -> list[str]:
    """Require a live mismatch condition, runtime capture and fatal failure body."""
    wanted = assertion.strip()
    match = STDOUT_IF.fullmatch(wanted)
    if not match:
        return ['stdout check must be an explicit if [[ "$actual_stdout" != expected ]]; then comparison']
    commands = list(_commands(lines))
    contexts: list[tuple[str, bool]] = []
    found = []
    for pos, (_, command) in enumerate(commands):
        if command == wanted:
            found.append((pos, list(contexts)))
        control = re.sub(r'^(?:(?:!|time)\s+)+', '', command)
        if re.match(r'^if(?:\s|$)', control):
            contexts.append(('if', bool(SANITIZER_IF.fullmatch(command))))
        elif re.match(r'^(?:elif|else)(?:\s|$)', control):
            if contexts:
                contexts[-1] = (contexts[-1][0], False)
        elif re.match(r'^fi(?:\s|;|$)', control):
            if contexts and contexts[-1][0] == 'if':
                contexts.pop()
        elif re.match(r'(?:function\s+\w+(?:\s*\(\s*\))?|\w+\s*\(\s*\))\s*(?:\{|$)', command):
            contexts.append(('function', False))
        elif command == '}':
            if contexts and contexts[-1][0] == 'function':
                contexts.pop()
        elif re.match(r'(?:for|while|until|case|select)\b', command):
            contexts.append(('control', False))
        elif command in {'done', 'esac'}:
            if contexts and contexts[-1][0] == 'control':
                contexts.pop()
        elif re.search(r'(?:^|[;&|!\s])(?:\{|\()\s*$', command):
            contexts.append(('group', False))
        elif re.match(r'^[})](?:\s|;|$)', command) and contexts and contexts[-1][0] == 'group':
            contexts.pop()
    if len(found) != 1:
        return ['stdout comparison must occur exactly once as executable shell syntax']
    pos, parents = found[0]
    errors = []
    if any(not allowed for _, allowed in parents):
        errors.append('stdout comparison is inside an unsupported/inactive control wrapper')
    # Allow only reporting followed by an unconditional nonzero exit. A nested
    # `if false`, `exit 0`, or `echo "exit 1"` cannot masquerade as the assertion.
    body = []
    for _, command in commands[pos + 1:]:
        if command == 'fi':
            break
        body.append(command)
    if not body or not re.fullmatch(r'exit [1-9][0-9]*', body[-1]):
        errors.append('stdout mismatch must end in an unconditional nonzero exit')
    if any(not re.match(r'^(?:echo|printf)\s', command) for command in body[:-1]):
        errors.append('stdout mismatch body contains unsupported control or commands')
    actual = match.group(1)
    captures = [command for _, command in commands[:pos]
                if re.match(re.escape(actual) + r'="?\$\(', command)]
    if not captures:
        errors.append(f'stdout comparison has no runtime capture for {actual}')
    if captures and not re.match(re.escape(actual) + r'="?\$\((?:normalize_stdout\b|tr\b|timeout [1-9][0-9]*s\b)', captures[-1]):
        errors.append(f'stdout runtime capture uses unsupported command for {actual}')
    return errors
