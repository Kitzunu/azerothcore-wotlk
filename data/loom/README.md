# Loom — toolchain, content & design notes

`data/loom/` is the home of the whole Loom subsystem except the compiled runtime: the authoring
**tools**, the opcode **schema**, the deployable **scripts** and **threads**, and this README. Loom is a
readable, composable replacement for the `smart_scripts` (SAI) *serialization* that reuses the existing
SmartAI execution engine unchanged. The **runtime** is C++ and lives with the engine
(`src/server/game/AI/SmartScripts/LoomLoader.{h,cpp}`, `LoomOpcodes.{h,gen.cpp}`).

The tooling lives here — not under `src/` (AC's C++ codestyle scans all of `src/` and rejects PEP8
double-blank-lines) and not under `apps/`.

> **Status:** the runtime is wired into the tree (§4) but **unbuilt/unverified** — it needs a
> `make` to confirm it compiles against the fkYAML API. The Python toolchain runs end-to-end today
> (§6). **Opcode coverage is complete**: all 92 events / 183 actions / 38 targets are generated from
> the SAI enums, and the entire base `smart_scripts` table (52,768 rows / 14,311 entities) converts
> to Loom with **zero unmapped opcodes** (§5).

## 1. What's where

| Path | Role |
|---|---|
| `data/loom/scripts/*.loom` | **Deployable content** — one behavior file per entity (read at load). |
| `data/loom/threads/*.loom` | **Deployable content** — reusable behavior fragments (auto-scanned; no DB rows). |
| `data/loom/schema/loom_opcodes.json` | **Generated** vocabulary — keyword ↔ SAI enum ↔ named param slots. Read by the Python tools. |
| `data/loom/tools/gen_opcodes.py` | **Generator** — parses `SmartScriptMgr.h` → the JSON schema **and** the C++ tables (no drift). |
| `data/loom/tools/loomc.py` | CI validator/linter (author-time safety; resolves `include:` threads). |
| `data/loom/tools/export_sai_to_loom.py` | Migration: `smart_scripts` → Loom YAML. |
| `src/server/game/AI/SmartScripts/LoomLoader.{h,cpp}` | **Runtime** compiler: reads the `.loom` file → `SmartScriptHolder[]` → `SmartAIMgr`. |
| `src/server/game/AI/SmartScripts/LoomOpcodes.{h,gen.cpp}` | Opcode tables (`.gen.cpp` is generated — do not hand-edit). |
| `data/sql/updates/pending_db_world/rev_*_creature_loom.sql` | The `creature_loom` **index** table (entity → file path). |

The JSON schema and the C++ tables are **both generated from the same enum source** by
`gen_opcodes.py`, so the Python toolchain and the C++ runtime can never disagree about an opcode.
Regenerate after any change to the SAI enums: `python data/loom/tools/gen_opcodes.py`.

## 2. Storage model — the DB is an index, not a blob store

The database does **not** hold script text. `creature_loom` is a thin index: `(entryorguid,
source_type, file)`. The worldserver reads each pointed-at `.loom` file from the **content root**
(worldserver.conf `LoomContentDir`, default `<DataDir>/loom`) and compiles it. This keeps SQL dumps
tiny — one short row per entity instead of embedding 14k multi-line YAML blobs — and lets scripts be
edited, diffed and reviewed as plain files. Threads need no DB rows at all: they're scanned from
`<root>/threads/`.

```
Authoring:   data/loom/**/*.loom ──loomc check──▶ CI gate
Migration:   smart_scripts ──export_sai_to_loom──▶ data/loom/scripts/*.loom  (+ fidelity test, §5)
Runtime:     creature_loom (index) ──▶ read <root>/<file> ──LoomLoader──▶ SmartScriptHolder[] ──▶ SmartAI (unchanged)
```

## 3. The document format (canonical YAML)

```yaml
entity: 12345
type: creature
name: "Example Boss"
events:
  - event: aggro                 # NB: `event:`, NOT `on:` — see the gotcha below
    do:
      - talk: 0
      - cast: { spell: 133, at: victim }
  - event: health_pct
    below: 30
    once: true
    do:
      - phase: enrage
  - event: update_ic
    in_phase: [enrage]
    every: [6000, 9000]
    do:
      - cast: { spell: 15496, at: victim }
```

- **Events** carry named params (`below:`, `every: [min,max]`, `chance:`, `once:`, `in_phase:`).
- A `do:` list compiles to a **link-chain** of holders — exactly how SAI represents multi-action events.
- **Phases** are labels; the loader assigns bit indices in first-seen order (author never sees the bitmask).
- **Targets** hang off each action as `at:` (`self`, `victim`, `invoker`, `creature_range: {...}`, `position: {...}`, …).

### Threads (`include:`) — the composition SAI can't do

A **thread** is a reusable, parameterized behavior fragment in `data/loom/threads/`. A script pulls
one in with `include:`; the loader substitutes `{{ param }}` and splices the thread's events in — so
shared patterns are written and reviewed once instead of copy-pasted across creatures.

```yaml
# data/loom/threads/cast_at_health.loom
thread: cast_at_health
params: [spell, pct]
body: |
  events:
    - event: health_pct
      below: {{ pct }}
      once: true
      do:
        - cast: { spell: {{ spell }}, at: self }
```

```yaml
# data/loom/scripts/46_murloc_forager.loom — entire behavior is one shared pattern
entity: 46
type: creature
name: "Murloc Forager"
include:
  - cast_at_health: { spell: 3368, pct: 40 }
```

