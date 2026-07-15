/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * Loom compiler/loader. See LoomLoader.h and docs/Loom.md.
 *
 * The keyword->opcode tables live in LoomOpcodes.gen.cpp, generated from SmartScriptMgr.h by
 * data/loom/tools/gen_opcodes.py, and cover the full SAI vocabulary. This file is the compiler:
 * it walks the YAML document and fills SmartScriptHolder structs using those tables.
 */

#include "LoomLoader.h"
#include "LoomOpcodes.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "World.h"
#include <fkYAML/node.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace
{
    // Opcode tables (LoomEvents/LoomActions/LoomTargets) are generated from SmartScriptMgr.h into
    // LoomOpcodes.gen.cpp — see LoomOpcodes.h. They cover the full SAI vocabulary.

    // ---- fkYAML helpers -----------------------------------------------------------------------

    bool Has(fkyaml::node const& n, char const* key)
    {
        return n.is_mapping() && n.contains(key);
    }

    uint32 AsUInt(fkyaml::node const& n)
    {
        if (n.is_integer())
            return static_cast<uint32>(n.get_value<int64_t>());
        return static_cast<uint32>(std::stoll(n.get_value<std::string>()));
    }

    void FillSlot(fkyaml::node const& src, std::string const& slot, uint32& out)
    {
        if (!slot.empty() && Has(src, slot.c_str()))
            out = AsUInt(src[slot]);
    }

    // Fill any generic `paramN` keys (1..count) — the fallback for raw slots that the schema does
    // not give a descriptive name. Guarantees lossless round-trips even for undocumented params.
    void FillGeneric(fkyaml::node const& src, uint32* raw[], size_t count)
    {
        for (size_t i = 0; i < count; ++i)
            FillSlot(src, "param" + std::to_string(i + 1), *raw[i]);
    }

    // ---- Compilation context ------------------------------------------------------------------

    class Context
    {
    public:
        Context(int32 entryOrGuid, SmartScriptType sourceType)
            : EntryOrGuid(entryOrGuid), SourceType(sourceType) {}

        int32 const           EntryOrGuid;
        SmartScriptType const SourceType;

        std::vector<SmartScriptHolder> Holders;
        std::vector<std::string>       Errors;
        uint32 NextEventId = 0;

        // Phase labels -> 1..12. A label of the form `phase_N` is pinned to N so that content
        // migrated from SAI keeps its exact phase numbers (round-trip fidelity); arbitrary labels
        // are assigned the lowest free number. The author never has to touch the bitmask.
        uint32 PhaseBit(std::string const& label)
        {
            auto it = _phases.find(label);
            if (it != _phases.end())
                return it->second;

            // A `phase_N` label pins to N. N == 0 is valid and means "no phase" (SET_EVENT_PHASE 0
            // clears the phase / returns to always-active), so it must not be reassigned.
            if (label.rfind("phase_", 0) == 0)
            {
                char* end = nullptr;
                long n = std::strtol(label.c_str() + 6, &end, 10);
                if (end && *end == '\0' && n >= 0 && n <= SMART_EVENT_PHASE_COUNT)
                {
                    _phases[label] = static_cast<uint32>(n);
                    return static_cast<uint32>(n);
                }
            }

            // Arbitrary label -> lowest free phase number 1..12.
            bool taken[SMART_EVENT_PHASE_COUNT + 1] = { false };
            for (auto const& kv : _phases)
                if (kv.second <= SMART_EVENT_PHASE_COUNT)
                    taken[kv.second] = true;
            uint32 num = 0;
            for (uint32 c = 1; c <= SMART_EVENT_PHASE_COUNT; ++c)
                if (!taken[c]) { num = c; break; }

            if (num == 0)
            {
                Errors.push_back("more than " + std::to_string(SMART_EVENT_PHASE_COUNT) +
                                 " phases used (label '" + label + "')");
                return 0;
            }
            _phases[label] = num;
            return num;
        }

        SmartScriptHolder MakeHolder()
        {
            SmartScriptHolder h;
            h.entryOrGuid = EntryOrGuid;
            h.source_type = SourceType;
            h.event_id    = NextEventId++;
            h.link        = 0;
            return h;
        }

    private:
        std::unordered_map<std::string, uint32> _phases;
    };

    bool CompileTarget(Context& ctx, fkyaml::node const& at, SmartScriptHolder& holder)
    {
        std::string tgtName;
        fkyaml::node params; // may stay null for scalar targets

        if (at.is_string())
        {
            tgtName = at.get_value<std::string>();          // `at: victim`
        }
        else if (at.is_mapping() && at.begin() != at.end())
        {
            auto first = at.begin();                        // `at: { creature_range: { entry: 123, ... } }`
            tgtName = first.key().get_value<std::string>();
            params  = first.value();
        }
        else
        {
            ctx.Errors.push_back("`at:` must be a target keyword or single-key mapping");
            return false;
        }

        auto const tgtIt = LoomTargets().find(tgtName);
        if (tgtIt == LoomTargets().end())
        {
            ctx.Errors.push_back("unknown target `at: " + tgtName + "`");
            return false;
        }
        LoomOpcodeDef const& tgtDef = tgtIt->second;
        holder.target.type = static_cast<SMARTAI_TARGETS>(tgtDef.type);

        if (holder.target.type == SMART_TARGET_POSITION && params.is_mapping())
        {
            if (params.contains("x")) holder.target.x = params["x"].get_value<float>();
            if (params.contains("y")) holder.target.y = params["y"].get_value<float>();
            if (params.contains("z")) holder.target.z = params["z"].get_value<float>();
            if (params.contains("o")) holder.target.o = params["o"].get_value<float>();
            return true;
        }

        if (params.is_mapping())
        {
            uint32* raw[4] = { &holder.target.raw.param1, &holder.target.raw.param2,
                               &holder.target.raw.param3, &holder.target.raw.param4 };
            for (size_t i = 0; i < tgtDef.slots.size() && i < 4; ++i)
                FillSlot(params, tgtDef.slots[i], *raw[i]);
            FillGeneric(params, raw, 4);
        }
        return true;
    }

    bool CompileAction(Context& ctx, fkyaml::node const& action, SmartScriptHolder& holder)
    {
        if (!action.is_mapping() || action.begin() == action.end())
        {
            ctx.Errors.push_back("action must be a single-key mapping (e.g. `- cast: {...}`)");
            return false;
        }

        auto first = action.begin();
        std::string const acName = first.key().get_value<std::string>();
        fkyaml::node const& value = first.value();

        auto const acIt = LoomActions().find(acName);
        if (acIt == LoomActions().end())
        {
            ctx.Errors.push_back("unknown action `" + acName + "`");
            return false;
        }
        LoomOpcodeDef const& acDef = acIt->second;
        holder.action.type = static_cast<SMART_ACTION>(acDef.type);

        uint32* raw[6] = { &holder.action.raw.param1, &holder.action.raw.param2, &holder.action.raw.param3,
                           &holder.action.raw.param4, &holder.action.raw.param5, &holder.action.raw.param6 };

        // `phase:` special-case — the value is a label, not a number.
        if (holder.action.type == SMART_ACTION_SET_EVENT_PHASE)
        {
            *raw[0] = ctx.PhaseBit(value.get_value<std::string>());
        }
        else if (value.is_mapping())
        {
            for (size_t i = 0; i < acDef.slots.size() && i < 6; ++i)
                FillSlot(value, acDef.slots[i], *raw[i]);
            FillGeneric(value, raw, 6);
        }
        else if (!value.is_null())
        {
            // Scalar shorthand fills the first slot, e.g. `talk: 0`, `despawn: 3000`.
            *raw[0] = AsUInt(value);
        }

        // Target: the `at:` sub-key of a map value, else NONE.
        if (value.is_mapping() && value.contains("at"))
            return CompileTarget(ctx, value["at"], holder);

        holder.target.type = SMART_TARGET_NONE;
        return true;
    }

    bool CompileEvent(Context& ctx, fkyaml::node const& event)
    {
        // NOTE: the trigger key is `event:`, not `on:`. `on` is a YAML 1.1 boolean (like yes/no/off),
        // so PyYAML-based tooling would read the key as `true` while fkYAML (YAML 1.2) reads "on" —
        // the two layers must never disagree, so we avoid 1.1 boolean keywords entirely.
        if (!Has(event, "event"))
        {
            ctx.Errors.push_back("event missing `event:` key");
            return false;
        }

        std::string const evName = event["event"].get_value<std::string>();
        auto const evIt = LoomEvents().find(evName);
        if (evIt == LoomEvents().end())
        {
            ctx.Errors.push_back("unknown event `event: " + evName + "`");
            return false;
        }
        LoomOpcodeDef const& evDef = evIt->second;

        SmartEvent evTemplate{};
        evTemplate.type = static_cast<SMART_EVENT>(evDef.type);

        uint32* raw[6] = { &evTemplate.raw.param1, &evTemplate.raw.param2, &evTemplate.raw.param3,
                           &evTemplate.raw.param4, &evTemplate.raw.param5, &evTemplate.raw.param6 };
        for (size_t i = 0; i < evDef.slots.size() && i < 6; ++i)
            FillSlot(event, evDef.slots[i], *raw[i]);
        FillGeneric(event, raw, 6);

        // Sugar: `below:`/`above:` for *_pct events -> hp/mana min+max.
        if (Has(event, "below")) { *raw[0] = 0; *raw[1] = AsUInt(event["below"]); }
        if (Has(event, "above")) { *raw[0] = AsUInt(event["above"]); *raw[1] = 100; }

        // Sugar: `initial: [min,max]` / `every: [min,max]` for timed events.
        auto readPair = [](fkyaml::node const& seq, uint32& lo, uint32& hi)
        {
            auto it = seq.begin();
            lo = AsUInt(*it); ++it;
            hi = (it != seq.end()) ? AsUInt(*it) : lo;
        };
        if (Has(event, "initial") && event["initial"].is_sequence())
            readPair(event["initial"], *raw[0], *raw[1]);
        if (Has(event, "every") && event["every"].is_sequence())
            readPair(event["every"], *raw[2], *raw[3]);

        evTemplate.event_chance = Has(event, "chance") ? AsUInt(event["chance"]) : 100;
        evTemplate.event_flags  = 0;
        if (Has(event, "once") && event["once"].get_value<bool>())
            evTemplate.event_flags |= SMART_EVENT_FLAG_NOT_REPEATABLE;

        evTemplate.event_phase_mask = 0;
        if (Has(event, "in_phase") && event["in_phase"].is_sequence())
        {
            for (fkyaml::node const& p : event["in_phase"])
            {
                uint32 bit = ctx.PhaseBit(p.get_value<std::string>());
                if (bit >= 1 && bit <= SMART_EVENT_PHASE_COUNT)
                    evTemplate.event_phase_mask |= SmartPhaseMask[bit - 1][1];
            }
        }

        if (!Has(event, "do") || !event["do"].is_sequence())
        {
            ctx.Errors.push_back("event `event: " + evName + "` has no `do:` action list");
            return false;
        }

        std::vector<fkyaml::node> actions(event["do"].begin(), event["do"].end());
        if (actions.empty())
        {
            ctx.Errors.push_back("event `event: " + evName + "` has an empty `do:` list");
            return false;
        }

        // The `do:` list becomes a link-chain of holders: the first carries the real event, the rest
        // are SMART_EVENT_LINK — exactly how SAI represents multi-action events.
        size_t const firstHolderIndex = ctx.Holders.size();
        for (size_t i = 0; i < actions.size(); ++i)
        {
            SmartScriptHolder h = ctx.MakeHolder();
            h.event = (i == 0) ? evTemplate : SmartEvent{};
            if (i != 0)
            {
                h.event.type             = SMART_EVENT_LINK;
                h.event.event_chance     = 100;
                h.event.event_phase_mask = evTemplate.event_phase_mask;
            }

            if (!CompileAction(ctx, actions[i], h))
                return false;

            ctx.Holders.push_back(std::move(h));
        }

        // Wire the link chain: each holder points at the next holder's event_id; last stays 0.
        for (size_t i = firstHolderIndex; i + 1 < ctx.Holders.size(); ++i)
            ctx.Holders[i].link = ctx.Holders[i + 1].event_id;

        return true;
    }

    // Convert an `include:` argument value to a substitution string.
    std::string ArgToString(fkyaml::node const& v)
    {
        if (v.is_integer())
            return std::to_string(v.get_value<int64_t>());
        if (v.is_string())
            return v.get_value<std::string>();
        return {};
    }

    std::string Substitute(std::string body, std::string const& name, std::string const& value)
    {
        // Replace every `{{ name }}` (any inner spacing) with the value.
        std::regex ph("\\{\\{\\s*" + name + "\\s*\\}\\}");
        return std::regex_replace(body, ph, value);
    }

    // Expand one `include:` entry into its events, appended to `outEvents`. Parsed thread documents
    // are kept alive in `keepAlive` because the appended nodes are copies referencing owned data.
    void ExpandInclude(Context& ctx, fkyaml::node const& inc, LoomThreadMap const& threads,
                       std::vector<fkyaml::node>& outEvents, std::vector<fkyaml::node>& keepAlive)
    {
        std::string name;
        fkyaml::node args;
        if (inc.is_string())
        {
            name = inc.get_value<std::string>();
        }
        else if (inc.is_mapping() && inc.begin() != inc.end())
        {
            name = inc.begin().key().get_value<std::string>();
            args = inc.begin().value();
        }
        else
        {
            ctx.Errors.push_back("`include:` entry must be a thread name or single-key mapping");
            return;
        }

        auto it = threads.find(name);
        if (it == threads.end())
        {
            ctx.Errors.push_back("unknown thread `include: " + name + "`");
            return;
        }

        std::string body = it->second.body;
        if (args.is_mapping())
            for (auto kv = args.begin(); kv != args.end(); ++kv)
                body = Substitute(body, kv.key().get_value<std::string>(), ArgToString(kv.value()));

        try
        {
            keepAlive.push_back(fkyaml::node::deserialize(body));
            fkyaml::node const& doc = keepAlive.back();
            if (doc.contains("events") && doc["events"].is_sequence())
                for (fkyaml::node const& ev : doc["events"])
                    outEvents.push_back(ev);
        }
        catch (std::exception const& e)
        {
            ctx.Errors.push_back("thread `" + name + "` failed to expand: " + e.what());
        }
    }
}

