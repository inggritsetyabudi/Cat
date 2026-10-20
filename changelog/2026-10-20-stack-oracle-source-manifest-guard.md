# 2026-10-20 — Isolated stack-oracle source manifest guard

The TEST ONLY first-chance oracle prototype previously rejected the reviewed
production pair fixture because it compared the entire non-experimental `claw`
tree. Its source preflight now expands the immutable attempt-1 source tree and
compares every committed file path, Git mode, object type and object ID, with
only explicit reviewed exceptions recorded in a digest-pinned manifest.

- Historical source remains `6d697e88a8f679e26732f906f85ae831cdc52bca`.
- The complete opt-in adapter tree is pinned to reviewed
  `f6acc88da962e021321d6f13c8c47f0bb1e1ac20`, tree
  `19f7cc0dc4b17fca71e8b86e582b0b1a41f6e9cf`; it is not ignored.
- Pair fixture, harness, single native-suite invocation, CI evidence uploads
  and their changelog are pinned to exact reviewed `ceff0e9` blobs. The CI diff
  adds evidence uploads, not another stack-diagnostic invocation.
- Only five individually named prototype-maintenance paths are reviewed with
  this correction: this changelog, the isolated workflow, guard, tests and
  manifest. These are not production source exceptions. Review of those files
  remains necessary; this guard is not a self-authenticating security boundary.
- Unknown additions, deletions, mode changes, altered reviewed blobs, other
  experimental subtrees, and all original production/native/stack/runtime inputs
  fail closed. Focused controls exercise accepted exact pair/adapter content and
  rejected production sentinels, original inputs, unknown paths and adapter drift.

The initial correction `26668d4` passed 12 local controls and independent review,
but its exact-head CI remained 8/10: both Windows prototype workers rejected the
manifest digest before tests because checkout line endings differed. That failed
run is retained as run `39382543535`. The follow-up hashes the exact committed
manifest blob (unchanged digest), checks checkout equivalence allowing CRLF-to-LF
conversion only, and adds LF/CRLF positive and content-tampering negative controls.
No production or reviewed adapter/pair blob exception is broadened.

The original stack workflow, observer, binary/archive hashes, provenance and
accepted exception/status rules are unchanged. Historical attempt-1 raw AV
`0xC0000005` remains **unresolved**, irrespective of later green executions.
The historical binaries do not validate the current adapter. Production remains
unchanged, the adapter remains opt-in/default OFF, and pair adapter admission
remains **CLOSED** with no `--pair-i32`. No merge, release, version, or PRD marker.