A script may mix `include:` and its own `events:` (see `597_bloodscalp_berserker.loom`). Threads are
files, not DB rows — `loomc` and the runtime both discover them by scanning `threads/`.

### The `on:` gotcha (important design decision)

The trigger key is **`event:`**, not `on:`. `on`/`off`/`yes`/`no` are **YAML 1.1 booleans**: PyYAML
(1.1) would parse the key `on:` as `True`, while fkYAML (YAML 1.2) reads the string `"on"`. The Python
tooling and the C++ runtime must never disagree about a document, so Loom avoids YAML-1.1 boolean
keywords entirely. (This bit the first draft and is exactly the kind of thing `loomc` exists to prevent.)

## 4. How it's wired into the tree (done)

These edits are already in place; they still need a `make` to verify compilation.

| # | Where | Change |
|---|---|---|
| 1 | `data/sql/updates/pending_db_world/rev_*_creature_loom.sql` | `creature_loom` + `loom_thread` tables |
| 2 | [`WorldDatabase.h`](../../src/server/database/Database/Implementation/WorldDatabase.h) | added `WORLD_SEL_LOOM_SCRIPTS` enumerator |
| 3 | [`WorldDatabase.cpp`](../../src/server/database/Database/Implementation/WorldDatabase.cpp) | `PrepareStatement(WORLD_SEL_LOOM_SCRIPTS, "SELECT entryorguid, source_type, script FROM creature_loom ...")` |
| 4 | [`SmartScriptMgr.cpp`](../../src/server/game/AI/SmartScripts/SmartScriptMgr.cpp) | `#include "LoomLoader.h"` + `LoomLoader::LoadInto(mEventMap);` at the tail of `LoadSmartAIFromDB()` |
| 5 | [`game/CMakeLists.txt`](../../src/server/game/CMakeLists.txt) | link the header-only `fkYAML` interface target into the game library |

`LoomLoader::LoadInto` fills the existing `SmartAIEventMap mEventMap[SMART_SCRIPT_TYPE_MAX]`, so
downstream (`GetScript`, AI instantiation, reloads) is untouched. `LoomLoader.{h,cpp}` sit in the
SmartScripts source dir and are picked up by the existing source glob. **No SAI code is removed** —
Loom is purely additive and `smart_scripts` remains the fallback (Loom wins per-key when present).

Still TODO: a `loomc check --sql` CI step over `data/sql/updates/**/*.sql`, mirroring `codestyle-sql.py`.

## 5. Migration & the fidelity test

**Coverage (measured against the real base data):** converting the entire
`data/sql/base/db_world/smart_scripts.sql` — **52,768 rows across 14,311 entities** — yields
**0 unmapped opcodes / 0 structural errors** and 79 advisory warnings (0.55%, all cross-document
dead-phase heuristics on per-GUID spawn overrides whose phase is set by their template-entry script).

`export_sai_to_loom.py` converts existing content. Because Loom and SAI lower to the *same*
`SmartScriptHolder` tuples, the migration is verifiable rather than trusted:

```
for each entity:
    holders_sai  = LoadSmartAIFromDB path
    holders_loom = LoomLoader::Compile(export(holders_sai))
    assert holders_loom == holders_sai      # field-by-field
```

This belongs as a Google Test under `src/test/` (`-DBUILD_TESTING=ON`) and gates the content migration
batches. `LoomLoader::Compile` is deliberately pure (no DB, no globals) precisely so this test is trivial.

## 6. Try the toolchain

```bash
cd src/server/game/AI/SmartScripts/loom
python tools/loomc.py check examples/example_boss.loom          # -> "Loom validation passed."
python tools/export_sai_to_loom.py --csv your_smart_scripts.csv # -> Loom YAML on stdout
#   ...or --db HOST USER PASS acore_world, or --out-dir to emit rev_*.sql updates
```

Verified end-to-end in this draft: the example lints clean; a seeded-error doc is rejected with four
precise diagnostics (unknown event, bad param, unknown target, dead phase); and a 4-row SAI CSV
(link-chained) exports to readable Loom that then lints clean.

## 7. Remaining work (honest list)

- **Not built yet.** The runtime is wired in but needs a `make` to confirm it compiles against the
  fkYAML API. This is the one thing between here and a working feature.
- **Slot names for the long tail.** Common opcodes have curated names; the rest are auto-derived from
  the enum comments, with generic `paramN` for undocumented positions (handled losslessly by both the
  loader and exporter). Refining those names is incremental — edit `CURATED` in `gen_opcodes.py` and
  regenerate; it changes readability only, never correctness.
- **Threads/`include:`** (§6 of the design doc) are schema'd (`loom_thread`) but the splicer isn't
  implemented yet — a compile-time expansion in `loomc` + `LoomLoader`.
- **No spell/emote *name* aliases** yet: ids are numeric (they're DB references). Optional name tables
  are an authoring-layer nicety for `loomc`, not a runtime concern.
- **`IsEventValid`/`IsTargetValid`** should be invoked per-holder inside `LoadInto` before injection
  (currently it compiles and injects; hardening adds the existing SAI validators as a backstop).
- **Fidelity Google Test** (§5) still to be written: assert `Compile(export(SAI)) == SAI` holder-for-
  holder across all 14,311 entities. `LoomLoader::Compile` is pure precisely so this is trivial.
