#!/usr/bin/env python3
"""
gen_opcodes — generate the Loom opcode vocabulary from the authoritative SAI enums.

Parses src/server/game/AI/SmartScripts/SmartScriptMgr.h (the SMART_EVENT / SMART_ACTION /
SMARTAI_TARGETS enums, including their inline param-doc comments) and emits BOTH:

  * data/loom/schema/loom_opcodes.json                       (used by loomc + export tools)
  * src/server/game/AI/SmartScripts/LoomOpcodes.gen.cpp      (compiled into the game library)

Generating both from one source guarantees the Python toolchain and the C++ runtime can never
drift, and guarantees full coverage: every real opcode is mapped. Slot names come from the enum
comments where they parse cleanly (curated overrides for the common ones); every remaining raw
param position is still addressable as `paramN`, so migration is lossless even for undocumented
params.

Run from the repo root:  python data/loom/tools/gen_opcodes.py
"""

import json
import os
import re
import sys

def find_root(start):
    """Ascend until the repo root (contains both `src` and `deps`), so this script works from
    wherever the loom/ directory is placed."""
    d = os.path.abspath(start)
    while True:
        if os.path.isdir(os.path.join(d, "src")) and os.path.isdir(os.path.join(d, "deps")):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            raise RuntimeError("could not locate repo root from " + start)
        d = parent


ROOT = find_root(os.path.dirname(__file__))
SS_DIR = os.path.join(ROOT, "src", "server", "game", "AI", "SmartScripts")
HEADER = os.path.join(SS_DIR, "SmartScriptMgr.h")                          # input: the SAI enums
JSON_OUT = os.path.join(ROOT, "data", "loom", "schema", "loom_opcodes.json")  # output: Python schema
CPP_OUT = os.path.join(SS_DIR, "LoomOpcodes.gen.cpp")                      # output: compiled tables

SECTIONS = {
    #  section     enum name          keyword prefix   raw-param width
    "events":  ("SMART_EVENT",     "SMART_EVENT_",  6),
    "actions": ("SMART_ACTION",    "SMART_ACTION_", 6),
    "targets": ("SMARTAI_TARGETS", "SMART_TARGET_", 4),
}

# Opcodes that must not become keywords (structural markers / internal).
# Only the enum's structural markers, NOT legitimate opcodes that happen to end in _END
# (e.g. SMART_EVENT_GAME_EVENT_END is a real opcode and must be kept).
SKIP_EXACT = {"SMART_EVENT_LINK"}
SKIP_SUFFIX = ("_TC_END", "_AC_END", "_TC_START", "_AC_START", "_MAX")

# Comments that are prose, not a param list -> all positions fall back to paramN.
PROSE_MARKERS = (
    "warning", "@todo", "not supported", "placeholder", "don't use", "replaced by",
    "confusing", "lovely", "wipe-safe", "internal usage", "kill credit", "use owner",
    "who caused", "self cast", "current target", "dead last", "any random", "highest aggro",
    "except top", "no params", "no action", "pre-stored", "all players", "all units",
    "tagged this", "vehicle can", "removes heroism", "sends the", "spawnpos",
)

