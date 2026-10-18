#!/usr/bin/env python3
"""Require inline-value errors to agree before lowering or native artifacts."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> int:
    compiler = str(Path(sys.argv[1]).resolve())
    fixtures = Path(__file__).resolve().parent / "fixtures"
    # The expression begins at column 13 after four spaces and `println(`.
    cases = [
        ("incompatible", (fixtures / "revise_bad_inline_incompatible_branch.cat").read_text(),
         "Incompatible branch types in if expression: Int32 and String.", 2, 13),
        ("missing_else", (fixtures / "revise_bad_inline_missing_else.cat").read_text(),
         "'if' as a value requires an 'else' branch.", 2, 13),
        ("unit_direct", "fn main() {\n    println(println(1))\n}\n", "type Unit", 2, 13),
        ("unit_if", "fn main() {\n    println(if true { println(1) } else { println(2) })\n}\n",
         "type Unit", 2, 13),
        ("unit_pick", "choice Route { left right }\nfn main() {\n    val route: Route = left\n    println(pick route { left { println(1) } right { println(2) } })\n}\n",
         "type Unit", 4, 13),
        ("tuple_if", "fn main() {\n    println(if true { (1, 2) } else { (3, 4) })\n}\n",
         "type Tuple", 2, 13),
        ("array_if", "fn main() {\n    println(if true { [1, 2] } else { [3, 4] })\n}\n",
         "type Array", 2, 13),
        ("range", "fn main() {\n    println(1..3)\n}\n", "type Range", 2, 13),
        ("choice", "choice Route { left right }\nfn main() {\n    val route: Route = left\n    println(route)\n}\n",
         "type Route", 4, 13),
        ("shape", "shape Point { x: Int32 }\nfn main() {\n    println(Point { x: 1 })\n}\n",
         "type Point", 3, 13),
        ("print_unit", "fn main() {\n    print(println(1))\n}\n", "type Unit", 2, 11),
        ("arity_zero", "fn main() {\n    println()\n}\n", "Call to 'println' expects 1 argument(s), got 0", 2, 5),
        ("arity_many", "fn main() {\n    print(1, 2)\n}\n", "Call to 'print' expects 1 argument(s), got 2", 2, 5),
        ("shadowed_builtin", "fn main() {\n    val println = 1\n    println(2)\n}\n",
         "Expression is not callable: println", 3, 5),
        ("float128", "fn unsupported(value: Float128) {\n    println(value)\n}\nfn main() {}\n",
         "type Float128", 2, 13),
    ]
    env = dict(os.environ, NO_COLOR="1")
    with tempfile.TemporaryDirectory(prefix="claw-inline-errors-") as temporary:
        root = Path(temporary)
        for name, source, diagnostic, line, column in cases:
            path = root / f"{name}.cat"
            path.write_text(source, encoding="utf-8")
            for mode in ("check", "oir", "llvm", "build"):
                exe = root / f"{name}-{mode}.exe"
                args = [compiler, mode, str(path)]
                if mode == "build":
                    args.append(str(exe))
                result = subprocess.run(args, capture_output=True, text=True,
                                        encoding="utf-8", env=env, timeout=30, check=False)
                stderr = result.stderr.replace("\r", "")
                expected_location = f"{name}.cat:{line}:{column}"
                valid = (result.returncode != 0 and not result.stdout
                         and "error[semantic]" in stderr and diagnostic in stderr
                         and expected_location in stderr
                         and not exe.exists() and not exe.with_suffix(".ll").exists())
                if not valid:
                    raise SystemExit(f"FAIL {name}/{mode}: expected semantic rejection at "
                                     f"{expected_location} containing {diagnostic!r}, no stdout/artifacts\n"
                                     f"exit={result.returncode}\nstdout={result.stdout}\nstderr={stderr}")
            print(f"[inline-errors/pass] {name}: check/OIR/LLVM/build, source span, no artifacts")
    print(f"Inline value rejection matrix: {len(cases)} cases, {len(cases) * 4} command checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
