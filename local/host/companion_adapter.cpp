#include "../../src/companion.h"
#include "companion_payload_store.h"
#include "companion_diagnostics.h"
#include "../../src/menu.h"
#include "../../src/missions.h"
#include "../../src/npc.h"
#include "../../src/spawner.h"

// Current-client evidence: inspect_flowgraph_bridge.py, flow-handlers/flow-load.
// Resolve from the command registration, not fixed RVAs from a prior build.
using CommandFn = void(__fastcall*)(void*);
static CommandFn g_executeModule = nullptr;
static uintptr_t* g_flowSystem = nullptr;
using TypeFn = uint16_t(__fastcall*)(uintptr_t, const char*);
static TypeFn g_getType = nullptr;
static bool g_nodeConfigAbi = false;
using FactionGetFn = uint8_t(__fastcall*)(uintptr_t);
using FactionSetFn = void(__fastcall*)(uintptr_t, uint8_t);
static FactionGetFn g_factionGet = nullptr;
static FactionSetFn g_factionSet = nullptr;
using PersonalHostileFn = bool(__fastcall*)(uintptr_t, uint64_t, uint8_t);
using PersonalAggressorFn = bool(__fastcall*)(uintptr_t, uint64_t, bool);
using FactionReactionFn = uint8_t(__fastcall*)(uintptr_t, uint8_t);
static PersonalHostileFn g_personalHostile = nullptr;
static PersonalAggressorFn g_personalAggressor = nullptr;
static FactionReactionFn g_factionReaction = nullptr;
static bool g_personalLayoutVerified = false;
static companion::RequestQueue g_requests;
static companion::Diagnostics g_diagnostics;
static uint64_t g_peacefulNpc = 0, g_leaderRequestedNpc = 0;
static uint8_t g_leaderNpcFaction = 255, g_leaderPlayerFaction = 255;
static companion::PayloadStore g_payloads;
static const companion::Payload* g_executingPayload = nullptr;
static bool g_orderExecuting = false;
static bool g_probeExecuting = false;
// Results are indexed by the module suffix, so cancellation of an older order
// cannot be mistaken for the result of the latest request. Native callbacks may
// arrive while another module is running; publish only small atomic values.
static volatile LONG g_orderCount = 0;
static volatile LONG g_orderActions[16] = {};
static volatile LONG g_probeObserved = 0;
static bool ObserveCompanionResult(const char* message, bool active) {
    if (!memchr(message, '\0', 120)) return false;
    if (active && !strcmp(message, "SCO_COMPANION_PROBE_EXECUTED"))
        InterlockedExchange(&g_probeObserved, 1);
    const LONG count = InterlockedCompareExchange(&g_orderCount, 0, 0);
    return g_diagnostics.observe(message, active, static_cast<unsigned>(count));
}
using CreateInstanceFn = uint32_t(__fastcall*)(uintptr_t, uint32_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
static CreateInstanceFn g_createInstanceOrig = nullptr;
static uint32_t __fastcall CreateInstanceObserved(uintptr_t manager, uint32_t moduleId, uintptr_t values,
    uintptr_t callback, uintptr_t arg5, uintptr_t arg6) {
    const uint32_t instance = g_createInstanceOrig(manager, moduleId, values, callback, arg5, arg6);
    if (g_probeExecuting || g_orderExecuting)
        Log("[companion-execution] native create-instance module=%u result=%u%s; AI acceptance not inferred",
            moduleId, instance, instance == 0xFFFFFFFF ? " (invalid instance)" : "");
    return instance;
}
static const char* g_actionStage = "idle";

static int LogActionFault(EXCEPTION_POINTERS* e, const char* stage) {
    const uintptr_t at = e && e->ExceptionRecord ? reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress) : 0;
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    Log("[companion] exception stage=%s code=%08lX address=%llX game-rva=%llX", stage,
        e && e->ExceptionRecord ? e->ExceptionRecord->ExceptionCode : 0,
        static_cast<unsigned long long>(at), static_cast<unsigned long long>(at >= base ? at - base : 0));
    return EXCEPTION_EXECUTE_HANDLER;
}

