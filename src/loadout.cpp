#include "loadout.h"
#include "teleport.h"
#include "menu.h"
#include "spawner.h"
#include "sco/game/actors.h"
#include "sco/signatures.h"
#include <share.h>
#include <string>

static struct {
    bool       ok = false;
    uintptr_t* game = nullptr;
    int32_t    frameworkSlot = 0;
    int32_t    actorSlot = 0;
    int32_t    loadSlot = 0;
} g_lo;

namespace actors = sco::game::actors;

// The player loadout loader is sco-core's loadout.* rows (sco/game/actors.h); its slots are read
// from the instructions that row checks.
bool ResolveLoadoutApi(const Section&, const Section&) {
    if (!ActorsCapability("loadout.loader", "gear", "gear menu and outfits")) return false;
    const uint8_t* h = sco::Sig("loadout.load_player_loadout");
    g_lo.game          = reinterpret_cast<uintptr_t*>(sco::Sig("loadout.game"));
    g_lo.frameworkSlot = Rel32(h + actors::kLoadoutFrameworkSlot);
    g_lo.actorSlot     = Rel32(h + actors::kLoadoutActorSlot);
    g_lo.loadSlot      = Rel32(h + actors::kLoadoutLoadSlot);
    g_lo.ok = true;
    return true;
}

// items.txt's lists: the ten gear slots and, apart from them, the attachment and multitool lists
// (gear_attachments.h; attachments after Alpha-Rome0's PR #127, integration by Claude for Local5).
static gear::Catalog g_catalog;
static volatile LONG g_gearState = 0;
static SRWLOCK       g_gearLock = SRWLOCK_INIT;
static struct { bool pending; int picks[Gear_SlotCount]; MenuWeaponAttachments attachments; } g_equipRequest;

int Menu_GearCount(int slot) {
    if (g_gearState != 2) { InterlockedCompareExchange(&g_gearState, 1, 0); return -1; }
    return gear::GearCount(g_catalog, slot);
}

const char* Menu_GearName(int slot, int index) {
    return g_gearState == 2 ? gear::GearName(g_catalog, slot, index) : "";
}

int Menu_GearExtraCount(int list) {
    if (g_gearState != 2) { InterlockedCompareExchange(&g_gearState, 1, 0); return -1; }
    return gear::ExtraCount(g_catalog, list);
}

const char* Menu_GearExtraName(int list, int index) {
    return g_gearState == 2 ? gear::ExtraName(g_catalog, list, index) : "";
}

void Menu_RequestEquip(const int picks[Gear_SlotCount], const MenuWeaponAttachments* attachments) {
    AcquireSRWLockExclusive(&g_gearLock);
    g_equipRequest.pending = true;
    memcpy(g_equipRequest.picks, picks, sizeof(g_equipRequest.picks));
    g_equipRequest.attachments = attachments ? *attachments : MenuWeaponAttachments{};
    ReleaseSRWLockExclusive(&g_gearLock);
}

static int BuildGearLists() {
    char path[MAX_PATH];
    if (!DataFilePath(path, sizeof(path), "items.txt")) return 0;
    FILE* f = _fsopen(path, "r", _SH_DENYNO);
    if (!f) { Log("[gear] can't open %s", path); return 0; }
    const uintptr_t registry = VCall<uintptr_t>(*g_tp.entitySystem, actors::kEsClassRegistry);
    // The class check is the only game call; it proves a class exists, not that it fits a weapon.
    const auto exists = [registry](const char* name) {
        return VCall<uintptr_t>(registry, actors::kRegistryFindClass, name) != 0;
    };
    gear::Begin(g_catalog);
    char line[128];
    while (fgets(line, sizeof(line), f)) gear::ParseLine(g_catalog, line, exists);
    fclose(f);
    const gear::Catalog& c = g_catalog;
    Log("[gear] %d items in the gear menu; %d optics, %d barrels, %d underbarrels, %d multitool modules, "
        "%d canisters, %d batteries (%d unknown names skipped, %d malformed extra names)",
        c.gearTotal, c.extraCount[gear::Extra_Optic], c.extraCount[gear::Extra_Barrel], c.extraCount[gear::Extra_Underbarrel],
        c.extraCount[gear::Extra_ToolModule], c.extraCount[gear::Extra_ToolCanister], c.extraCount[gear::Extra_ToolBattery],
        c.unknown, c.rejected);
    return c.gearTotal;
}

