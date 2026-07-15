# Loom — a readable, composable creature & gameobject scripting format

> **Loom** replaces the `smart_scripts` (SAI) serialization with a named, self-describing,
> composable document format. Behaviors are woven from reusable **threads**.
> The execution engine is unchanged — Loom only replaces how scripts are *authored, stored, and reviewed*.

- **Status:** Draft / proposal
- **Scope:** Creatures, gameobjects, areatriggers (everything `source_type` covers in SAI today)
- **Non-goal:** Replacing C++ `CreatureScript` for genuinely complex bosses, or replacing Eluna.

---

## 1. Why (the problem in one screen)

A `smart_scripts` row is a hand-serialized tagged union flattened into a wide table:

```
entryorguid, source_type, id, link,
event_type, event_phase_mask, event_chance, event_flags, event_param1..4,
action_type, action_param1..6,
target_type, target_param1..4, x, y, z, o, comment
```

The *engine* (event → condition → action → target) is fine. The **encoding** is the problem:

| SAI pain | Consequence |
|---|---|
| Positional magic integers (`action_param3`) | Cannot read a row without an enum table open |
| Fixed arity (4 event / 6 action params) | Rich data gets bit-packed or spilled into `param_string` |
| Sequencing via `link` + `event_phase_mask` bitmasks | Control flow is unreadable and fragile to reorder |
| No composition | Every creature re-encodes the same patterns from scratch |
| Opaque diffs (`UPDATE ... SET action_param3=17`) | PR reviewers can't see intent |
| Validation only at server load | Errors are runtime log spam, not author-time failures |

**Loom's thesis:** keep the proven event/action/target engine, replace the serialization with something
**named, self-describing, composable, and diffable**, while staying DB-distributed and idempotent via SQL updates.

---

## 2. Design goals

1. **Legible.** A script reads like intent, not like a register dump.
2. **DB-native & SQL-distributed.** Ships through the existing `pending_db_world` update workflow; idempotent `DELETE`+`INSERT`.
3. **Engine reuse.** Compiles down to the *same* in-memory structures the SAI engine already executes. No new runtime.
4. **Author-time validation.** A PR with an unknown spell id or a dangling phase fails CI, not the worldserver log.
5. **Composable.** Shared behaviors (interrupt-on-cast, flee-at-low-health, standard patrol) are named, reviewed-once **threads**.
6. **Non-breaking migration.** SAI and Loom run side by side; content moves entry-by-entry with a mechanical exporter.

### Non-goals

- Not a general programming language. No arbitrary loops/recursion authored by content designers — that's what C++/Eluna are for.
- Not a new *engine*. v1 is a 1:1 re-encoding of today's SAI vocabulary; new expressiveness lands only after parity is proven.

---

## 3. Architecture

Loom is a **front-end**. It slots in ahead of the existing SmartAI runtime:

```
   ┌─────────────┐   ┌──────────┐   ┌───────────┐   ┌──────────────────────┐
   │ .loom doc   │──▶│  Parser  │──▶│ Compiler  │──▶│ SmartScriptHolder[]  │──▶ existing SmartAI engine
   │ (in DB)     │   │ (schema  │   │ (resolve  │   │ (unchanged in-memory │     (unchanged)
   │             │   │  check)  │   │  names →  │   │  action structs)     │
   └─────────────┘   └──────────┘   │  ids/enums│   └──────────────────────┘
                                    └───────────┘
```

- **Parser** — reads the document (YAML/JSON), validates against the Loom JSON Schema.
- **Compiler** — resolves symbolic names (`SPELL_FIREBALL`, `target: victim`, phase labels) into the numeric ids/enums
  the engine expects; lowers lexical sequencing and `every: [min,max]` timers into the existing event/action tuples.
- **Runtime** — **unchanged**. The engine keeps executing `SmartScriptHolder` structures exactly as it does for SAI.

> This is the key risk-reduction move: Loom is a *serialization + toolchain* change, not a runtime rewrite.
> Every existing boss the engine handles today keeps working the moment its script is re-encoded.

---

## 4. Storage & DB model

One document per scripted entity. Store the whole script as a single reviewable blob.