// -------------------------------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------------------------------

LoomCompileResult LoomLoader::Compile(int32 entryOrGuid, SmartScriptType sourceType,
                                      std::string const& yaml, LoomThreadMap const* threads)
{
    Context ctx(entryOrGuid, sourceType);

    try
    {
        fkyaml::node doc = fkyaml::node::deserialize(yaml);

        std::vector<fkyaml::node> events;
        std::vector<fkyaml::node> keepAlive;

        // `include:` entries expand to events spliced in ahead of the document's own events.
        if (doc.contains("include") && doc["include"].is_sequence())
        {
            if (!threads)
                ctx.Errors.push_back("document uses `include:` but no threads are available");
            else
                for (fkyaml::node const& inc : doc["include"])
                    ExpandInclude(ctx, inc, *threads, events, keepAlive);
        }

        if (doc.contains("events") && doc["events"].is_sequence())
            for (fkyaml::node const& ev : doc["events"])
                events.push_back(ev);

        if (events.empty() && ctx.Errors.empty())
            ctx.Errors.push_back("document has no events (missing `events:` and `include:`)");

        for (fkyaml::node const& ev : events)
            CompileEvent(ctx, ev);
    }
    catch (std::exception const& e)
    {
        ctx.Errors.push_back(std::string("YAML parse error: ") + e.what());
    }

    LoomCompileResult result;
    result.errors = std::move(ctx.Errors);
    if (result.ok())
        result.holders = std::move(ctx.Holders);
    return result;
}