bool LoadoutApiReady() { return g_lo.ok; }

static volatile LONG g_s42VisorHud = 0;
void Menu_SetS42VisorHud(bool on) { InterlockedExchange(&g_s42VisorHud, on ? 1 : 0); }

std::string LoadoutItem(const char* port, const char* name, const std::string& children) {
    return gear::Item(port, name, children);
}

// The player's head, as the original builds it (orig 1800265d0). A head item is always
// emitted: with no Head_ItemPort the player gets the default PU face, otherwise the named
// head gets PU_Head_Eyes / PU_Head_Teeth when the outfit names none. The lens always sits
// under the eyes; Default_LensDisplay is the SQ42 visor HUD the Settings checkbox picks.
// Accessories (hat, eye/head accessories, horns, jewellery) go under the head in both cases.
std::string HeadItem(const HeadParts& p) {
    const char* lens = InterlockedCompareExchange(&g_s42VisorHud, 0, 0) != 0
                     ? "Default_LensDisplay" : "Default_LensDisplay_PU";
    if (!p.head)
        return LoadoutItem("Head_ItemPort", "PU_Protos_Head",
                   LoadoutItem("Eyes_ItemPort", "Head_Eyes_Blue_01", LoadoutItem("Lens_ItemPort", lens))
                   + "<Item portName=\"Teeth_ItemPort\" itemName=\"Head_Teeth\" tag=\"Char_Accessory_Head\"/>"
                   + "<Item portName=\"Hair_ItemPort\" itemName=\"hair_37\" tag=\"Char_Head_Hair Male\"><Items>"
                   + LoadoutItem("Material_Variant", "Hair_Var_Brown") + "</Items></Item>"
                   + p.accessories);
    std::string children = LoadoutItem("Eyes_ItemPort", p.eyes ? p.eyes : "PU_Head_Eyes", LoadoutItem("Lens_ItemPort", lens))
                         + LoadoutItem("Teeth_ItemPort", p.teeth ? p.teeth : "PU_Head_Teeth");
    if (p.eyebrow) children += LoadoutItem("Eyebrow_ItemPort", p.eyebrow);
    if (p.hair)    children += LoadoutItem("Hair_ItemPort", p.hair, p.hairColor ? LoadoutItem("Material_Variant", p.hairColor) : std::string());
    return LoadoutItem("Head_ItemPort", p.head, children + p.accessories);
}

// MobiGlas sits under the body in both the gear menu and outfits.
std::string MobiGlasItem() {
    return LoadoutItem("mobiglas_attach", "MobiGlas",
               "<Item portName=\"mobiglas_screen_attach\" itemName=\"PersonalMobiGlas_PU\" tag=\"MobiGlas\"/>"
               "<Item portName=\"legacy_mobiglas_screen_attach\" itemName=\"LegacyMobiGlas\" tag=\"MobiGlas\"/>");
}

static const char* LoadLoadout(const char* gamePath) {
    __try {
        const uintptr_t framework = VCall<uintptr_t>(*g_lo.game, g_lo.frameworkSlot);
        const uintptr_t actor = framework ? VCall<uintptr_t>(framework, g_lo.actorSlot) : 0;
        if (!actor) return "вы ещё не появились в игровом мире";
        VCall<void>(actor, g_lo.loadSlot, gamePath, 8u, true);
        return nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "ошибка загрузки снаряжения";
    }
}