```sql
CREATE TABLE `creature_loom` (
    `entryorguid`  INT          NOT NULL,
    `source_type`  TINYINT      NOT NULL DEFAULT 0,  -- 0 creature, 1 gameobject, 2 areatrigger (same as SAI)
    `script`       JSON         NOT NULL,            -- the Loom document
    `loom_version` SMALLINT     NOT NULL DEFAULT 1,
    `comment`      VARCHAR(255) NOT NULL DEFAULT '',
    PRIMARY KEY (`entryorguid`, `source_type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

- **`JSON` column** gives MySQL-side validation and queryability (`WHERE JSON_CONTAINS(script->'$.uses_spells', '133')`).
- **Idempotency unchanged:** an SQL update is `DELETE FROM creature_loom WHERE entryorguid=… AND source_type=…;` then one `INSERT`.
  The diff is now the *entire readable script*, not a scatter of param columns.
- Documents are authored in YAML for humans; the SQL update stores canonical JSON. A `apps/codestyle` formatter converts
  YAML ↔ JSON so reviewers read YAML in PRs and the DB stores JSON.

---

## 5. The Loom document

A document has an optional header, a list of **event blocks**, and optional named **phases**.

### 5.1 Anatomy

```yaml
entity: 12345            # entryorguid (or a guid with source note)
name: "Example Boss"     # comment, human-only

uses:                    # optional symbol table — makes the body read cleanly
  spells:
    FIREBALL:  133
    CLEAVE:    15496
    ENRAGE:    8599

on aggro:
  - say: 0                                   # text group id from creature_text
  - cast: FIREBALL @ victim

on health_pct below 30 once:
  - phase: enrage

phase enrage:
  - cast: ENRAGE @ self
  - every [6s, 9s]:                          # repeating timed action
      cast: CLEAVE @ victim
```

### 5.2 Events

Same vocabulary as `SMART_EVENT_*`, expressed by name. Each event block is `on <event> [modifiers]:` followed by an ordered action list.

| Loom | SAI equivalent |
|---|---|
| `on aggro:` | `SMART_EVENT_AGGRO` |
| `on spawn:` | `SMART_EVENT_JUST_SUMMONED` / `RESPAWN` |
| `on death:` | `SMART_EVENT_DEATH` |
| `on health_pct below 30 once:` | `SMART_EVENT_HEALTH_PCT`, param once-flag |
| `on spell_hit SHADOW_BOLT:` | `SMART_EVENT_SPELLHIT` |
| `on gossip_select option 1:` | `SMART_EVENT_GOSSIP_SELECT` |
| `on receive_emote 21:` | `SMART_EVENT_RECEIVE_EMOTE` |

Event modifiers replace the opaque numeric columns:

- `chance 40%` → `event_chance`
- `once` / `every [min,max]` → repeat flags + timer params (no more four-column timer packing)
- `in phase enrage,burn` → `event_phase_mask` (by **label**, not bitmask)
- `flags: [debug_only]` → `event_flags`

### 5.3 Actions

Same vocabulary as `SMART_ACTION_*`, with named params. Order in the list *is* the sequence (replaces `link` chaining).

```yaml
- say: 2
- cast: FIREBALL @ victim
- cast: HEAL @ self flags [triggered, interrupt_previous]
- morph: 17                     # set_display / morph
- move_to: waypoint 3
- summon: 12346 @ position(x, y, z, o) for 30s
- set_data: { field: 1, value: 0 }
- emote: 5
- despawn: after 3s
- call thread: flee_at_15pct    # invoke a reusable thread (see §6)
```

### 5.4 Targets — `@ <target>`

Targets are named instead of `target_type` + four params:

| Loom | SAI target |
|---|---|
| `@ self` | `SMART_TARGET_SELF` |
| `@ victim` | `SMART_TARGET_VICTIM` |
| `@ random_player range 40` | hostile-random variants |
| `@ friendly missing_hp 5000` | `SMART_TARGET_CLOSEST_FRIENDLY` etc. |
| `@ creature 12346 range 20` | `SMART_TARGET_CLOSEST_CREATURE` |
| `@ position(x, y, z, o)` | stored coords |
| `@ stored 1` | `SMART_TARGET_STORED` (target registers, see §5.6) |

### 5.5 Conditions