namespace
{
    // The Loom content root: `LoomContentDir` if set, else `<DataDir>/loom`. Script `file` paths in
    // creature_loom are resolved against it; reusable threads are scanned from `<root>/threads/`.
    std::filesystem::path LoomContentRoot()
    {
        std::string dir = sConfigMgr->GetOption<std::string>("LoomContentDir", "");
        if (dir.empty())
            dir = sWorld->GetDataPath() + "loom";
        return std::filesystem::path(dir);
    }

    std::string ReadFile(std::filesystem::path const& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return {};
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    // Scan `<root>/threads/*.loom` into a thread registry. Each file declares `thread:`, `params:`
    // and a `body:` template; they need no DB rows because scripts reference them by name.
    LoomThreadMap LoadLoomThreads(std::filesystem::path const& root)
    {
        LoomThreadMap threads;
        std::filesystem::path dir = root / "threads";
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec))
            return threads;

        for (auto const& entry : std::filesystem::directory_iterator(dir, ec))
        {
            if (entry.path().extension() != ".loom")
                continue;
            try
            {
                fkyaml::node doc = fkyaml::node::deserialize(ReadFile(entry.path()));
                if (!doc.contains("thread"))
                    continue;

                LoomThread thread;
                thread.body = doc.contains("body") ? doc["body"].get_value<std::string>() : std::string{};
                if (doc.contains("params") && doc["params"].is_sequence())
                    for (fkyaml::node const& p : doc["params"])
                        thread.params.push_back(p.get_value<std::string>());

                threads[doc["thread"].get_value<std::string>()] = std::move(thread);
            }
            catch (std::exception const& e)
            {
                LOG_ERROR("server.loading", "Loom: thread file {} failed to parse: {}",
                          entry.path().string(), e.what());
            }
        }
        return threads;
    }
}