// Game thread only (ProcessLoadout / ProcessOutfits): writes the XML into SC_USER and has the
// player's actor load it. One counter and one "last file" for both callers, so the gear menu
// and the outfit menu can't hand out the same file name or delete each other's current file.
static std::string g_lastLocalXml;
static uint64_t g_lastLocalVersion=0;
uint32_t CopyLastLocalLoadout(char* out,uint32_t capacity,uint64_t* version){
 if(version)*version=g_lastLocalVersion;
 const auto n=static_cast<uint32_t>(g_lastLocalXml.size());
 if(out && capacity>=n)memcpy(out,g_lastLocalXml.data(),n);return n;
}
void EquipLoadoutXml(const std::string& xml, const char* tag) {
    static int  counter = 0;
    static char lastFile[MAX_PATH] = "";
    char dir[MAX_PATH];
    const DWORD n = GetEnvironmentVariableA("SC_USER", dir, sizeof(dir));
    if (n == 0 || n >= sizeof(dir)) { Log("[%s] SC_USER isn't set (start the game with sc-offline.exe)", tag); return; }
    char name[64], file[MAX_PATH], gamePath[96];
    snprintf(name, sizeof(name), "offline_loadout_%lu_%d.xml", GetCurrentProcessId(), ++counter);
    snprintf(file, sizeof(file), "%s\\%s", dir, name);
    snprintf(gamePath, sizeof(gamePath), "%%USER%%/%s", name);
    FILE* f = _fsopen(file, "w", _SH_DENYNO);
    if (!f) { Log("[%s] can't write %s", tag, file); return; }
    fwrite(xml.data(), 1, xml.size(), f);
    fclose(f);
    if (const char* err = LoadLoadout(gamePath)) { Log("[%s] equip failed: %s", tag, err); return; }
    g_lastLocalXml=xml;++g_lastLocalVersion;
    Log("[%s] equipped (%s)", tag, name);
    if (lastFile[0]) DeleteFileA(lastFile);
    strcpy_s(lastFile, file);
}

// The gear menu's XML (gear_attachments.h): byte-identical to the ten-slot builder when no
// attachment or multitool part is chosen. Notes say what the builder had to leave out.
static void EquipPicks(const int picks[Gear_SlotCount], const MenuWeaponAttachments& attachments) {
    gear::BuildNotes notes;
    EquipLoadoutXml(gear::GearXml(g_catalog, picks, attachments, HeadItem(HeadParts{}) + MobiGlasItem(), &notes), "gear");
    if (notes.primaryToolLegacy)
        SetMenuStatus("Мультитул основного оружия надет без модуля: порт utility_attach_1 занят мультитулом пистолета.");
    if (notes.baseMultitoolMissing)
        SetMenuStatus("Модуль или картридж не применён: базового класса grin_multitool_01 нет в списке снаряжения; оставлен SRT по умолчанию.");
    if (notes.srtModuleMissing)
        SetMenuStatus("Картридж не применён: модуля SRT нет в разделе [toolmodule]; оставлен SRT по умолчанию.");
    if (notes.noBattery)
        Log("[gear] multitool without a battery: [toolbattery] is empty or unknown to the game");
}

void ProcessLoadout() {
    if (!g_lo.ok || !g_tp.ok) return;
    if (g_gearState == 1) {
        uintptr_t actor, entity;
        bool live = false;
        __try { live = GetLocalPlayer(actor, entity); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (live) {
            __try { BuildGearLists(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("[gear] fault while reading items.txt"); }
            InterlockedExchange(&g_gearState, 2);
        }
    }
    AcquireSRWLockExclusive(&g_gearLock);
    const auto req = g_equipRequest;
    g_equipRequest.pending = false;
    ReleaseSRWLockExclusive(&g_gearLock);
    if (req.pending && g_gearState == 2) EquipPicks(req.picks, req.attachments);
}