Loom reuses the existing `conditions` table rather than reinventing it — a condition guard is just a reference:

```yaml
on aggro:
  when: conditions group 5     # existing conditions table, SourceGroup 5
  - cast: FIREBALL @ victim
```

For simple inline guards, common conditions are sugar:

```yaml
on update_ic:
  when: [ target.hp_pct < 20, self.mana_pct > 30 ]
  - cast: EXECUTE @ victim
```

### 5.6 Phases & stored targets

- **Phases** are labels, declared with `phase <name>:` blocks and switched with `- phase: <name>`.
  The compiler assigns bit indices; authors never see the bitmask.
- **Stored targets** keep SAI's target-register concept but named:
  `- store as add: @ creature 12346 range 40` then later `@ stored add`.

---

## 6. Threads — the composition win SAI can't do

A **thread** is a named, reusable behavior fragment. This is the single biggest ergonomic gain over SAI.

```yaml
# threads/caster_interrupt_on_channel.loom
thread caster_interrupt_on_channel:
  params: [interrupt_spell]
  on target_casting:
    - cast: {{ interrupt_spell }} @ victim flags [interrupt_previous]
```

Used by any creature:

```yaml
entity: 12345
uses: { spells: { KICK: 1766, FIREBALL: 133 } }

include: caster_interrupt_on_channel(interrupt_spell = KICK)

on aggro:
  - cast: FIREBALL @ victim
```

- `include` splices a thread's events into the entity at compile time (zero runtime cost — it lowers to plain rows).
- Ships a **standard library** of threads: `flee_at_pct`, `patrol_waypoints`, `enrage_timer`, `summon_adds_on_pct`,
  `interrupt_on_channel`, `call_for_help`. Reviewed once, reused everywhere.
- Threads are stored in their own table (`loom_thread`) and versioned; entities reference them by name+version.

---

## 7. Validation & tooling

Move failures from **runtime** to **author time**.

- **`apps/codestyle/codestyle-loom.py`** (CI gate, mirrors `codestyle-sql.py`):
  - Schema-validate every document.
  - Resolve every symbol: unknown spell id, missing `creature_text` group, dangling phase label, unknown thread → **fail the PR**.
  - Idempotency check: every `INSERT` into `creature_loom` has a matching `DELETE`.
- **`loomc`** — a standalone compiler/linter (C++ or Python) usable locally and in CI: `loomc check path/to/script.loom`.
- **Worldserver load** still validates as a backstop and logs, but green CI means it should never fail there.
- **Editor support** (stretch): a JSON Schema published so VS Code gives autocomplete + inline errors on `.loom` files.

---

## 8. Migration from SAI

Migration is **mechanical and non-breaking** because Loom and SAI describe the *same* event/action/target tuples.

### 8.1 Dual-run (no flag day)

The `SmartAIMgr` load path checks `creature_loom` **first**, falls back to `smart_scripts`:

```
load entity 12345:
    if creature_loom has (entryorguid, source_type):  compile Loom → engine
    else if smart_scripts has it:                      load SAI rows → engine   (unchanged)
```

Both compile to identical in-memory structures, so a half-migrated DB runs fine. Content moves entry-by-entry.

### 8.2 The exporter (`smart_scripts` → Loom)

A one-time tool reads existing SAI rows and emits Loom documents:

1. Group rows by `(entryorguid, source_type)`.
2. Map each `event_type`/`action_type`/`target_type` back to its Loom keyword via the shared enum tables.
3. Reconstruct sequencing: `link` chains → ordered action lists; `event_phase_mask` bits → named phases (`phase_1`, … renamable later).
4. Reverse-resolve ids to symbols where a name table exists (spells, emotes), else keep the raw id with a `# TODO name` comment.
5. Emit YAML + write the SQL update that inserts the JSON.

Because it round-trips through the same engine vocabulary, the exporter can be **fidelity-tested**: compile the exported Loom,
compile the original SAI, assert the resulting `SmartScriptHolder` vectors are byte-identical. That gives a provable, safe migration.

### 8.3 Phased rollout

