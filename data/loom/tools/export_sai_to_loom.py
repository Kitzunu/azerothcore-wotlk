#!/usr/bin/env python3
"""
export_sai_to_loom — migrate existing SAI content into Loom documents.

Reads `smart_scripts` rows, groups them by (entryorguid, source_type), reconstructs the link-chains
into `do:` lists, maps numeric types back to Loom keywords via the shared schema, and emits Loom YAML.

Because both SAI and Loom describe the *same* event/action/target tuples, this round-trips through
the identical engine vocabulary — so the migration is mechanical and fidelity-testable (compile the
emitted Loom, compile the original SAI, assert the SmartScriptHolder vectors are byte-identical).

Input sources (pick one):
    --db  host user pass dbname        read live acore_world
    --csv rows.csv                     smart_scripts dumped as CSV with a header row

Output: one YAML document per entity on stdout (or --out-dir to write rev_*.sql updates).
"""

import argparse
import csv
import json
import os
import sys

try:
    import yaml
except ImportError:
    sys.exit("export requires PyYAML (pip install pyyaml)")

SCHEMA_PATH = os.path.join(os.path.dirname(__file__), "..", "schema", "loom_opcodes.json")

# smart_scripts column order (matches WORLD_SEL_SMART_SCRIPTS / LoadSmartAIFromDB).
COLS = ["entryorguid", "source_type", "id", "link",
        "event_type", "event_phase_mask", "event_chance", "event_flags",
        "event_param1", "event_param2", "event_param3", "event_param4", "event_param5", "event_param6",
        "action_type",
        "action_param1", "action_param2", "action_param3", "action_param4", "action_param5", "action_param6",
        "target_type", "target_param1", "target_param2", "target_param3", "target_param4",
        "target_x", "target_y", "target_z", "target_o", "comment"]

SMART_EVENT_LINK = 61
SMART_ACTION_SET_EVENT_PHASE = 22


def build_reverse(schema, section):
    """type-int -> (keyword, [slot names]). Canonical keywords only; aliases are skipped so the
    reverse mapping is deterministic."""
    return {defn["type"]: (kw, defn["params"])
            for kw, defn in schema[section].items()
            if isinstance(defn, dict) and "type" in defn and "alias_of" not in defn}


def phase_labels_from_mask(mask):
    """event_phase_mask bits -> ['phase_1', 'phase_3', ...] (renamable by the author later)."""
    return [f"phase_{bit + 1}" for bit in range(12) if mask & (1 << bit)]


class Exporter:
    def __init__(self, schema):
        self.ev = build_reverse(schema, "events")
        self.ac = build_reverse(schema, "actions")
        self.tg = build_reverse(schema, "targets")

    def slots_to_map(self, reverse_entry, params):
        """Build {slot_name: value}, dropping zeros. Positions past the schema's named slots use the
        generic `paramN` convention so no nonzero raw param is ever lost."""
        _, slot_names = reverse_entry
        out = {}
        for i, value in enumerate(params):
            if not value:
                continue
            name = slot_names[i] if i < len(slot_names) and slot_names[i] else f"param{i + 1}"
            out[name] = value
        return out

    def action_node(self, row):
        atype = row["action_type"]
        kw, _ = self.ac.get(atype, (f"action_{atype}", []))
        aparams = [row[f"action_param{i}"] for i in range(1, 7)]

        if atype == SMART_ACTION_SET_EVENT_PHASE:
            return {"phase": f"phase_{aparams[0]}"}   # numeric phase -> label

        value = self.slots_to_map(self.ac.get(atype, (kw, [])), aparams)

        # target
        ttype = row["target_type"]
        tkw, _ = self.tg.get(ttype, (f"target_{ttype}", []))
        tparams = [row[f"target_param{i}"] for i in range(1, 5)]
        tmap = self.slots_to_map(self.tg.get(ttype, (tkw, [])), tparams)
        if tkw == "position":
            value["at"] = {"position": {k: row[f"target_{k}"] for k in "xyzo"}}
        elif tmap:
            value["at"] = {tkw: tmap}
        elif tkw not in ("none",):
            value["at"] = tkw

        return {kw: value if value else None}

    def export_entity(self, rows):
        """rows: all smart_scripts rows for one (entryorguid, source_type), keyed by id."""
        by_id = {r["id"]: r for r in rows}
        consumed = set()
        events = []

        for r in sorted(rows, key=lambda x: x["id"]):
            if r["id"] in consumed or r["event_type"] == SMART_EVENT_LINK:
                continue

            # Walk the link chain to gather the do-list.
            chain, cur = [], r
            while cur is not None:
                chain.append(cur)
                consumed.add(cur["id"])
                cur = by_id.get(cur["link"]) if cur["link"] else None

            evkw, slot_names = self.ev.get(r["event_type"], (f"event_{r['event_type']}", []))
            node = {"event": evkw}
            eparams = [r[f"event_param{i}"] for i in range(1, 7)]
            for i, nm in enumerate(slot_names):
                if nm and eparams[i]:
                    node[nm] = eparams[i]
            if r["event_chance"] and r["event_chance"] != 100:
                node["chance"] = r["event_chance"]
            if r["event_flags"] & 1:
                node["once"] = True
            phases = phase_labels_from_mask(r["event_phase_mask"])
            if phases:
                node["in_phase"] = phases
            node["do"] = [self.action_node(c) for c in chain]
            events.append(node)

        return {"events": events}


def rows_from_csv(path):
    with open(path, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            yield {k: (int(row[k]) if k not in ("comment", "target_x", "target_y", "target_z", "target_o")
                       else row[k]) for k in COLS}


def rows_from_db(host, user, pw, db):
    import pymysql  # optional dependency, only needed for --db
    conn = pymysql.connect(host=host, user=user, password=pw, database=db, cursorclass=pymysql.cursors.DictCursor)
    with conn.cursor() as cur:
        cur.execute(f"SELECT {', '.join(COLS)} FROM smart_scripts ORDER BY entryorguid, source_type, id")
        yield from cur.fetchall()


def main():
    ap = argparse.ArgumentParser(prog="export_sai_to_loom")
    ap.add_argument("--csv")
    ap.add_argument("--db", nargs=4, metavar=("HOST", "USER", "PASS", "DB"))
    ap.add_argument("--out-dir", help="write rev_*.sql updates here instead of YAML to stdout")
    args = ap.parse_args()

    with open(SCHEMA_PATH, encoding="utf-8") as f:
        schema = json.load(f)
    exporter = Exporter(schema)

    rows = list(rows_from_csv(args.csv)) if args.csv else list(rows_from_db(*args.db))

    # group by entity
    entities = {}
    for r in rows:
        entities.setdefault((r["entryorguid"], r["source_type"]), []).append(r)

    for (eog, stype), erows in sorted(entities.items()):
        doc = exporter.export_entity(erows)
        body = yaml.safe_dump(doc, sort_keys=False, default_flow_style=False, allow_unicode=True)
        if args.out_dir:
            esc = body.replace("'", "\\'")
            sql = (f"DELETE FROM `creature_loom` WHERE `entryorguid`={eog} AND `source_type`={stype};\n"
                   f"INSERT INTO `creature_loom` (`entryorguid`,`source_type`,`script`) VALUES\n"
                   f"({eog}, {stype}, '{esc}');\n")
            os.makedirs(args.out_dir, exist_ok=True)
            with open(os.path.join(args.out_dir, f"loom_{eog}_{stype}.sql"), "w", encoding="utf-8") as f:
                f.write(sql)
        else:
            print(f"# entity {eog} (source_type {stype})")
            print(body)


if __name__ == "__main__":
    main()