static bool ReadablePointer(uintptr_t p, size_t bytes) {
    MEMORY_BASIC_INFORMATION info = {};
    if (!p || !VirtualQuery(reinterpret_cast<void*>(p), &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const uintptr_t end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    return p <= end && bytes <= end - p;
}
using DebugEventFn = void(__fastcall*)(uintptr_t, int, uintptr_t);
static DebugEventFn g_debugEventOrig = nullptr;
static uintptr_t g_debugLogGate = 0;
static void __fastcall DebugEventObserved(uintptr_t node, int event, uintptr_t info) {
    // Observe only our marker; always forward to the original node unchanged.
    __try {
        if (event == 1 && ReadablePointer(info, 0x28)) {
            const uintptr_t inputs = Rd<uintptr_t>(info + 0x20);
            if (ReadablePointer(inputs, 80)) {
                const uintptr_t message = Rd<uintptr_t>(inputs + 40);
                if (ReadablePointer(message, 120) &&
                    !strncmp(reinterpret_cast<const char*>(message), "SCO_COMPANION_", 14)) {
                    const bool active = (Rd<uint8_t>(inputs + 32) & 1) != 0;
                    if (ObserveCompanionResult(reinterpret_cast<const char*>(message), active)) {
                    const uintptr_t gate = ReadablePointer(g_debugLogGate, 8) ? Rd<uintptr_t>(g_debugLogGate) : 0;
                    const int suppressed = ReadablePointer(gate, 16) ? Rd<int>(gate + 12) : -1;
                    Log("[companion-execution] Debug:Log message=%.119s input-active=%d native-log-suppressed=%d; physical behavior still needs observation",
                        reinterpret_cast<const char*>(message), active, suppressed);
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Observation failure must not replace the native behavior.
    }
    g_debugEventOrig(node, event, info);
}
static volatile LONG g_probeRequested = 0;
static char g_status[512] = "Проверка загрузки модуля ещё не выполнена.";
static bool g_disabled = false;
static bool g_managerVerified = false;
static uintptr_t g_buddyEnvironmentSlot = 0;
static bool g_assignmentLayoutVerified = false;
using CancelDispatchFn = void(__fastcall*)(uintptr_t, uintptr_t, const void*);
static CancelDispatchFn g_cancelDispatch = nullptr;
static uintptr_t g_cancelSystemSlot = 0;
static uint64_t g_trialNpc = 0, g_trialStarted = 0, g_trialAcceptedAt = 0;
static uintptr_t g_trialComponent = 0;
static companion::TrialObservation g_trialObservation;
static bool g_trialUsed = false, g_trialCancelSent = false, g_trialCancelDecided = false;
static uint64_t g_trialCancelAt = 0, g_trialLastPoll = 0;
static uint8_t g_emptyCancelPayload = 0; // empty event: native copy/destruct callbacks are no-ops
static const char kName[] = "SCO_CompanionProbe";
static const char kPath[] = "Data/Libs/FlowgraphModules/SCO_CompanionProbe.xml";
static const char kSchemaName[] = "SCO_CompanionSchema";
static const char kSchemaPath[] = "Data/Libs/FlowgraphModules/SCO_CompanionSchema.xml";

bool CompanionXmlPayload(const char* path, const char*& data, size_t& size) {
    if (!path) return false;
    if (const auto* payload = g_payloads.find(path)) {
        data = payload->xml; size = strlen(payload->xml); return true;
    }
    if (!_stricmp(path, kSchemaPath)) {
        // Disconnected nodes; this module is inspected, never instantiated/executed.
        static const char schema[] =
            "<Graph isModule=\"1\" moduleName=\"SCO_CompanionSchema\" enabled=\"0\" MultiPlayer=\"ClientServer\">"
            "<ModuleInputsOutputs/><Nodes>"
            "<Node Id=\"1\" Class=\"AI:Assignment:AssignBuddyLeader\"><Inputs/></Node>"
            "<Node Id=\"2\" Class=\"AI:Assignment:AssignmentFollowAtDistance\"><Inputs/></Node>"
            "<Node Id=\"3\" Class=\"AI:Assignment:AssignmentDefendTarget\"><Inputs/></Node>"
            "<Node Id=\"4\" Class=\"AI:Assignment:AssignmentIdle\"><Inputs/></Node>"
            "<Node Id=\"5\" Class=\"AI:Assignment:AssignmentCharacterMoveTo\"><Inputs/></Node>"
            "<Node Id=\"6\" Class=\"Debug:Log\"><Inputs/></Node>"
            "</Nodes><Edges/><GraphTokens/></Graph>";
        data = schema; size = sizeof(schema) - 1; return true;
    }
    if (_stricmp(path, kPath)) return false;
    static const char payload[] =
        "<Graph isModule=\"1\" moduleName=\"SCO_CompanionProbe\" enabled=\"1\" MultiPlayer=\"ClientServer\">"
        "<ModuleInputsOutputs/><Nodes>"
        "<Node Id=\"1\" Class=\"Module:Start_SCO_CompanionProbe\"/>"
        "<Node Id=\"2\" Class=\"Debug:Log\"><Inputs message=\"SCO_COMPANION_PROBE_EXECUTED\"/></Node>"
        "</Nodes><Edges>"
        "<Edge nodeIn=\"2\" nodeOut=\"1\" portIn=\"input\" portOut=\"Start\" enabled=\"1\"/>"
        "</Edges><GraphTokens/></Graph>";
    data = payload;
    size = sizeof(payload) - 1;
    return true;
}

static bool InText(const uint8_t* p, size_t n = 1) {
    return p >= g_text.base && p + n <= g_text.base + g_text.size;
}

static CommandFn FindCommand(const Section& text, const Section& rdata, const char* command) {
    const uint8_t* name = FindCString(rdata, command);
    const uint8_t* lea = name ? FindRipLea(text, 0x48, 0x8D, 0x15, name) : nullptr;
    if (!lea) return nullptr;
    for (int back = 7; back <= 40; ++back) {
        const uint8_t* p = lea - back;
        if (p < text.base || !BytesMatch(p, "4C 8D 05")) continue;
        const uint8_t* handler = p + 7 + Rel32(p + 3);
        if (handler >= text.base && handler + 32 < text.base + text.size)
            return reinterpret_cast<CommandFn>(const_cast<uint8_t*>(handler));
    }
    return nullptr;
}

void ResolveCompanionBridge(const Section& text, const Section& rdata) {
    int matches = 0;
    // Native reset of the current assignment: this+1B0/1B8 are a shared pointer.
    // Read-only inspection uses this layout only when its reset code is unique.
    g_assignmentLayoutVerified = FindUniquePattern(text,
        "48 89 5C 24 08 57 48 83 EC 20 48 8B D9 E8 ?? ?? ?? ?? 48 8B BB B8 01 00 00 33 C0 48 89 83 B0 01 00 00 48 89 83 B8 01 00 00", matches) != nullptr;
    Log("[companion-assignment] current-assignment layout matches=%d; verified=%d; no cancellation invoked", matches, g_assignmentLayoutVerified);
    const uint8_t* cancelCaller = FindUniquePattern(text,
        "4C 8B 3D ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 48 8B F0 0F B7 40 04 66 83 F8 04 74 2B 66 39 7E 02 75 25 66 83 F8 02 75 0B", matches);
    // Generic emitter/constructor prefixes are shared by other event types.
    // Resolve the emitter through the unique native task call and prove its
    // cached type identity through the exact CancelAssignments name constructor.
    const uint8_t* cancel = cancelCaller && InText(cancelCaller, 91) && cancelCaller[86] == 0xE8 ?
        cancelCaller + 91 + Rel32(cancelCaller + 87) : nullptr;
    const uint8_t* eventName = FindCString(rdata, "AI_EvtControl_CancelAssignments");
    const uint8_t* eventLea = eventName ? FindRipLea(text, 0x48, 0x8D, 0x05, eventName) : nullptr;
    const uint8_t* cancelCtor = eventLea && eventLea >= text.base + 92 ? eventLea - 92 : nullptr;
    if (cancel && cancelCtor && InText(cancel, 124) && InText(cancelCtor, 112) &&
        BytesMatch(cancel, "48 89 5C 24 08 57 48 83 EC 50 8B 05 ?? ?? ?? ?? 48 8B FA 4C 89 44 24 20 48 8B D9 85 C0 75 19 41 B8 77 00 00 00") &&
        BytesMatch(cancelCtor, "40 53 48 83 EC 20 33 C0 C6 42 14 01 89 42 10 48 8B DA 48 89 42 18 88 42 20 48 8D 05 ?? ?? ?? ?? 48 89 42 08 8B 05 ?? ?? ?? ?? 66 C7 42 22 FF 03") &&
        cancel + 16 + Rel32(cancel + 12) == cancelCtor + 42 + Rel32(cancelCtor + 38) &&
        BytesMatch(cancel + 104, "48 8B 03 FF 90 78 03 00 00")) {
        const uint8_t* copy = cancel + 72 + Rel32(cancel + 68);
        const uint8_t* destroy = cancel + 96 + Rel32(cancel + 92);
        if (InText(copy, 3) && InText(destroy, 3) &&
            (copy[0] == 0xC3 || BytesMatch(copy, "C2 00 00")) &&
            (destroy[0] == 0xC3 || BytesMatch(destroy, "C2 00 00"))) {
            g_cancelDispatch = reinterpret_cast<CancelDispatchFn>(const_cast<uint8_t*>(cancel));
            g_cancelSystemSlot = reinterpret_cast<uintptr_t>(cancelCaller + 7 + Rel32(cancelCaller + 3));
        }
    }
    Log("[companion-trial] typed CancelAssignments dispatch verified=%d; no cancellation executed", g_cancelDispatch != nullptr);
    // Evidence: native BuddyLeaderChanged and its two leaf getters. Resolve the
    // environment slot from that caller; never call the leader-change handler.
    const uint8_t* buddyCaller = FindUniquePattern(text,
        "48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? 4C 8B F0 48 85 C0 0F 84 ?? ?? ?? ?? 48 83 BF B0 02 00 00 00", matches);
    if (buddyCaller) {
        const uint8_t* hubGetter = buddyCaller + 12 + Rel32(buddyCaller + 8);
        const uint8_t* mapGetter = buddyCaller + 20 + Rel32(buddyCaller + 16);
        if (InText(hubGetter, 8) && InText(mapGetter, 5) &&
            BytesMatch(hubGetter, "48 8B 81 60 0C 00 00 C3") &&
            BytesMatch(mapGetter, "48 8B 41 60 C3"))
            g_buddyEnvironmentSlot = reinterpret_cast<uintptr_t>(buddyCaller + 7 + Rel32(buddyCaller + 3));
    }
    Log("[companion-buddy-data] environment caller matches=%d; getter layout verified=%d; read-only probe pending", matches, g_buddyEnvironmentSlot != 0);
    g_getType = reinterpret_cast<TypeFn>(FindUniquePattern(text,
        "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 48 8B F1 48 89 54 24 20 48 8D 54 24 20 48 8D 4C 24 28 E8 ?? ?? ?? ?? 48 8B 5E 10 48 8B 7B 08 80 7F 19 00", matches));
    Log("[companion] native GetTypeId signature matches: %d", matches);
    // Native CFlowData::GetConfiguration: calls IFlowNode::GetConfiguration at 0x20,
    // then walks input descriptors at 72-byte stride (current XML parser confirms it).
    g_nodeConfigAbi = FindUniquePattern(text,
        "41 56 48 81 EC C0 00 00 00 48 8B 49 18 4C 8B F2 48 8B 01 FF 50 20 41 F6 46 10 01 0F 84 ?? ?? ?? ?? 49 8B 06", matches) != nullptr;
    Log("[companion] node configuration ABI signature matches: %d", matches);
    g_factionGet = reinterpret_cast<FactionGetFn>(FindUniquePattern(text,
        "40 53 48 83 EC 30 0F B6 81 2C 02 00 00 48 8B D9 3C FF 75 6E 48 8D 05 ?? ?? ?? ?? 48 89 7C 24 48", matches));
    Log("[companion] faction getter signature matches: %d", matches);
    g_factionSet = reinterpret_cast<FactionSetFn>(FindUniquePattern(text,
        "48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC 40 48 8D 05 ?? ?? ?? ?? 0F B6 EA 48 89 44 24 60 48 8D 99 E0 01 00 00 48 8B F1", matches));
    Log("[companion] faction setter signature matches: %d", matches);
    g_personalHostile = reinterpret_cast<PersonalHostileFn>(FindUniquePattern(text,
        "48 89 5C 24 10 48 89 6C 24 18 56 57 41 54 41 56 41 57 48 83 EC 30 4C 8D 25 ?? ?? ?? ?? 45 0F B6 F8 4C 89 64 24 60 48 8D B1 98 00 00 00", matches));
    Log("[companion] personal hostility getter signature matches: %d", matches);
    g_personalAggressor = reinterpret_cast<PersonalAggressorFn>(FindUniquePattern(text,
        "48 89 6C 24 18 57 48 83 EC 30 48 8B FA 48 8B E9 45 84 C0 74 16 80 B9 D0 00 00 00 00 74 0D B0 01", matches));
    Log("[companion] personal aggressor getter signature matches: %d", matches);
    g_factionReaction = reinterpret_cast<FactionReactionFn>(FindUniquePattern(text,
        "48 89 5C 24 18 48 89 6C 24 20 56 48 83 EC 30 48 8D 05 ?? ?? ?? ?? 0F B6 F2 48 89 44 24 40 48 8D 99 90 03 00 00 48 8B E9", matches));
    Log("[companion] faction reaction getter signature matches: %d", matches);
    // Native Hostility caller passes its embedded CPersonalHostility (+0x258)
    // and EntityId to IsAggressorTo. Verify both the layout and the call target.
    const uint8_t* personalCaller = FindUniquePattern(text,
        "49 8D 8E 58 02 00 00 41 B0 01 48 8B 10 E8 ?? ?? ?? ?? 48 83 C4 40", matches);
    g_personalLayoutVerified = personalCaller && g_personalAggressor &&
        personalCaller + 18 + Rel32(personalCaller + 14) == reinterpret_cast<const uint8_t*>(g_personalAggressor);
    Log("[companion] personal hostility layout caller matches: %d; verified=%d", matches, g_personalLayoutVerified);
    const auto reload = reinterpret_cast<const uint8_t*>(FindCommand(text, rdata, "fg_reloadModules"));
    const auto execute = reinterpret_cast<const uint8_t*>(FindCommand(text, rdata, "fg_executeModule"));
    // reload obtains IModuleManager at flow slot 0xC0, then scans at manager 0x48.
    if (!reload || !execute || !InText(reload, 48) || !InText(execute, 48) ||
        !BytesMatch(reload, "48 83 EC 28 48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 C0 00 00 00") ||
        !BytesMatch(execute, "40 55 41 56 41 57 48 8D 6C 24 B9 48 81 EC 90 00 00 00 48 8B 01")) {
        Log("[companion] module bridge signature mismatch; disabled");
        strcpy_s(g_status, "Интерфейс модулей этой версии не подтверждён.");
        g_disabled = true;
        return;
    }
    g_flowSystem = reinterpret_cast<uintptr_t*>(const_cast<uint8_t*>(reload + 11 + Rel32(reload + 7)));
    g_executeModule = reinterpret_cast<CommandFn>(const_cast<uint8_t*>(execute));
    Log("[companion] module bridge located; runtime module test pending");
}

static int __fastcall ArgCount(void*) { return g_orderExecuting ? 4 : 2; }
static const char* __fastcall Arg(void*, int i) {
    return i == 0 ? "fg_executeModule" : i == 1 ? (g_orderExecuting && g_executingPayload ? g_executingPayload->name : kName) :
        g_orderExecuting && g_executingPayload && i == 2 ? g_executingPayload->npc : g_orderExecuting && g_executingPayload && i == 3 ? g_executingPayload->player : "";
}
static void* const kArgsVtable[] = { nullptr, reinterpret_cast<void*>(&ArgCount), reinterpret_cast<void*>(&Arg), nullptr };
static void* const kArgs[] = { const_cast<void**>(kArgsVtable) };

static uintptr_t HostilityComponent(uintptr_t entity, const char* type) {
    __try {
        const uintptr_t c = EntityComponent(entity, type);
        Log("[companion] component query %s -> %llX", type, static_cast<unsigned long long>(c));
        if (!ReadablePointer(c, 0x230)) return 0;
        const uintptr_t vt = Rd<uintptr_t>(c);
        const uintptr_t rdata = reinterpret_cast<uintptr_t>(g_rdata.base);
        // Current native Hostility vtable: base RVA 0x8213fc0, GetFactionId at
        // +0x6E8 and SetFactionId at +0x6F8. Native caller also uses +0x6E8.
        if (vt < rdata || vt - rdata >= g_rdata.size || 0x700 > g_rdata.size - (vt - rdata)) return 0;
        const bool get = Rd<uintptr_t>(vt + 0x6E8) == reinterpret_cast<uintptr_t>(g_factionGet);
        const bool set = Rd<uintptr_t>(vt + 0x6F8) == reinterpret_cast<uintptr_t>(g_factionSet);
        Log("[companion] component %s native faction methods get=%d set=%d", type, get, set);
        return get && set ? c : 0;
    } __except (LogActionFault(GetExceptionInformation(), type)) {
        // Missing/unsupported component names are lookup failures, not a faction write.
        return 0;
    }
}

static uintptr_t Hostility(uint64_t id) {
    const uintptr_t entity = EntityPointerById(id);
    if (!entity || !g_factionGet || !g_factionSet) return 0;
    static const char* types[] = {"IEntityComponentHostility", "EntityComponentHostility", "Hostility"};
    for (const char* type : types) {
        const uintptr_t c = HostilityComponent(entity, type);
        if (c) return c;
    }
    return 0;
}

static void ReadAssignmentState(uint64_t npc) {
    __try {
        if (!g_assignmentLayoutVerified) { Log("[companion-assignment] layout unverified; read skipped"); return; }
        const uintptr_t entity = EntityPointerById(npc);
        if (!entity) return;
        const uintptr_t component = EntityComponent(entity, "ISubsumptionAssignmentComponent");
        if (!ReadablePointer(component, 0x1C0)) {
            Log("[companion-assignment] NPC=%llu assignment component missing/unreadable; no task sent", static_cast<unsigned long long>(npc)); return;
        }
        const uintptr_t vt = Rd<uintptr_t>(component);
        const uintptr_t rdata = reinterpret_cast<uintptr_t>(g_rdata.base);
        if (vt < rdata || vt - rdata >= g_rdata.size) {
            Log("[companion-assignment] component vtable outside native rdata; read skipped"); return;
        }
        const uintptr_t current = Rd<uintptr_t>(component + 0x1B0);
        const uintptr_t control = Rd<uintptr_t>(component + 0x1B8);
        Log("[companion-assignment] NPC=%llu component=%llX current=%llX control=%llX current-readable=%d; no follow/cancel requested",
            static_cast<unsigned long long>(npc), static_cast<unsigned long long>(component),
            static_cast<unsigned long long>(current), static_cast<unsigned long long>(control), ReadablePointer(current, 8));
    } __except (LogActionFault(GetExceptionInformation(), "assignment-state-read-only")) {
        Log("[companion-assignment] read inconclusive; commands remain disabled");
    }
}

static void ReadRelationship(uint64_t npc, uint64_t player, uintptr_t buddy, uintptr_t own) {
    ReadAssignmentState(npc);
    g_actionStage = "relationship faction read";
    const uint8_t playerFaction = g_factionGet(own), npcFaction = g_factionGet(buddy);
    Log("[companion] relationship NPC=%llu player=%llu faction NPC=%u player=%u (255=unknown)",
        static_cast<unsigned long long>(npc), static_cast<unsigned long long>(player), npcFaction, playerFaction);
    // These getters check separate layers, not the full final combat decision.
    if (!g_personalLayoutVerified || !g_personalHostile || !g_personalAggressor ||
        !ReadablePointer(buddy, 0x3A0) || !ReadablePointer(own, 0x3A0)) {
        Log("[companion] relationship partial: personal layout/functions unavailable; no orders");
        strcpy_s(g_status, "Фракции прочитаны; личное отношение пока не проверено. Смотрите журнал.");
        return;
    }
    g_actionStage = "NPC personal hostility read";
    const bool hostile = g_personalHostile(buddy + 0x258, player, playerFaction);
    g_actionStage = "NPC personal aggressor read";
    const bool aggressor = g_personalAggressor(buddy + 0x258, player, true);
    g_actionStage = "player personal hostility read";
    const bool playerHostile = g_personalHostile(own + 0x258, npc, npcFaction);
    g_actionStage = "player personal aggressor read";
    const bool playerAggressor = g_personalAggressor(own + 0x258, npc, true);
    Log("[companion] relationship personal NPC->player hostile=%d aggressor=%d; player->NPC hostile=%d aggressor=%d; full combat decision unverified",
        hostile, aggressor, playerHostile, playerAggressor);
    const uintptr_t vt = Rd<uintptr_t>(buddy);
    const uintptr_t rdata = reinterpret_cast<uintptr_t>(g_rdata.base);
    // Current vtable base 0x8213fc0, reaction getter pointer 0x8214710 -> +0x750.
    if (g_factionReaction && vt >= rdata && vt - rdata < g_rdata.size &&
        0x758 <= g_rdata.size - (vt - rdata) && ReadablePointer(vt + 0x750, 8) &&
        Rd<uintptr_t>(vt + 0x750) == reinterpret_cast<uintptr_t>(g_factionReaction)) {
        g_actionStage = "NPC faction reaction read";
        const uint8_t reaction = g_factionReaction(buddy, playerFaction);
        Log("[companion] relationship faction reaction NPC->player=%u; enum semantics unverified", reaction);
    }
    sprintf_s(g_status, "Личная враждебность NPC: %s; агрессия: %s. Фракция игрока: %u%s. Это не проверка всех причин боя.",
        hostile ? "да" : "нет", aggressor ? "да" : "нет", playerFaction, playerFaction == 255 ? " (неизвестна)" : "");
}

bool CompanionOrdersAvailable() { return false; }
bool CompanionFollowTrialAvailable() { return !g_disabled && g_managerVerified && g_assignmentLayoutVerified && g_cancelDispatch && !g_trialUsed && !InterlockedCompareExchange(&g_orderCount, 0, 0); }

static uintptr_t TrialAssignmentComponent(uint64_t npc) {
    const uintptr_t entity = EntityPointerById(npc);
    const uintptr_t c = entity ? EntityComponent(entity, "ISubsumptionAssignmentComponent") : 0;
    if (!ReadablePointer(c, 0x1C0)) return 0;
    const uintptr_t vt = Rd<uintptr_t>(c), rdata = reinterpret_cast<uintptr_t>(g_rdata.base);
    return vt >= rdata && vt - rdata < g_rdata.size ? c : 0;
}

static void ProcessFollowTrial(uint64_t now) {
    if (!g_trialNpc || now - g_trialLastPoll < 500) return;
    g_trialLastPoll = now;
    __try {
        const uintptr_t c = EntityAlive(g_trialNpc) ? TrialAssignmentComponent(g_trialNpc) : 0;
        if (!g_trialObservation.matches(LocalPlayerEntityId(), EntityPointerById(g_trialNpc), c)) {
            Log("[companion-trial] player/entity/component missing or changed; native dispatch refused, trial remains quarantined");
            strcpy_s(g_status, "Проверка остановлена: игрок или исходная сущность NPC изменились. Повтор запрещён.");
            g_trialNpc = 0; return;
        }
        const uintptr_t current = Rd<uintptr_t>(c + 0x1B0);
        const uintptr_t previous = g_trialObservation.previous;
        if (g_trialObservation.observe(current, g_trialCancelSent))
            Log("[companion-trial] assignment observation previous=%llX current=%llX after-cancel=%d late=%d replacement=%d disappearance=%d; attribution to our graph unverified",
                static_cast<unsigned long long>(previous), static_cast<unsigned long long>(current), g_trialCancelSent,
                g_trialObservation.lateActivation, g_trialObservation.replacement, g_trialObservation.disappearance);
        if (!g_trialAcceptedAt && current) {
            g_trialAcceptedAt = now;
            Log("[companion-trial] active assignment observed NPC=%llu current=%llX; cancellation eligibility checked after 10 s",
                g_trialNpc, static_cast<unsigned long long>(current));
        }
        const bool due = g_trialAcceptedAt ? now - g_trialAcceptedAt >= 10000 : now - g_trialStarted >= 60000;
        if (!g_trialCancelDecided && due) {
            g_trialCancelDecided = true; g_trialCancelAt = now;
            // Re-read immediately before dispatch, including changes between polls.
            const uintptr_t beforeCancel = Rd<uintptr_t>(c + 0x1B0);
            g_trialObservation.observe(beforeCancel, false);
            if (!g_trialObservation.mayCancel(beforeCancel)) {
                Log("[companion-trial] cancellation refused: no stable assignment or assignment disappeared/replaced; first=%llX current=%llX replacement=%d disappearance=%d; observing only",
                    static_cast<unsigned long long>(g_trialObservation.firstAssignment), static_cast<unsigned long long>(beforeCancel),
                    g_trialObservation.replacement, g_trialObservation.disappearance);
                strcpy_s(g_status, "Отмена не отправлена: задание отсутствует или изменилось. Продолжаю наблюдение; повтор запрещён.");
                return;
            }
            const uintptr_t system = ReadablePointer(g_cancelSystemSlot, 8) ? Rd<uintptr_t>(g_cancelSystemSlot) : 0;
            if (!ReadablePointer(system, 8) || !ReadablePointer(Rd<uintptr_t>(system), 0x380) ||
                !InText(reinterpret_cast<const uint8_t*>(Rd<uintptr_t>(Rd<uintptr_t>(system) + 0x378)))) {
                strcpy_s(g_status, "Отмена недоступна: интерфейс изменился. Повтор запрещён."); g_trialNpc = 0; return;
            }
            // Same typed helper and raw component target as the native CancelAssignments task.
            // It constructs the dispatch descriptor; we never fabricate an engine event header.
            if (!EntityAlive(g_trialNpc) || !g_trialObservation.matches(LocalPlayerEntityId(),
                    EntityPointerById(g_trialNpc), TrialAssignmentComponent(g_trialNpc))) {
                Log("[companion-trial] identity changed before cancel dispatch; cancellation refused");
                strcpy_s(g_status, "Исходная цель изменилась: отмена не отправлена, повтор запрещён.");
                g_trialNpc = 0; return;
            }
            const uintptr_t dispatchCurrent = Rd<uintptr_t>(c + 0x1B0);
            g_trialObservation.observe(dispatchCurrent, false);
            if (!g_trialObservation.mayCancel(dispatchCurrent)) {
                Log("[companion-trial] assignment changed immediately before dispatch; cancellation refused; observing only");
                strcpy_s(g_status, "Задание изменилось перед отменой: вызов отмены запрещён. Продолжаю наблюдение.");
                return;
            }
            g_trialCancelSent = true;
            Log("[companion-trial] dispatch CancelAssignments NPC=%llu before=%llX (empty event payload)",
                g_trialNpc, static_cast<unsigned long long>(current));
            g_cancelDispatch(system, c, &g_emptyCancelPayload);
            strcpy_s(g_status, "Штатная отмена отправлена. Проверяю состояние задания.");
        } else if (g_trialCancelDecided && companion::TrialObservation::finished(now, g_trialCancelAt)) {
            Log("[companion-trial] after cancellation decision current=%llX active-before-observed=%d; graph lifecycle and physical movement separately unverified",
                static_cast<unsigned long long>(current), g_trialAcceptedAt != 0);
            Log("[companion-trial] bounded observation ended: late=%d replacement=%d disappearance=%d; no second cancellation; repeat remains forbidden",
                g_trialObservation.lateActivation, g_trialObservation.replacement, g_trialObservation.disappearance);
            strcpy_s(g_status, "Наблюдение завершено. Связь задания с нашим графом не доказана; повтор запрещён.");
            g_trialNpc = 0; // one trial only: graph-instance cancellation still unverified
        }
    } __except (LogActionFault(GetExceptionInformation(), "follow-trial-monitor")) {
        g_trialNpc = 0; g_disabled = true;
        strcpy_s(g_status, "Ошибка проверки задания; дальнейшие команды отключены.");
    }
}

static void RunAction(companion::Request request) {
    int action = request.action;
    // Diagnostic snapshot only: no new native AI/graph calls and no per-frame log flood.
    const LONG issued = InterlockedCompareExchange(&g_orderCount, 0, 0);
    Log("[companion-context] action=%d tick=%llu thread=%lu selected-last-NPC=%llu previous-leader-NPC=%llu peaceful-NPC=%llu orders=%ld mutations-enabled=%d manager-verified=%d disabled=%d",
        action, GetTickCount64(), GetCurrentThreadId(), LastSpawnedNpcId(), g_leaderRequestedNpc, g_peacefulNpc,
        issued, CompanionOrdersAvailable(), g_managerVerified, g_disabled);
    for (LONG i = 0; i < issued && i < 16; ++i) {
        const unsigned result = g_diagnostics.result(static_cast<unsigned>(i));
        Log("[companion-context] prior module=SCO_CompanionOrder_%ld action=%ld result=%s; callback does not prove assignment cancelled or NPC followed",
            i + 1, InterlockedCompareExchange(&g_orderActions[i], 0, 0), result & companion::LoadFailed ? "load-failed" : result == 3 ? "Success+Failure" : result == 1 ? "Success" : result == 2 ? "Failure" : "pending");
    }
    const bool trial = action == 9;
    if (action != 7 && !(trial && CompanionFollowTrialAvailable()) && !CompanionOrdersAvailable()) {
        Log("[companion] mutation refused: BuddyAI disturbance-ignore-list lookup returned null in observed crash; native cancellation still unverified");
        strcpy_s(g_status, "Команды компаньона временно отключены после сбоя игры. Доступно чтение отношения NPC.");
        return;
    }
    Log("[companion] action requested=%d", action);
    if (g_disabled || !g_flowSystem || !g_executeModule || !g_getType) {
        Log("[companion] action refused: bridge disabled or unresolved"); return;
    }
    if (!g_managerVerified) { strcpy_s(g_status, "Дождитесь автоматической проверки модуля."); return; }
    __try {
        g_actionStage = "entity lookup";
        const uint64_t npc = LastSpawnedNpcId(), player = LocalPlayerEntityId();
        if (!request.matches(npc, player)) {
            Log("[companion-queue] stale target refused requested NPC=%llu player=%llu current NPC=%llu player=%llu",
                request.npc, request.player, npc, player);
            strcpy_s(g_status, "NPC или игрок изменился после нажатия. Команда не отправлена; проверьте выбранного NPC."); return;
        }
        if (!npc || npc == player || !EntityAlive(npc) || !player) {
            Log("[companion] action refused: NPC/player not ready (NPC=%llu player=%llu)",
                static_cast<unsigned long long>(npc), static_cast<unsigned long long>(player));
            strcpy_s(g_status, "Создайте одного наземного NPC через это меню."); return;
        }
        g_actionStage = "player hostility lookup";
        const uintptr_t own = Hostility(player);
        g_actionStage = "NPC hostility lookup";
        const uintptr_t buddy = Hostility(npc);
        if (!own || !buddy) {
            Log("[companion] faction interface unavailable; orders refused (player=%d NPC=%d)", own != 0, buddy != 0);
            strcpy_s(g_status, "Фракция не проверена: команды отключены. Смотрите mod.log."); return;
        }
        if (action == 7) { ReadRelationship(npc, player, buddy, own); return; }
        if (trial) {
            const uintptr_t c = TrialAssignmentComponent(npc);
            if (!c || Rd<uintptr_t>(c + 0x1B0) || Rd<uintptr_t>(c + 0x1B8)) {
                strcpy_s(g_status, "Проба запрещена: компонент отсутствует или уже имеет задание."); return;
            }
            g_trialComponent = c;
        }
        const LONG prior = InterlockedCompareExchange(&g_orderCount, 0, 0);
        if (prior > 0 && (g_leaderRequestedNpc != npc || action == 1 || prior >= 2)) {
            Log("[companion] lifecycle fence: selected=%llu owner=%llu prior=%ld; no verified native cancellation", npc, g_leaderRequestedNpc, prior);
            strcpy_s(g_status, "Предыдущие задания не отменены штатно. Повторное назначение или смена NPC запрещены в этой сессии."); return;
        }
        if (prior == 1 && g_diagnostics.result(0) != companion::SuccessSeen) {
            strcpy_s(g_status, "Штатный узел назначения лидера ещё не выдал Success; следующий приказ запрещён."); return;
        }
        g_actionStage = "player faction read";
        const uint8_t faction = g_factionGet(own);
        g_actionStage = "NPC faction read";
        const uint8_t before = g_factionGet(buddy);
        if (g_peacefulNpc != npc) {
            Log("[companion] action refused: peaceful behavior not confirmed for this NPC");
            strcpy_s(g_status, "Сначала подтвердите, что этот NPC сейчас не атакует вас."); return;
        }
        if (!g_personalLayoutVerified || !g_personalHostile || !g_personalAggressor || !ReadablePointer(buddy, 0x3A0)) {
            strcpy_s(g_status, "Проверка личной враждебности недоступна; приказ отменён."); return;
        }
        g_actionStage = "order personal hostility check";
        const bool hostile = g_personalHostile(buddy + 0x258, player, faction);
        const bool aggressor = g_personalAggressor(buddy + 0x258, player, true);
        if (hostile || aggressor) {
            Log("[companion] action refused: NPC hostile=%d aggressor=%d", hostile, aggressor);
            strcpy_s(g_status, "NPC проявляет личную враждебность или агрессию; приказ отменён."); return;
        }
        if (action == 1) {
            g_leaderRequestedNpc = 0;
            if (faction != 255) {
                g_actionStage = "NPC faction write";
                g_factionSet(buddy, faction);
                g_actionStage = "NPC faction readback";
                const uint8_t after = g_factionGet(buddy);
                Log("[companion] NPC=%llu faction before=%u player=%u after=%u; behavior not verified",
                    static_cast<unsigned long long>(npc), before, faction, after);
                if (after != faction) { strcpy_s(g_status, "Назначение фракции не подтвердилось."); return; }
            } else {
                Log("[companion] player faction unknown: requesting BuddyLeader for observed peaceful NPC; factions unchanged (NPC=%u player=255)", before);
            }
            action = 6;
        }
        if (!trial && action != 6 && (g_leaderRequestedNpc != npc || before != g_leaderNpcFaction || faction != g_leaderPlayerFaction)) {
            Log("[companion] action refused: leader request absent or factions changed");
            strcpy_s(g_status, "Сначала запросите назначение игрока лидером этому NPC."); return;
        }
        const unsigned count = static_cast<unsigned>(InterlockedCompareExchange(&g_orderCount, 0, 0));
        if (count >= 16) { strcpy_s(g_status, "Лимит 16 пробных команд за запуск достигнут."); return; }
        const char* nodeType = action == 6 ? "AI:Assignment:AssignBuddyLeader" : action == 3 ? "AI:Assignment:AssignmentIdle" : action == 4 ?
            "AI:Assignment:AssignmentDefendTarget" : action == 5 ? "AI:Assignment:AssignmentCharacterMoveTo" :
            "AI:Assignment:AssignmentFollowAtDistance";
        if (!g_getType(*g_flowSystem, nodeType)) { strcpy_s(g_status, "Узел задания отсутствует."); return; }
        companion::Payload draft{};
        auto& g_orderName = draft.name; auto& g_orderPath = draft.path;
        auto& g_orderXml = draft.xml; auto& g_orderNpc = draft.npc; auto& g_orderPlayer = draft.player;
        sprintf_s(g_orderName, "SCO_CompanionOrder_%u", count + 1);
        sprintf_s(g_orderPath, "Data/Libs/FlowgraphModules/%s.xml", g_orderName);
        sprintf_s(g_orderNpc, "%llu", static_cast<unsigned long long>(npc));
        sprintf_s(g_orderPlayer, "%llu", static_cast<unsigned long long>(player));
        const char* targetPort = action == 6 ? "BuddyLeader" : action == 5 ? "DestinationEntity" : "Target";
        const char* settings = action == 4 ? "Range=\"25\" PatrolAround=\"0\"" : action == 5 ?
            "IgnorePath=\"0\" ShouldTeleport=\"0\" Speed=\"2\"" : (action == 3 || action == 6) ? "" :
            "Distance=\"4\" MinDistance=\"2\" IdealDistance=\"4\" IdleSpotDistance=\"10\" AllowCombat=\"0\" FollowBetweenCover=\"0\"";
        char targetEdge[256] = {};
        if (action != 3) sprintf_s(targetEdge, "<Edge nodeIn=\"2\" nodeOut=\"1\" portIn=\"%s\" portOut=\"Player\" enabled=\"1\"/>", targetPort);
        sprintf_s(g_orderXml,
            "<Graph isModule=\"1\" moduleName=\"%s\" enabled=\"1\" MultiPlayer=\"ClientServer\">"
            "<ModuleInputsOutputs><Port Name=\"Buddy\" Type=\"EntityId\" Input=\"1\"/><Port Name=\"Player\" Type=\"EntityId\" Input=\"1\"/></ModuleInputsOutputs>"
            "<Nodes><Node Id=\"1\" Class=\"Module:Start_%s\"/><Node Id=\"2\" Class=\"%s\"><Inputs %s/></Node>"
            "<Node Id=\"3\" Class=\"Debug:Log\"><Inputs message=\"SCO_COMPANION_SUCCESS_%s\"/></Node>"
            "<Node Id=\"4\" Class=\"Debug:Log\"><Inputs message=\"SCO_COMPANION_FAILURE_%s\"/></Node></Nodes><Edges>"
            "<Edge nodeIn=\"2\" nodeOut=\"1\" portIn=\"entityId\" portOut=\"Buddy\" enabled=\"1\"/>%s"
            "<Edge nodeIn=\"2\" nodeOut=\"1\" portIn=\"Trigger\" portOut=\"Start\" enabled=\"1\"/>"
            "<Edge nodeIn=\"3\" nodeOut=\"2\" portIn=\"input\" portOut=\"Success\" enabled=\"1\"/>"
            "<Edge nodeIn=\"4\" nodeOut=\"2\" portIn=\"input\" portOut=\"Failure\" enabled=\"1\"/></Edges><GraphTokens/></Graph>",
            g_orderName, g_orderName, nodeType, settings, g_orderName, g_orderName, targetEdge);
        Log("[companion-order] action=%d module=%s npc=%s player=%s node=%s settings=%s previous-leader=%llu",
            action, g_orderName, g_orderNpc, g_orderPlayer, nodeType, settings, g_leaderRequestedNpc);
        g_actionStage = "assignment module load";
        if (!EnsureLocalXmlHook()) return;
        if (trial) g_trialUsed = true; // a failed load must never allow another experimental order
        const auto* retained = g_payloads.add(draft);
        if (!retained) { strcpy_s(g_status, "Данные команды не удалось сохранить; native вызовы отменены."); return; }
        InterlockedExchange(&g_orderActions[count], action);
        g_diagnostics.reserve(count);
        InterlockedExchange(&g_orderCount, count + 1); // reserve even if native loading fails; never reuse the old payload
        const uintptr_t manager = VCall<uintptr_t>(*g_flowSystem, 0xC0);
        if (!manager || !VCall<uintptr_t>(manager, 0x60, retained->name, retained->path, 0)) {
            g_diagnostics.loadFailed(count);
            strcpy_s(g_status, "Модуль команды не загрузился; его данные сохранены, повтор запрещён."); return;
        }
        g_executingPayload = retained;
        g_orderExecuting = true;
        if (trial) {
            g_trialUsed = true; g_trialNpc = npc; g_trialStarted = GetTickCount64();
            g_trialObservation = {player, EntityPointerById(npc), g_trialComponent};
            Log("[companion-trial] one-shot no-leader follow started; combat disabled; NPC=%llu", npc);
        }
        g_actionStage = "assignment execute handler";
        g_executeModule(const_cast<void**>(kArgs));
        g_orderExecuting = false;
        if (action == 6) {
            g_leaderRequestedNpc = npc;
            g_leaderNpcFaction = g_factionGet(buddy);
            g_leaderPlayerFaction = faction;
        }
        Log("[companion] request action=%d npc=%s player=%s module=%s; NOT confirmed by handler return", action, g_orderNpc, g_orderPlayer, g_orderName);
        strcpy_s(g_status, "Запрос отправлен. Проверьте поведение NPC; принятие ещё не подтверждено.");
    } __except (LogActionFault(GetExceptionInformation(), g_actionStage)) {
        g_orderExecuting = false; g_disabled = true;
        Log("[companion] fault during order; disabled for this session");
        strcpy_s(g_status, "Ошибка интерфейса: дальнейшие команды отключены.");
    }
}
static void InspectModulePorts(uintptr_t module, bool isSchema = false) {
    if (!g_nodeConfigAbi || !module) return;
    const uint8_t* getter = reinterpret_cast<const uint8_t*>(Rd<uintptr_t>(Rd<uintptr_t>(module) + 0x30));
    if (!InText(getter, 5) || !BytesMatch(getter, "48 8B 41 28 C3")) {
        Log("[companion] root graph getter signature mismatch; schema inspection skipped");
        return;
    }
    const uintptr_t graph = VCall<uintptr_t>(module, 0x30);
    if (!graph) { Log("[companion] schema root graph missing"); return; }
    // Current native propagation walks the graph's six-byte link vector.
    const uintptr_t links = Rd<uintptr_t>(graph + 0x110), linksEnd = Rd<uintptr_t>(graph + 0x118);
    if (linksEnd >= links && (linksEnd - links) % 6 == 0 && linksEnd - links <= 6 * 64)
        Log("[companion-schema] root links=%zu", (linksEnd - links) / 6);
    // Native CreateNode uses graph+0xE0 and 48*nodeId; inspected current binary.
    const uintptr_t begin = Rd<uintptr_t>(graph + 0xE0), end = Rd<uintptr_t>(graph + 0xE8);
    if (!begin || end < begin || (end - begin) % 48 || end - begin > 48 * 64) {
        Log("[companion] schema node array bounds invalid; inspection skipped"); return;
    }
    static const char* labels[] = {"AssignBuddyLeader", "FollowAtDistance", "DefendTarget", "Idle", "MoveTo", "DebugLog"};
    static const char* probeLabels[] = {"ModuleStart", "DebugLog", "ModuleEnd"};
    for (size_t i = 0; i < (end - begin) / 48; ++i) {
        const uintptr_t node = Rd<uintptr_t>(begin + i * 48 + 0x18);
        if (!node) continue;
        const uint8_t* configFn = reinterpret_cast<const uint8_t*>(Rd<uintptr_t>(Rd<uintptr_t>(node) + 0x20));
        if (!InText(configFn)) { Log("[companion] node config outside executable; skipped"); continue; }
        alignas(8) uint8_t config[128] = {};
        VCall<void>(node, 0x20, static_cast<void*>(config));
        const char* label = isSchema ? (i < 6 ? labels[i] : "extra") : (i < 3 ? probeLabels[i] : "extra");
        if (!strcmp(label, "DebugLog") && !g_debugEventOrig) {
            uint8_t* slot = reinterpret_cast<uint8_t*>(Rd<uintptr_t>(node) + 0x38);
            const uint8_t* eventFn = reinterpret_cast<const uint8_t*>(Rd<uintptr_t>(reinterpret_cast<uintptr_t>(slot)));
            if (InText(eventFn, 0x40) && BytesMatch(eventFn,
                "83 FA 01 75 5E 53 48 83 EC 20 49 8B 48 20 49 8B D8 E8 ?? ?? ?? ?? 84 C0 74 44 48 8B 05 ?? ?? ?? ?? 83 78 0C 00")) {
                const uint8_t* activeFn = eventFn + 0x16 + Rel32(eventFn + 0x12);
                if (InText(activeFn, 7) && BytesMatch(activeFn, "0F B6 41 20 24 01 C3")) {
                    g_debugLogGate = reinterpret_cast<uintptr_t>(eventFn + 0x21 + Rel32(eventFn + 0x1D));
                    g_debugEventOrig = reinterpret_cast<DebugEventFn>(const_cast<uint8_t*>(eventFn));
                    const uintptr_t hook = reinterpret_cast<uintptr_t>(&DebugEventObserved);
                    DWORD err = 0;
                    if (!WriteCode(slot, reinterpret_cast<const uint8_t*>(&hook), sizeof(hook), err)) {
                        g_debugEventOrig = nullptr;
                        Log("[companion] Debug:Log observation unavailable (%lu)", err);
                    } else Log("[companion] native Debug:Log observation installed; original behavior retained");
                }
            }
        }
        const uintptr_t imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        Log("[companion-schema] node %zu %s flags=%08X config-rva=%llX", i, label,
            *reinterpret_cast<const uint32_t*>(config + 16),
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(configFn) - imageBase));
        const uintptr_t inputs = *reinterpret_cast<const uintptr_t*>(config);
        for (size_t p = 0; inputs && p < 32; ++p) {
            const uintptr_t port = inputs + p * 72;
            const char* name = Rd<const char*>(port);
            if (!name) break;
            Log("[companion-schema] %s input[%zu]=%.100s default-tag=%u data=%016llX", label, p, name,
                Rd<uint32_t>(port + 56), static_cast<unsigned long long>(Rd<uint64_t>(port + 32)));
        }
        // Current native XML parser (17d3e69..17d3e92) reads config+8,
        // then indexes name pointers with shl 5: 32-byte output descriptors.
        const uintptr_t outputs = *reinterpret_cast<const uintptr_t*>(config + 8);
        for (size_t p = 0; outputs && p < 32; ++p) {
            const uintptr_t port = outputs + p * 32;
            if (!ReadablePointer(port, 32)) break;
            const char* name = Rd<const char*>(port);
            if (!name || !ReadablePointer(reinterpret_cast<uintptr_t>(name), 100)) break;
            Log("[companion-schema] %s output[%zu]=%.100s", label, p, name);
        }
    }
}

// Read the definition registry on the game thread. No native lookup, constructor,
// assignment, graph command, or write to client memory is performed here.
static void ProbeBuddyDefinition() {
    __try {
        if (!ReadablePointer(g_buddyEnvironmentSlot, 8)) {
            Log("[companion-buddy-data] unavailable: environment signature/slot not verified"); return;
        }
        const uintptr_t env = Rd<uintptr_t>(g_buddyEnvironmentSlot);
        if (!ReadablePointer(env, 0xC68)) { Log("[companion-buddy-data] unavailable: environment unreadable"); return; }
        const uintptr_t hub = Rd<uintptr_t>(env + 0xC60);
        if (!ReadablePointer(hub, 0x68)) { Log("[companion-buddy-data] unavailable: library hub not ready"); return; }
        const uintptr_t map = Rd<uintptr_t>(hub + 0x60);
        if (!ReadablePointer(map, 0x20)) { Log("[companion-buddy-data] unavailable: ignore-list registry not ready"); return; }
        // Current name/key lookup reads controls, node-pointer slots and mask at
        // offsets 0, 8, 0x18. The terminal sentinel is excluded from the scan.
        const uintptr_t controls = Rd<uintptr_t>(map);
        const uintptr_t slots = Rd<uintptr_t>(map + 8);
        const uint64_t mask = Rd<uint64_t>(map + 0x18);
        if (!mask || mask > 65535 || (mask & (mask + 1)) != 0 ||
            !ReadablePointer(controls, static_cast<size_t>(mask + 1)) ||
            !ReadablePointer(slots, static_cast<size_t>(mask * 8)) ||
            Rd<uint8_t>(controls + static_cast<uintptr_t>(mask)) != 0xFF) {
            Log("[companion-buddy-data] unavailable: bounded registry layout check failed (mask=%llu)", static_cast<unsigned long long>(mask)); return;
        }
        unsigned occupied = 0, unreadable = 0, found = 0;
        uint64_t keyLo = 0, keyHi = 0;
        for (uint64_t i = 0; i < mask; ++i) {
            if (Rd<uint8_t>(controls + static_cast<uintptr_t>(i)) >= 0x80) continue;
            ++occupied;
            const uintptr_t record = Rd<uintptr_t>(slots + static_cast<uintptr_t>(i * 8));
            if (!ReadablePointer(record, 0x18)) { ++unreadable; continue; }
            const uintptr_t name = Rd<uintptr_t>(record + 0x10);
            char label[128] = {};
            bool terminated = false;
            for (size_t j = 0; j < sizeof(label) - 1; ++j) {
                if (!ReadablePointer(name + j, 1)) break;
                label[j] = Rd<char>(name + j);
                if (!label[j]) { terminated = true; break; }
            }
            if (!terminated) { ++unreadable; continue; }
            if (!strcmp(label, "DisturbanceIgnoreList_BuddyAI")) {
                ++found; keyLo = Rd<uint64_t>(record); keyHi = Rd<uint64_t>(record + 8);
            }
        }
        if (Rd<uintptr_t>(map) != controls || Rd<uintptr_t>(map + 8) != slots || Rd<uint64_t>(map + 0x18) != mask) {
            Log("[companion-buddy-data] inconclusive: registry changed during read"); return;
        }
        Log("[companion-buddy-data] registry slots=%llu occupied=%u unreadable=%u BuddyAI matches=%u key=%016llX:%016llX; no NPC command sent",
            static_cast<unsigned long long>(mask), occupied, unreadable, found,
            static_cast<unsigned long long>(keyHi), static_cast<unsigned long long>(keyLo));
        if (!found) Log("[companion-buddy-data] BuddyAI %s; absence reason not established", unreadable ? "not found in readable entries (inconclusive)" : "absent from inspected registry");
    } __except (LogActionFault(GetExceptionInformation(), "buddy-definition-read-only")) {
        Log("[companion-buddy-data] inconclusive: read fault; NPC commands remain disabled");
    }
}

static void RunProbe() {
    if (g_disabled || !g_flowSystem || !g_executeModule) return;
    __try {
        const uintptr_t flow = *g_flowSystem;
        if (!flow) { Log("[companion] flow system not ready"); return; }
        const uintptr_t manager = VCall<uintptr_t>(flow, 0xC0);
        if (!manager) { Log("[companion] module manager not ready"); return; }
        const uintptr_t vt = Rd<uintptr_t>(manager);
        const uint8_t* load = reinterpret_cast<const uint8_t*>(Rd<uintptr_t>(vt + 0x60));
        const uint8_t* get = reinterpret_cast<const uint8_t*>(Rd<uintptr_t>(vt + 0x70));
        if (!InText(load, 40) || !InText(get, 32) ||
            !BytesMatch(load, "48 89 5C 24 20 55 41 56 41 57 48 83 EC 20 48 8B 01 4D 8B F8") ||
            !BytesMatch(get, "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B F1")) {
            Log("[companion] manager load/get signatures changed; disabled");
            strcpy_s(g_status, "Загрузка модуля отключена: интерфейс изменился.");
            g_disabled = true;
            return;
        }
        g_managerVerified = true;
        ProbeBuddyDefinition();
        // Registry lookup only: a Subsumption task/event name does not prove a FlowGraph node exists.
        if (g_getType) {
            const uint16_t cancelType = g_getType(flow, "AI:Assignment:CancelAssignments");
            Log("[companion-cancel] FlowGraph registry lookup AI:Assignment:CancelAssignments -> %u; no task executed", cancelType);
            Log("[companion-cancel] native AI_EvtControl_CancelAssignments handler found by offline analysis; invocation ABI/lifecycle not runtime-verified");
        }
        if (!g_createInstanceOrig) {
            uint8_t* slot = reinterpret_cast<uint8_t*>(vt + 0x88);
            const uint8_t* fn = reinterpret_cast<const uint8_t*>(Rd<uintptr_t>(reinterpret_cast<uintptr_t>(slot)));
            // Console handler calls this six-argument wrapper; verified current PE.
            if (InText(fn, 55) && BytesMatch(fn,
                "48 83 EC 48 48 8B 44 24 78 4D 8B D1 48 89 44 24 38 41 B9 FF FF 00 00 48 8B 44 24 70")) {
                g_createInstanceOrig = reinterpret_cast<CreateInstanceFn>(const_cast<uint8_t*>(fn));
                const uintptr_t hook = reinterpret_cast<uintptr_t>(&CreateInstanceObserved);
                DWORD err = 0;
                if (!WriteCode(slot, reinterpret_cast<const uint8_t*>(&hook), sizeof(hook), err)) {
                    g_createInstanceOrig = nullptr;
                    Log("[companion] create-instance observation unavailable (%lu)", err);
                } else Log("[companion] native create-instance observation installed; original behavior retained");
            }
        }
        // Read-only type lookup, validated against the function used by native XML loading.
        if (g_getType) {
            static const char* names[] = { "Debug:Log", "AI:Assignment:AssignBuddyLeader",
                "AI:Assignment:AssignmentFollowAtDistance", "AI:Assignment:AssignmentDefendTarget",
                "AI:Assignment:AssignmentIdle", "AI:Assignment:AssignmentCharacterMoveTo" };
            for (const char* name : names) {
                const uint16_t id = g_getType(flow, name);
                Log("[companion] node type %s: %u%s", name, static_cast<unsigned>(id), id ? "" : " (not registered)");
            }
        } else Log("[companion] native type lookup not uniquely resolved; inspection skipped");
        uintptr_t module = VCall<uintptr_t>(manager, 0x70, kName);
        Log("[companion] probe registered before load: %s", module ? "yes" : "no");
        if (!module) {
            if (!EnsureLocalXmlHook()) {
                Log("[companion] local XML hook unavailable; probe deferred");
                return;
            }
            module = VCall<uintptr_t>(manager, 0x60, kName, kPath, 0);
        }
        const uintptr_t registered = VCall<uintptr_t>(manager, 0x70, kName);
        if (!module || !registered) {
            Log("[companion] explicit probe load failed (file: %s); no NPC assignments", kPath);
            strcpy_s(g_status, "Пробный модуль не загрузился. Подробности в журналах.");
            return;
        }
        Log("[companion] explicit probe load returned a registered module; invoking native execute handler");
        InspectModulePorts(registered);
        if (g_getType && g_getType(flow, "AI:Assignment:AssignBuddyLeader") &&
            g_getType(flow, "AI:Assignment:AssignmentFollowAtDistance") &&
            g_getType(flow, "AI:Assignment:AssignmentDefendTarget") &&
            g_getType(flow, "AI:Assignment:AssignmentIdle") &&
            g_getType(flow, "AI:Assignment:AssignmentCharacterMoveTo")) {
            uintptr_t schema = VCall<uintptr_t>(manager, 0x70, kSchemaName);
            if (!schema) schema = VCall<uintptr_t>(manager, 0x60, kSchemaName, kSchemaPath, 0);
            Log("[companion] disconnected schema module loaded: %s (not executed)", schema ? "yes" : "no");
            InspectModulePorts(schema, true);
        }
        g_probeExecuting = true;
        g_executeModule(const_cast<void**>(kArgs));
        g_probeExecuting = false;
        Log("[companion] execute handler returned; verify SCO_COMPANION_PROBE_EXECUTED in Game.log");
        strcpy_s(g_status, "Модуль зарегистрирован. Выполнение нужно подтвердить по журналу.");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_probeExecuting = false;
        g_disabled = true;
        Log("[companion] fault in module bridge; disabled, no NPC assignments");
        strcpy_s(g_status, "Ошибка интерфейса модулей; повторные вызовы отключены.");
    }
}

void Menu_RequestCompanionProbe() { InterlockedExchange(&g_probeRequested, 1); }
void Menu_ConfirmCompanionPeaceful(bool peaceful) {
    g_peacefulNpc = peaceful ? LastSpawnedNpcId() : 0;
    if (!peaceful) g_leaderRequestedNpc = 0;
    Log("[companion] peaceful observation confirmed=%d NPC=%llu", peaceful, static_cast<unsigned long long>(g_peacefulNpc));
}
void Menu_RequestCompanionAction(int action) {
    if ((action >= 1 && action <= 5) || action == 7 || action == 9) {
        const companion::Request request{action, LastSpawnedNpcId(), LocalPlayerEntityId()};
        const auto replaced = g_requests.publish(request);
        Log("[companion-queue] action=%d replaced-pending=%d selected-last-NPC=%llu player=%llu tick=%llu", action, replaced.action, request.npc, request.player, GetTickCount64());
    }
}
const char* Menu_CompanionBridgeStatus() { return g_status; }
const char* Menu_CompanionResultStatus() {
    static char summary[512] = {};
    const LONG count = InterlockedCompareExchange(&g_orderCount, 0, 0);
    if (count <= 0 || count > 16) return InterlockedCompareExchange(&g_probeObserved, 0, 0) ?
        "Тестовый граф исполняется. Результатов команд NPC пока нет." : "";
    const LONG action = InterlockedCompareExchange(&g_orderActions[count - 1], 0, 0);
    const unsigned result = g_diagnostics.result(static_cast<unsigned>(count - 1));
    const char* name = action == 6 ? "Назначение лидера" : (action == 2 || action == 9) ? "Следование" :
        action == 3 ? "Ожидание" : action == 4 ? "Охрана" : "Возврат";
    const char* outcome = result & companion::LoadFailed ? "загрузка модуля не удалась; данные удерживаются до выхода." :
        result == 3 ? "наблюдались Success и Failure; порядок/завершение задания проверяются отдельно." :
        result == 1 ? "штатный узел выдал Success. Проверьте поведение NPC." :
        result == 2 ? "штатный узел выдал Failure. Возможны отказ или отмена задания." :
        "результат ещё не получен. Длительное задание может оставаться активным.";
    sprintf_s(summary, "Команда %ld — %s: %s", count, name, outcome);
    return summary;
}
void ProcessCompanionBridge() {
    static bool started = false, automaticDone = false;
    static ULONGLONG first = 0;
    const ULONGLONG now = GetTickCount64();
    if (!started) { started = true; first = now; }
    const bool requested = InterlockedExchange(&g_probeRequested, 0) != 0;
    const auto request = g_requests.consume();
    if (request.action) RunAction(request);
    ProcessFollowTrial(now);
    // Read-only automatic sample after a newly spawned NPC has had time to stream in.
    // Never assigns a leader, changes a faction, or issues a movement/combat order.
    static uint64_t observedNpc = 0;
    static ULONGLONG observedAt = 0;
    static bool relationshipRead = false;
    const uint64_t currentNpc = LastSpawnedNpcId();
    if (currentNpc != observedNpc) { observedNpc = currentNpc; observedAt = now; relationshipRead = false; }
    if (!g_disabled && g_managerVerified && observedNpc && !relationshipRead && now - observedAt >= 5000 && EntityAlive(observedNpc)) {
        relationshipRead = true;
        RunAction({7, observedNpc, LocalPlayerEntityId()});
    }
    if (requested || (!automaticDone && now - first >= 30000)) {
        automaticDone = true;
        RunProbe();
    }
}