| Milestone | Deliverable | Risk |
|---|---|---|
| **M0 — Schema** | Loom JSON Schema covering SAI's full event/action/target vocabulary 1:1. No new features. | none (design only) |
| **M1 — Compiler** | `loomc` + loader that compiles docs into existing engine structs. Dual-run wiring. | low (additive) |
| **M2 — Exporter + fidelity test** | `smart_scripts` → Loom, with holder-equality assertion. | low (verifiable) |
| **M3 — CI validator** | `codestyle-loom.py` gate. | none |
| **M4 — Content migration** | Convert content in batches (per zone/instance), reviewing readable diffs. SAI stays as fallback. | contained (per-batch) |
| **M5 — Ergonomics** | Threads/std-lib, named timers, inline conditions, editor schema. | additive |
| **M6 — Sunset (optional, later)** | Once coverage is high, mark `smart_scripts` legacy; keep the loader for third-party DBs. | deferred |

No step forces a big-bang rewrite, and every step is independently revertible.

---

## 9. Worked example — SAI vs Loom

**Behavior:** On aggro, yell and cast Fireball at the tank. At 30% HP (once), enter an enrage phase that casts Enrage on self
and Cleaves the tank every 6–9s.

### SAI (today)

```sql
-- 5 rows, magic numbers, link/phase chaining, unreadable diff
INSERT INTO smart_scripts (entryorguid,source_type,id,link,event_type,event_phase_mask,event_chance,event_flags,
  event_param1,event_param2,event_param3,event_param4,action_type,action_param1,action_param2,action_param3,
  action_param4,action_param5,action_param6,target_type,target_param1,target_param2,target_param3,x,y,z,o,comment) VALUES
(12345,0,0,0,4,0,100,0, 0,0,0,0, 1,0,0,0,0,0,0, 2,0,0,0, 0,0,0,0,'Boss - On Aggro - Say 0'),
(12345,0,1,0,4,0,100,0, 0,0,0,0, 11,133,0,0,0,0,0, 2,0,0,0, 0,0,0,0,'Boss - On Aggro - Cast Fireball'),
(12345,0,2,0,2,0,100,0, 0,30,0,0, 22,2,0,0,0,0,0, 1,0,0,0, 0,0,0,0,'Boss - At 30% HP - Set Phase 2'),
(12345,0,3,0,61,4,100,0, 0,0,0,0, 11,8599,0,0,0,0,0, 1,0,0,0, 0,0,0,0,'Boss - Phase 2 - Cast Enrage'),
(12345,0,4,0,0,4,100,0, 6000,9000,6000,9000, 11,15496,0,0,0,0,0, 2,0,0,0, 0,0,0,0,'Boss - Phase 2 - Cleave');
```

### Loom

```yaml
entity: 12345
name: "Example Boss"
uses:
  spells: { FIREBALL: 133, ENRAGE: 8599, CLEAVE: 15496 }

on aggro:
  - say: 0
  - cast: FIREBALL @ victim

on health_pct below 30 once:
  - phase: enrage

phase enrage:
  - cast: ENRAGE @ self
  - every [6s, 9s]:
      cast: CLEAVE @ victim
```

Same runtime behavior. The intent is now obvious, the diff is reviewable, the spells are named, the timer is `[6s,9s]`
instead of four columns, and the phase is `enrage` instead of bit `4`.

---

## 10. Open questions

- **YAML vs JSON as author format.** YAML reads best; JSON validates/stores best. Proposal: author YAML, store JSON, PRs show YAML.
- **Guid-based scripts.** Negative `entryorguid` (per-spawn) works identically; needs a naming convention in `comment`.
- **Waypoint data.** Keep using `waypoints`/`waypoint_data` tables and reference by id, or inline short paths in the doc? Lean: reference by id.
- **Third-party DBs.** Many downstream projects have huge `smart_scripts` sets — the SAI loader must remain indefinitely as a fallback.
- **Performance.** Compilation is load-time only; runtime is unchanged. Cache compiled holders exactly as SAI does today.

---

## 11. TL;DR

Loom keeps AzerothCore's proven event→action→target engine and throws away only the unreadable serialization.
Scripts become named, composable YAML/JSON documents stored in the DB and shipped as SQL updates. Migration is a mechanical,
fidelity-tested export that runs alongside SAI with no flag day. The payoff: readable scripts, reviewable diffs,
author-time validation, and reusable behavior threads — the four things SAI structurally cannot provide.
