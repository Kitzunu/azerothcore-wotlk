#!/usr/bin/env python3
"""
loomc — Loom validator / linter.

Author-time safety: resolves every Loom document against the shared opcode schema and fails on
unknown opcodes, bad param names, dangling phase labels, or malformed structure. This is the CI
gate (mirror of apps/codestyle/codestyle-sql.py) that turns "worldserver log spam at runtime"
into "red X on the PR".

Usage:
    loomc.py check <file.loom | file.yaml ...>
    loomc.py check --sql data/sql/updates/pending_db_world/*.sql   # extract & lint creature_loom inserts

Exit code 0 = all good, 1 = at least one document rejected.
"""

import argparse
import json
import os
import re
import sys

try:
    import yaml
except ImportError:
    sys.exit("loomc requires PyYAML (pip install pyyaml)")

SCHEMA_PATH = os.path.join(os.path.dirname(__file__), "..", "schema", "loom_opcodes.json")


def _find_root(start):
    d = os.path.abspath(start)
    while True:
        if os.path.isdir(os.path.join(d, "src")) and os.path.isdir(os.path.join(d, "deps")):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            return os.path.abspath(start)
        d = parent


# Deployable threads live with the rest of the Loom content, under data/loom/threads.
THREADS_DIR = os.path.join(_find_root(os.path.dirname(__file__)), "data", "loom", "threads")


def load_schema():
    with open(SCHEMA_PATH, encoding="utf-8") as f:
        return json.load(f)


def load_threads(dirpath=THREADS_DIR):
    """Load reusable thread fragments (loom/threads/*.loom) into {name: {params, body}}."""
    threads = {}
    if not os.path.isdir(dirpath):
        return threads
    for fn in sorted(os.listdir(dirpath)):
        if not fn.endswith(".loom"):
            continue
        with open(os.path.join(dirpath, fn), encoding="utf-8") as f:
            d = yaml.safe_load(f)
        if isinstance(d, dict) and "thread" in d:
            threads[d["thread"]] = {"params": d.get("params") or [], "body": d.get("body", "")}
    return threads


class Linter:
    def __init__(self, schema, threads=None):
        self.schema = schema
        self.threads = threads or {}
        self.errors = []
        self.warnings = []

    def err(self, where, msg):
        self.errors.append(f"{where}: {msg}")

    def warn(self, where, msg):
        self.warnings.append(f"{where}: {msg}")

    def expand_include(self, where, inc):
        """Resolve one `include:` entry to the thread's events, substituting `{{ param }}`."""
        if isinstance(inc, str):
            tname, args = inc, {}
        elif isinstance(inc, dict) and len(inc) == 1:
            (tname, args), = inc.items()
            args = args or {}
        else:
            self.err(where, "include entry must be a thread name or single-key mapping")
            return []

        thread = self.threads.get(tname)
        if not thread:
            self.err(where, f"unknown thread `{tname}`")
            return []

        extra = set(args) - set(thread["params"])
        if extra:
            self.err(where, f"thread `{tname}` got unknown arg(s) {sorted(extra)}")

        body = thread["body"]
        for p, v in args.items():
            body = re.sub(r"\{\{\s*" + re.escape(str(p)) + r"\s*\}\}", str(v), body)
        leftover = re.findall(r"\{\{\s*(\w+)\s*\}\}", body)
        if leftover:
            self.err(where, f"thread `{tname}` missing arg(s) for {sorted(set(leftover))}")
            return []

        try:
            parsed = yaml.safe_load(body)
        except yaml.YAMLError as e:
            self.err(where, f"thread `{tname}` body parse error: {e}")
            return []
        return (parsed.get("events") if isinstance(parsed, dict) else None) or []

    def check_document(self, name, doc):
        if not isinstance(doc, dict):
            self.err(name, "document must be a mapping")
            return

        events = []
        for i, inc in enumerate(doc.get("include", []) or []):
            events += self.expand_include(f"{name} include[{i}]", inc)

        own = doc.get("events")
        if own is not None:
            if not isinstance(own, list):
                self.err(name, "`events:` must be a list")
                return
            events += own

        if "include" not in doc and own is None:
            self.err(name, "document has no `events:` or `include:`")
            return

        state = {"defined": set(), "used": set(), "mutators": False}

        for i, ev in enumerate(events):
            where = f"{name} events[{i}]"
            self.check_event(where, ev, state)

        # "Dead phase" heuristic (WARNING, not error): a phase referenced by `in_phase:` that this
        # document never enters. It's only advisory because a phase can be set by set_event_phase,
        # inc_event_phase, random_phase(_range), or by a *different* script — notably a creature's
        # template-entry script setting the phase for its per-GUID spawn overrides. A hand-authored
        # script that trips this usually has a real bug; migrated content legitimately may not.
        if not state["mutators"]:
            for p in state["used"] - state["defined"]:
                self.warn(name, f"phase '{p}' is used in `in_phase:` but this document never changes phase")

    def check_event(self, where, ev, state):
        # `event:` is the trigger key (not `on:` — `on` is a YAML 1.1 boolean and would be read
        # as `True` here while fkYAML reads the string "on"; the two layers must never diverge).
        if not isinstance(ev, dict) or "event" not in ev:
            self.err(where, "event needs an `event:` key")
            return
        on = ev["event"]
        if on not in self.schema["events"]:
            self.err(where, f"unknown event `event: {on}`")
        if "do" not in ev or not isinstance(ev["do"], list) or not ev["do"]:
            self.err(where, "event needs a non-empty `do:` list")
            return

        for p in ev.get("in_phase", []) or []:
            state["used"].add(p)

        for j, action in enumerate(ev["do"]):
            self.check_action(f"{where} do[{j}]", action, state)

    # Actions that change the active phase in any way.
    PHASE_MUTATORS = {"phase", "set_event_phase", "inc_phase", "inc_event_phase",
                      "random_phase", "random_phase_range"}

    def check_action(self, where, action, state):
        if not isinstance(action, dict) or len(action) != 1:
            self.err(where, "action must be a single-key mapping, e.g. `- cast: {...}`")
            return
        (op, value), = action.items()
        if op not in self.schema["actions"]:
            self.err(where, f"unknown action `{op}`")
            return

        if op in self.PHASE_MUTATORS:
            state["mutators"] = True

        if op == "phase":
            if not isinstance(value, str):
                self.err(where, "`phase:` value must be a label string")
            else:
                state["defined"].add(value)
            return

        # Validate param key names against the schema's slot list (+ the universal `at:` target key
        # and the generic `paramN` convention for positions the schema does not name).
        if isinstance(value, dict):
            allowed = set(self.schema["actions"][op]["params"]) | {"at"}
            for key in value:
                if key not in allowed and not re.fullmatch(r"param[1-6]", str(key)):
                    self.err(where, f"`{op}` has no parameter `{key}` (allowed: {sorted(allowed)})")
            if "at" in value:
                self.check_target(where, value["at"])

    def check_target(self, where, at):
        name = at if isinstance(at, str) else (next(iter(at)) if isinstance(at, dict) and at else None)
        if name is None:
            self.err(where, "`at:` must be a target keyword or single-key mapping")
            return
        if name not in self.schema["targets"]:
            self.err(where, f"unknown target `at: {name}`")


