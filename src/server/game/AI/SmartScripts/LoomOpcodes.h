/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * Loom opcode vocabulary — keyword -> (SAI type, named param slots).
 *
 * The tables are defined in LoomOpcodes.gen.cpp, which is GENERATED from SmartScriptMgr.h by
 * data/loom/tools/gen_opcodes.py. Do not hand-edit the generated tables; regenerate instead so the
 * C++ runtime and the Python toolchain (data/loom/schema/loom_opcodes.json) never drift.
 */

#ifndef ACORE_LOOMOPCODES_H
#define ACORE_LOOMOPCODES_H

#include "Define.h"
#include <string>
#include <unordered_map>
#include <vector>

struct LoomOpcodeDef
{
    uint32 type;
    // slots[i] names raw.param(i+1); always full-width (6 for events/actions, 4 for targets) so
    // every raw param is addressable and migration is lossless.
    std::vector<std::string> slots;
};

using LoomOpcodeTable = std::unordered_map<std::string, LoomOpcodeDef>;

LoomOpcodeTable const& LoomEvents();
LoomOpcodeTable const& LoomActions();
LoomOpcodeTable const& LoomTargets();

#endif // ACORE_LOOMOPCODES_H
