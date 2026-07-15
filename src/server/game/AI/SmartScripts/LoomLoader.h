/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * Loom — readable, composable creature/gameobject scripting.
 * See docs/Loom.md for the design and data/loom/README.md for the toolchain.
 *
 * LoomLoader parses Loom documents (YAML) from `creature_loom` and compiles them into the exact
 * same SmartScriptHolder structures SAI produces, then injects them into SmartAIMgr's event map.
 * The SmartAI execution engine is unchanged: Loom is a front-end, not a new runtime.
 */

#ifndef ACORE_LOOMLOADER_H
#define ACORE_LOOMLOADER_H

#include "SmartScriptMgr.h"
#include <string>
#include <vector>

// Result of compiling one document: the holders to inject, plus any author-facing diagnostics.
struct LoomCompileResult
{
    std::vector<SmartScriptHolder> holders;
    std::vector<std::string>       errors;   // non-empty => document rejected, nothing injected
    [[nodiscard]] bool ok() const { return errors.empty(); }
};

// A reusable behavior fragment loaded from a `<content root>/threads/*.loom` file. `body` is a YAML
// `events:` fragment that may reference `{{ param }}` placeholders substituted at include time.
struct LoomThread
{
    std::vector<std::string> params;
    std::string              body;
};
using LoomThreadMap = std::unordered_map<std::string, LoomThread>;

class LoomLoader
{
public:
    // Called from SmartAIMgr::LoadSmartAIFromDB() AFTER the smart_scripts pass, so Loom entries
    // take precedence over any legacy SAI rows for the same (entryOrGuid, source_type).
    // Returns the number of entities loaded from `creature_loom`. `eventMap` points at the
    // SmartAIEventMap[SMART_SCRIPT_TYPE_MAX] array owned by SmartAIMgr.
    static uint32 LoadInto(SmartAIEventMap* eventMap);

    // Compile a single document. Pure/testable: no DB, no globals touched. Used by unit tests and
    // by the fidelity harness that compares Loom output against SAI output field-for-field.
    // `threads` (optional) resolves the document's `include:` entries.
    static LoomCompileResult Compile(int32 entryOrGuid, SmartScriptType sourceType,
                                     std::string const& yaml, LoomThreadMap const* threads = nullptr);
};

#endif // ACORE_LOOMLOADER_H