# Nice slot names for the common opcodes (readability). Everything else is auto-named.
CURATED = {
    "events": {
        "update_ic":     ["initial_min", "initial_max", "repeat_min", "repeat_max"],
        "update_ooc":    ["initial_min", "initial_max", "repeat_min", "repeat_max"],
        "update":        ["initial_min", "initial_max", "repeat_min", "repeat_max"],
        "health_pct":    ["hp_min", "hp_max", "repeat_min", "repeat_max"],
        "mana_pct":      ["mana_min", "mana_max", "repeat_min", "repeat_max"],
        "target_health_pct": ["hp_min", "hp_max", "repeat_min", "repeat_max"],
        "spellhit":      ["spell", "school", "cooldown_min", "cooldown_max"],
        "spellhit_target": ["spell", "school", "cooldown_min", "cooldown_max"],
        "receive_emote": ["emote", "cooldown_min", "cooldown_max"],
        "victim_casting": ["repeat_min", "repeat_max", "spell"],
        "gossip_select": ["menu", "option"],
        "data_set":      ["field", "value", "cooldown_min", "cooldown_max"],
        "kill":          ["cooldown_min", "cooldown_max", "player_only", "creature_entry"],
        "range":         ["min_dist", "max_dist", "repeat_min", "repeat_max"],
        "movementinform": ["movement_type", "point", "path"],
    },
    "actions": {
        "talk":            ["group", "duration", "use_invoker", "use_talk_target"],
        "set_faction":     ["faction"],
        "cast":            ["spell", "cast_flags", "trigger_flags", "targets_limit"],
        "invoker_cast":    ["spell", "cast_flags", "trigger_flags", "targets_limit"],
        "self_cast":       ["spell", "cast_flags", "trigger_flags", "targets_limit"],
        "summon_creature": ["creature", "summon_type", "duration", "attack_invoker", "attack_owner", "flags"],
        "play_emote":      ["emote"],
        "set_emote_state": ["emote"],
        "set_event_phase": ["phase"],
        "inc_event_phase": ["value"],
        "force_despawn":   ["delay"],
        "set_data":        ["field", "value"],
        "set_inst_data":   ["field", "value"],
        "move_to_pos":     ["point", "transport", "controlled", "contact_distance"],
        "set_health_regen": ["enabled"],
        "add_aura":        ["spell", "target"],
        "set_sheath":      ["sheath"],
        "create_timed_event": ["id", "initial_min", "initial_max", "repeat_min", "repeat_max", "chance"],
    },
    "targets": {
        "hostile_second_aggro": ["max_dist", "player_only", "power_type", "aura"],
        "hostile_random":       ["max_dist", "player_only", "power_type", "aura"],
        "hostile_last_aggro":   ["max_dist", "player_only", "power_type", "aura"],
        "creature_range":       ["entry", "min_dist", "max_dist", "alive"],
        "creature_guid":        ["guid", "entry"],
        "creature_distance":    ["entry", "max_dist", "alive"],
        "closest_creature":     ["entry", "max_dist", "dead"],
        "closest_player":       ["max_dist"],
        "gameobject_range":     ["entry", "min_dist", "max_dist"],
        "stored":               ["id"],
        "player_range":         ["min_dist", "max_dist", "max_count"],
    },
}

# Extra ergonomic keyword aliases -> canonical keyword.
ALIASES = {
    "events":  {},
    "actions": {"phase": "set_event_phase", "summon": "summon_creature",
                "despawn": "force_despawn", "emote": "play_emote"},
    "targets": {"random": "hostile_random", "invoker": "action_invoker",
                "position": "position", "self": "self", "victim": "victim"},
}


# YAML 1.1 booleans / nulls — unsafe as bare mapping keys.
YAML_RESERVED = {"on", "off", "yes", "no", "true", "false", "y", "n", "null", "none"}


def camel_snake(tok):
    tok = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", tok)   # castFlags -> cast Flags
    tok = re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", " ", tok)  # HPValue -> HP Value
    return "_".join(w.lower() for w in re.split(r"[^A-Za-z0-9]+", tok) if w)


def parse_comment(comment, maxn):
    """Return up to maxn slot names (or None per position) parsed from an enum comment."""
    if not comment:
        return []
    low = comment.strip().lower()
    if not low or low in ("none", "no params", "no action") or low.startswith("none"):
        return []
    if any(m in low for m in PROSE_MARKERS):
        return []
    body = re.sub(r"\([^)]*\)", "", comment)   # strip parentheticals
    out = []
    for raw in body.split(",")[:maxn]:
        raw = re.sub(r"/.*$", "", raw)          # drop "0/1" style notes after slash-ish
        name = camel_snake(raw)
        name = re.sub(r"_?\d+$", "", name)      # emoteid1 -> emoteid
        words = name.split("_")
        if not name or len(words) > 3 or len(name) > 22:
            out.append(None)
        else:
            out.append(name)
    return out


def build_slots(section, keyword, comment, maxn):
    names = [None] * maxn
    for i, t in enumerate(parse_comment(comment, maxn)):
        names[i] = t
    for i, t in enumerate(CURATED[section].get(keyword, [])[:maxn]):
        names[i] = t
    used, out = set(), []
    for i in range(maxn):
        nm = names[i] or f"param{i + 1}"
        # Never emit a YAML 1.1 boolean/null word as a key — PyYAML would read `on:`/`no:` as a
        # bool while fkYAML reads the string, diverging the two layers. Fall back to positional.
        if nm in YAML_RESERVED:
            nm = f"param{i + 1}"
        base, k = nm, 2
        while nm in used:
            nm = f"{base}_{k}"; k += 1
        used.add(nm); out.append(nm)
    # Trim trailing generic `paramN` placeholders — the loader/exporter address those positionally
    # via the `paramN` convention, so they don't need to be listed. Named slots (and any generic
    # holes between named ones) are kept.
    while out and out[-1] == f"param{len(out)}":
        out.pop()
    return out