def extract_sql_inserts(sql_text):
    """Yield (comment, yaml_body) for each creature_loom INSERT, and check DELETE-before-INSERT."""
    has_delete = "DELETE FROM `creature_loom`" in sql_text or "DELETE FROM creature_loom" in sql_text
    has_insert = re.search(r"INSERT\s+INTO\s+`?creature_loom`?", sql_text, re.I)
    if has_insert and not has_delete:
        yield ("<sql>", None, "INSERT into creature_loom without a matching DELETE (idempotency)")
        return
    # Pull the YAML blob out of each VALUES(...) tuple (script is the 3rd column).
    for m in re.finditer(r"INSERT\s+INTO\s+`?creature_loom`?[^)]*\)\s*VALUES\s*(.+?);", sql_text, re.I | re.S):
        for tup in re.finditer(r"\(\s*(-?\d+)\s*,\s*(\d+)\s*,\s*'(.*?)'\s*,", m.group(1), re.S):
            body = tup.group(3).replace("\\'", "'").replace("\\n", "\n")
            yield (f"creature_loom {tup.group(1)}", body, None)


def main():
    ap = argparse.ArgumentParser(prog="loomc")
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("files", nargs="+")
    c.add_argument("--sql", action="store_true", help="inputs are .sql files with creature_loom inserts")
    args = ap.parse_args()

    schema = load_schema()
    linter = Linter(schema, load_threads())

    for path in args.files:
        with open(path, encoding="utf-8") as f:
            text = f.read()
        if args.sql:
            for name, body, hard_err in extract_sql_inserts(text):
                if hard_err:
                    linter.err(path, hard_err)
                elif body is not None:
                    try:
                        linter.check_document(f"{path} [{name}]", yaml.safe_load(body))
                    except yaml.YAMLError as e:
                        linter.err(path, f"{name}: YAML parse error: {e}")
        else:
            try:
                linter.check_document(path, yaml.safe_load(text))
            except yaml.YAMLError as e:
                linter.err(path, f"YAML parse error: {e}")

    if linter.warnings:
        print(f"Loom validation: {len(linter.warnings)} warning(s):", file=sys.stderr)
        for w in linter.warnings:
            print(f"  ! {w}", file=sys.stderr)

    if linter.errors:
        print("Loom validation FAILED:", file=sys.stderr)
        for e in linter.errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print("Loom validation passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