uint32 LoomLoader::LoadInto(SmartAIEventMap* eventMap)
{
    std::filesystem::path const root = LoomContentRoot();
    LoomThreadMap const threads = LoadLoomThreads(root);

    WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_SEL_LOOM_SCRIPTS);
    PreparedQueryResult result = WorldDatabase.Query(stmt);
    if (!result)
        return 0;

    uint32 entities = 0;
    uint32 rejected = 0;

    do
    {
        Field* fields = result->Fetch();
        int32 const           entryOrGuid = fields[0].Get<int32>();
        SmartScriptType const sourceType  = static_cast<SmartScriptType>(fields[1].Get<uint8>());
        std::string const     file        = fields[2].Get<std::string>();

        std::string const yaml = ReadFile(root / file);
        if (yaml.empty())
        {
            ++rejected;
            LOG_ERROR("sql.sql", "Loom: creature_loom (entryorguid {}, source_type {}) file '{}' is missing or empty",
                      entryOrGuid, uint32(sourceType), file);
            continue;
        }

        LoomCompileResult compiled = LoomLoader::Compile(entryOrGuid, sourceType, yaml, &threads);
        if (!compiled.ok())
        {
            ++rejected;
            for (std::string const& err : compiled.errors)
                LOG_ERROR("sql.sql", "Loom: {} (entryorguid {}, source_type {}) rejected: {}",
                          file, entryOrGuid, uint32(sourceType), err);
            continue;
        }

        // Loom wins over any legacy SAI rows already loaded for this key.
        SmartAIEventList& list = eventMap[uint32(sourceType)][entryOrGuid];
        list.clear();
        for (SmartScriptHolder& h : compiled.holders)
            list.push_back(std::move(h));

        ++entities;
    } while (result->NextRow());

    LOG_INFO("server.loading", ">> Loaded {} Loom script(s) ({} rejected).", entities, rejected);
    return entities;
}