def parse_enum(text, enum_name):
    m = re.search(r"enum\s+" + re.escape(enum_name) + r"\s*\{(.*?)\n\};", text, re.S)
    if not m:
        sys.exit(f"could not find enum {enum_name}")
    body = m.group(1)
    entries = []
    value = -1
    for line in body.splitlines():
        s = line.strip()
        if not s or s.startswith("//") or s.startswith("/*"):
            continue
        lm = re.match(r"(SMART_[A-Z0-9_]+)\s*(?:=\s*(0x[0-9A-Fa-f]+|-?\d+))?\s*,?\s*(?:///?<?\s*(.*))?$", s)
        if not lm:
            continue
        name, val, comment = lm.group(1), lm.group(2), (lm.group(3) or "").strip()
        value = int(val, 0) if val is not None else value + 1
        entries.append((name, value, comment))
    return entries


def main():
    text = open(HEADER, encoding="utf-8", errors="replace").read()
    schema = {"$generated_by": "data/loom/tools/gen_opcodes.py — do not edit by hand"}

    for section, (enum_name, prefix, maxn) in SECTIONS.items():
        table = {}
        for name, value, comment in parse_enum(text, enum_name):
            if name in SKIP_EXACT or name.endswith(SKIP_SUFFIX):
                continue
            keyword = name[len(prefix):].lower()
            if not keyword:
                continue
            table[keyword] = {"type": value, "params": build_slots(section, keyword, comment, maxn),
                              "desc": comment}
        for alias, canonical in ALIASES[section].items():
            if canonical in table and alias not in table:
                table[alias] = dict(table[canonical], alias_of=canonical)
        schema[section] = table

    schema["event_flags"] = {"once": 1, "debug_only": 128, "while_charmed": 512}
    schema["source_types"] = {"creature": 0, "gameobject": 1, "areatrigger": 2}

    os.makedirs(os.path.dirname(JSON_OUT), exist_ok=True)
    with open(JSON_OUT, "w", encoding="utf-8") as f:
        json.dump(schema, f, indent=2, ensure_ascii=False)
        f.write("\n")

    # ---- C++ ----
    def emit_table(fn, section):
        lines = [f"LoomOpcodeTable const& {fn}()", "{", "    static LoomOpcodeTable const t = {"]
        for kw, d in sorted(schema[section].items(), key=lambda kv: kv[1]["type"]):
            slots = ", ".join(f'"{s}"' for s in d["params"])
            # descriptions live in the JSON schema; keep the C++ compact (<=120 cols), wrapping the
            # slot list onto its own indented line when the single-line form would be too wide.
            line = f'        {{ "{kw}", {{ {d["type"]}, {{ {slots} }} }} }},'
            if len(line) <= 120:
                lines.append(line)
                continue
            # Wrap: opcode header, then slot names packed into <=120-col continuation lines.
            lines.append(f'        {{ "{kw}", {{ {d["type"]}, {{')
            toks = [f'"{s}"' for s in d["params"]]
            cur = "           "
            for t in toks:
                piece = " " + t + ","
                if len(cur) + len(piece) > 116:
                    lines.append(cur)
                    cur = "           "
                cur += piece
            lines.append(cur.rstrip(",") + " } } },")
        lines += ["    };", "    return t;", "}", ""]
        return "\n".join(lines)

    cpp = [
        "/*",
        " * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information",
        " *",
        " * GENERATED by data/loom/tools/gen_opcodes.py from SmartScriptMgr.h — DO NOT EDIT.",
        " * Regenerate with:  python data/loom/tools/gen_opcodes.py",
        " */",
        "",
        '#include "LoomOpcodes.h"',
        "",
        emit_table("LoomEvents", "events"),
        emit_table("LoomActions", "actions"),
        emit_table("LoomTargets", "targets"),
    ]
    with open(CPP_OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(cpp))

    print(f"events:  {len(schema['events'])} keywords")
    print(f"actions: {len(schema['actions'])} keywords")
    print(f"targets: {len(schema['targets'])} keywords")
    print(f"wrote {JSON_OUT}")
    print(f"wrote {CPP_OUT}")


if __name__ == "__main__":
    main()
