#pragma once
// The Player gear menu without the game: items.txt sections, attachment and multitool picks, and
// the loadout XML that loadout.cpp hands to the game's loader.
//
// Provenance. The model is Alpha-Rome0's sc-offline PR #127 (commit 7ea9731, "Add firearm and
// multitool gear attachments", GPL-3.0): the [optic]/[barrel]/[underbarrel]/[toolmodule]/
// [toolcanister]/[toolbattery] sections, the attachment ports and tags, the multitool on
// utility_attach_1 with its battery, module and SRT canister, and the default-SRT rule. This pure
// header, the shared name pool, the selection rules for a missing/invalid/incompatible pick, the
// legacy-identical XML when nothing new is chosen and the payload check are Claude's Local5
// integration (2026-10-13). The ten gear slots, Pick, MatchingMag and the XML layout are
// upstream sc-offline (scubamount, 0.7.7) code moved here unchanged in behaviour.
//
// Pure: no Windows, no game calls. Class names are checked against the game by the caller's
// "exists" function while parsing; nothing here proves that an attachment fits a weapon.
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

enum MenuGearSlot { Gear_Undersuit, Gear_Helmet, Gear_Torso, Gear_Arms, Gear_Legs, Gear_Backpack,
                    Gear_Primary, Gear_Sidearm, Gear_Ammo, Gear_Grenade, Gear_SlotCount };
enum MenuAttachmentSlot { Attachment_Optic, Attachment_Barrel, Attachment_Underbarrel, Attachment_SlotCount };

// Indices into the extra lists (gear::ExtraList); -1 is "none": the weapon class's own default.
// POD, so it crosses from the menu to the game thread under the gear lock like the gear picks.
struct MenuWeaponAttachments {
    int primary[Attachment_SlotCount] = { -1, -1, -1 };
    int sidearm[Attachment_SlotCount] = { -1, -1, -1 };
    int primaryToolModule = -1;
    int primaryToolCanister = -1;
    int sidearmToolModule = -1;
    int sidearmToolCanister = -1;
};

namespace gear {

// Lists kept apart from the ten gear slots: loadout.equip and the gear combos never see them.
enum ExtraList { Extra_Optic, Extra_Barrel, Extra_Underbarrel, Extra_ToolModule, Extra_ToolCanister,
                 Extra_ToolBattery, Extra_Count };
static_assert(int(Extra_Optic) == int(Attachment_Optic) && int(Extra_Barrel) == int(Attachment_Barrel)
              && int(Extra_Underbarrel) == int(Attachment_Underbarrel), "attachment slot = extra list");

constexpr int kMaxGear      = 3000;   // the ten gear slots, as before
constexpr int kMaxExtra     = 512;    // all extra lists together
constexpr int kNameSize     = 64;     // class names longer than 63 bytes are cut, as before
constexpr int kSectionNone  = -1;     // an unknown [section]: its lines are skipped, as before
constexpr int kSectionExtra = 100;    // kSectionExtra + ExtraList

inline const char* const kGearSections[Gear_SlotCount] = {
    "undersuit", "helmet", "torso", "arms", "legs", "backpack", "primary", "sidearm", "ammo", "grenade" };
inline const char* const kExtraSections[Extra_Count] = {
    "optic", "barrel", "underbarrel", "toolmodule", "toolcanister", "toolbattery" };

inline const char* const kAttachmentPorts[Attachment_SlotCount] = { "optics_attach", "barrel_attach", "underbarrel_attach" };
inline const char* const kAttachmentTags[Attachment_SlotCount] = {
    "WeaponAttachment(IronSight)", "WeaponAttachment(Barrel)", "WeaponAttachment(BottomAttachment)" };
constexpr const char* kMultitoolPort    = "utility_attach_1";
constexpr const char* kModulePort       = "module_attach";
constexpr const char* kModuleTag        = "WeaponAttachment(Utility)";
constexpr const char* kCanisterPort     = "canister_attach";
constexpr const char* kCanisterTag      = "WeaponAttachment(Magazine)";
constexpr const char* kBatteryPort      = "magazine_attach";
constexpr const char* kBaseMultitool    = "grin_multitool_01";
constexpr const char* kDefaultSrtTool   = "grin_multitool_01_default_salvage_repair";
constexpr const char* kSrtModule        = "grin_multitool_01_salvage_repair";

// ---- ASCII helpers (class names are ASCII; _strnicmp semantics) ---------------------------------
inline char Lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
inline bool IEqualsN(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const char x = Lower(a[i]), y = Lower(b[i]);
        if (x != y) return false;
        if (!x) return true;
    }
    return true;
}
inline bool IEquals(const char* a, const char* b) {
    if (!a || !b) return false;
    for (;; ++a, ++b) {
        if (Lower(*a) != Lower(*b)) return false;
        if (!*a) return true;
    }
}
// "prefix" alone or "prefix_..." (so grin_tractor matches grin_tractor_01, not grin_tractorx).
inline bool HasPrefixToken(const char* name, const char* prefix) {
    const size_t n = strlen(prefix);
    return name && IEqualsN(name, prefix, n) && strlen(name) >= n && (name[n] == '\0' || name[n] == '_');
}
// Extra-section names go into XML attributes: only plain class-name bytes are accepted there.
inline bool IsPlainClassName(const char* name) {
    if (!name || !*name) return false;
    for (const char* p = name; *p; ++p) {
        const char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            return false;
    }
    return true;
}
inline void CopyName(char (&out)[kNameSize], const char* name) {
    size_t i = 0;
    for (; name[i] && i + 1 < sizeof(out); ++i) out[i] = name[i];
    out[i] = 0;
}

// ---- items.txt ------------------------------------------------------------------------------------
struct Catalog {
    char gear[kMaxGear][kNameSize];
    int  gearStart[Gear_SlotCount];
    int  gearCount[Gear_SlotCount];
    int  gearTotal;
    char extra[kMaxExtra][kNameSize];
    int  extraStart[Extra_Count];
    int  extraCount[Extra_Count];
    int  extraTotal;
    int  unknown;    // names the game doesn't know
    int  rejected;   // extra-section names that aren't plain class names
    int  section;    // parser state: the current [section]
};

// name points at '['. "[optic]" -> kSectionExtra + Extra_Optic; the name must be followed by ']'.
inline int SectionOf(const char* name) {
    for (int s = 0; s < Gear_SlotCount; ++s) {
        const size_t n = strlen(kGearSections[s]);
        if (IEqualsN(name + 1, kGearSections[s], n) && name[1 + n] == ']') return s;
    }
    for (int e = 0; e < Extra_Count; ++e) {
        const size_t n = strlen(kExtraSections[e]);
        if (IEqualsN(name + 1, kExtraSections[e], n) && name[1 + n] == ']') return kSectionExtra + e;
    }
    return kSectionNone;
}

inline void Begin(Catalog& c) {
    memset(&c, 0, sizeof(c));
    c.section = kSectionNone;
}

// One line of items.txt (modified in place, as fgets's buffer was). exists(name) asks the game.
template <class Exists>
void ParseLine(Catalog& c, char* line, Exists&& exists) {
    line[strcspn(line, "\r\n#")] = 0;
    char* name = line + strspn(line, " \t");
    if (!*name) return;
    if (*name == '[') {
        c.section = SectionOf(name);
        if (c.section >= 0 && c.section < Gear_SlotCount) { c.gearStart[c.section] = c.gearTotal; c.gearCount[c.section] = 0; }
        if (c.section >= kSectionExtra) {
            const int e = c.section - kSectionExtra;
            c.extraStart[e] = c.extraTotal;
            c.extraCount[e] = 0;
        }
        return;
    }
    if (c.section >= 0 && c.section < Gear_SlotCount) {
        if (c.gearTotal >= kMaxGear) return;
        if (!exists(static_cast<const char*>(name))) { ++c.unknown; return; }
        CopyName(c.gear[c.gearTotal++], name);
        ++c.gearCount[c.section];
    } else if (c.section >= kSectionExtra) {
        if (c.extraTotal >= kMaxExtra) return;
        // Codex Local6: reject long extra IDs rather than silently truncating them.
        if (!IsPlainClassName(name) || strlen(name) >= kNameSize) { ++c.rejected; return; }
        if (!exists(static_cast<const char*>(name))) { ++c.unknown; return; }
        CopyName(c.extra[c.extraTotal++], name);
        ++c.extraCount[c.section - kSectionExtra];
    }
}

inline int GearCount(const Catalog& c, int slot) { return slot >= 0 && slot < Gear_SlotCount ? c.gearCount[slot] : 0; }
inline const char* GearName(const Catalog& c, int slot, int i) {
    return slot >= 0 && slot < Gear_SlotCount && i >= 0 && i < c.gearCount[slot] ? c.gear[c.gearStart[slot] + i] : "";
}
inline int ExtraCount(const Catalog& c, int list) { return list >= 0 && list < Extra_Count ? c.extraCount[list] : 0; }
inline const char* ExtraName(const Catalog& c, int list, int i) {
    return list >= 0 && list < Extra_Count && i >= 0 && i < c.extraCount[list] ? c.extra[c.extraStart[list] + i] : "";
}
// The picked class, or nullptr for none / an index the lists don't have.
inline const char* PickGear(const Catalog& c, const int picks[Gear_SlotCount], int slot) {
    const int i = picks[slot];
    return i >= 0 && i < c.gearCount[slot] ? c.gear[c.gearStart[slot] + i] : nullptr;
}
inline const char* PickExtra(const Catalog& c, int list, int i) {
    return list >= 0 && list < Extra_Count && i >= 0 && i < c.extraCount[list] ? c.extra[c.extraStart[list] + i] : nullptr;
}
inline int FindGear(const Catalog& c, int slot, const char* cls) {
    for (int i = 0; i < GearCount(c, slot); ++i) if (IEquals(GearName(c, slot, i), cls)) return i;
    return -1;
}
inline int FindExtra(const Catalog& c, int list, const char* cls) {
    for (int i = 0; i < ExtraCount(c, list); ++i) if (IEquals(ExtraName(c, list, i), cls)) return i;
    return -1;
}

// The gun's magazine from [ammo]: "<gun>_mag", then shorter by one "_part" at a time (upstream).
inline const char* MatchingMag(const Catalog& c, const char* gun) {
    if (!gun) return nullptr;
    char base[kNameSize];
    CopyName(base, gun);
    for (;;) {
        char want[72];
        snprintf(want, sizeof(want), "%s_mag", base);
        for (int i = 0; i < c.gearCount[Gear_Ammo]; ++i)
            if (IEquals(c.gear[c.gearStart[Gear_Ammo] + i], want)) return c.gear[c.gearStart[Gear_Ammo] + i];
        char* cut = strrchr(base, '_');
        if (!cut) return nullptr;
        *cut = 0;
    }
}

// ---- selection --------------------------------------------------------------------------------------
enum class WeaponKind { None, Firearm, Multitool, Tool };

inline bool IsMultitool(const char* cls) { return HasPrefixToken(cls, kBaseMultitool); }
inline bool IsDefaultSrtTool(const char* cls) { return IEquals(cls, kDefaultSrtTool); }
inline bool IsSrtModule(const char* cls) { return IEquals(cls, kSrtModule); }

// Hand tools in items.txt that aren't firearms get no optic/barrel/underbarrel. This is a list of
// what is known not to take them, not a proof that the rest do.
inline WeaponKind Classify(const char* cls) {
    if (!cls || !*cls) return WeaponKind::None;
    if (IsMultitool(cls)) return WeaponKind::Multitool;
    static const char* const kTools[] = { "crlf_medgun", "grin_salvage_repair", "grin_tractor" };
    for (const char* t : kTools) if (HasPrefixToken(cls, t)) return WeaponKind::Tool;
    return WeaponKind::Firearm;
}

// An SRT canister applies with the SRT module, or with the default SRT tool left on its default.
inline bool UsesSrtCanister(const char* weapon, const char* module) {
    return module ? IsSrtModule(module) : IsDefaultSrtTool(weapon);
}

struct Hand {
    int* firearm;
    int* module;
    int* canister;
};
inline Hand HandOf(MenuWeaponAttachments& a, bool primary) {
    return primary ? Hand{ a.primary, &a.primaryToolModule, &a.primaryToolCanister }
                   : Hand{ a.sidearm, &a.sidearmToolModule, &a.sidearmToolCanister };
}
inline void ClearHand(Hand h) {
    for (int s = 0; s < Attachment_SlotCount; ++s) h.firearm[s] = -1;
    *h.module = -1;
    *h.canister = -1;
}
inline bool HasChoices(const MenuWeaponAttachments& a, bool primary) {
    const int* firearm = primary ? a.primary : a.sidearm;
    for (int s = 0; s < Attachment_SlotCount; ++s) if (firearm[s] >= 0) return true;
    return primary ? a.primaryToolModule >= 0 || a.primaryToolCanister >= 0
                   : a.sidearmToolModule >= 0 || a.sidearmToolCanister >= 0;
}

// The game thread's rule before building XML: keep only picks the chosen weapon can use.
//  - no weapon in the hand: nothing; a firearm: optic/barrel/underbarrel only; a multitool: module
//    and canister only; a known tool (medgun, ...): nothing;
//  - an index outside its list (stale or forged): none;
//  - the default SRT tool with the SRT module and no canister is its own default: no choice;
//  - a canister without an SRT module (or the untouched default SRT tool): none.
inline void Sanitize(const Catalog& c, const int picks[Gear_SlotCount], MenuWeaponAttachments& a) {
    for (const bool primary : { true, false }) {
        const char* weapon = PickGear(c, picks, primary ? Gear_Primary : Gear_Sidearm);
        const WeaponKind kind = Classify(weapon);
        Hand h = HandOf(a, primary);
        for (int s = 0; s < Attachment_SlotCount; ++s)
            if (kind != WeaponKind::Firearm || !PickExtra(c, s, h.firearm[s])) h.firearm[s] = -1;
        if (kind != WeaponKind::Multitool) { *h.module = -1; *h.canister = -1; continue; }
        if (!PickExtra(c, Extra_ToolModule, *h.module)) *h.module = -1;
        if (!PickExtra(c, Extra_ToolCanister, *h.canister)) *h.canister = -1;
        if (IsDefaultSrtTool(weapon) && *h.module >= 0 && *h.canister < 0
            && IsSrtModule(ExtraName(c, Extra_ToolModule, *h.module)))
            *h.module = -1;
        if (!UsesSrtCanister(weapon, PickExtra(c, Extra_ToolModule, *h.module))) *h.canister = -1;
    }
}

// The menu's rule: another weapon class in a hand clears that hand's picks, so a pick made for one
// weapon never moves to another unseen. last keeps the hand's weapon between frames.
inline bool ResetOnWeaponChange(std::string& last, const char* weapon, MenuWeaponAttachments& a, bool primary) {
    const char* now = weapon ? weapon : "";
    if (IEquals(last.c_str(), now)) return false;
    last = now;
    ClearHand(HandOf(a, primary));
    return true;
}

// English product names from PR #127 (Alpha-Rome0), for search and tooltips only. Not a game
// translation; the display name comes from ReadableListName (global.ini or our fallback).
inline const char* EnglishAlias(const char* cls) {
    static const struct { const char* cls; const char* name; } kNames[] = {
        { "grin_multitool_01_mining", "OreBit Mining Attachment" },
        { "grin_multitool_01_cutter", "OxyTorch Cutter Attachment" },
        { "grin_multitool_01_tractorbeam", "TruHold Tractor Beam Attachment" },
        { "grin_multitool_01_healing", "LifeGuard Medical Attachment" },
        { "grin_multitool_01_salvage_repair", "Cambio-Lite SRT Attachment" },
        { "grin_multitool_resource_salvage_repair_01", "Cambio-Lite SRT Canister (empty)" },
        { "grin_multitool_resource_salvage_repair_01_filled", "Cambio-Lite SRT Canister (filled)" },
    };
    for (const auto& n : kNames) if (IEquals(cls, n.cls)) return n.name;
    return nullptr;
}

// ---- XML ------------------------------------------------------------------------------------------------
inline std::string Item(const char* port, const char* name, const std::string& children = std::string(), const char* tag = nullptr) {
    std::string s = std::string("<Item portName=\"") + port + "\" itemName=\"" + name + "\"";
    if (tag && *tag) s += std::string(" tag=\"") + tag + "\"";
    return children.empty() ? s + "/>" : s + "><Items>" + children + "</Items></Item>";
}

// What the builder changed or couldn't do; the caller reports it.
struct BuildNotes {
    bool primaryToolLegacy = false;     // both hands want utility_attach_1: the primary tool keeps its old port, no parts
    bool baseMultitoolMissing = false;  // the default SRT tool can't be swapped: grin_multitool_01 isn't in the gear lists
    bool srtModuleMissing = false;      // a canister needs the SRT module, which isn't in [toolmodule]
    bool noBattery = false;             // [toolbattery] is empty: the tool goes without a battery
};

// A firearm (or any gun without new choices): the matching magazine, then the chosen attachments.
inline std::string GunXml(const Catalog& c, const char* port, const char* gun, const int attachments[Attachment_SlotCount]) {
    const char* mag = MatchingMag(c, gun);
    std::string children = mag ? Item("magazine_attach", mag) : std::string();
    for (int s = 0; s < Attachment_SlotCount; ++s)
        if (const char* a = PickExtra(c, s, attachments[s])) children += Item(kAttachmentPorts[s], a, std::string(), kAttachmentTags[s]);
    return Item(port, gun, children);
}

// A multitool with a module or canister chosen (after Sanitize), on utility_attach_1. Empty when it
// can't be built; the caller then equips the tool the old way.
inline std::string MultitoolXml(const Catalog& c, const char* gun, int moduleIndex, int canisterIndex, BuildNotes& notes) {
    const char* module = PickExtra(c, Extra_ToolModule, moduleIndex);
    const char* canister = PickExtra(c, Extra_ToolCanister, canisterIndex);
    if (!module && !canister) return std::string();
    const char* tool = gun;
    if (IsDefaultSrtTool(gun)) {   // an explicit choice swaps the default SRT variant for the base tool (PR #127)
        if (FindGear(c, Gear_Sidearm, kBaseMultitool) < 0 && FindGear(c, Gear_Primary, kBaseMultitool) < 0) {
            notes.baseMultitoolMissing = true;
            return std::string();
        }
        tool = kBaseMultitool;
        if (!module) {
            const int srt = FindExtra(c, Extra_ToolModule, kSrtModule);
            if (srt < 0) { notes.srtModuleMissing = true; return std::string(); }
            module = ExtraName(c, Extra_ToolModule, srt);
        }
    }
    std::string children;
    if (ExtraCount(c, Extra_ToolBattery) > 0) children += Item(kBatteryPort, ExtraName(c, Extra_ToolBattery, 0));
    else notes.noBattery = true;
    if (module) children += Item(kModulePort, module, std::string(), kModuleTag);
    if (canister && IsSrtModule(module)) children += Item(kCanisterPort, canister, std::string(), kCanisterTag);
    return Item(kMultitoolPort, tool, children);
}

// The gear menu's loadout. With no attachment or multitool choice the bytes are those of the
// ten-slot builder before PR #127. headAndMobiGlas is HeadItem(...) + MobiGlasItem().
inline std::string GearXml(const Catalog& c, const int picks[Gear_SlotCount], MenuWeaponAttachments a,
                           const std::string& headAndMobiGlas, BuildNotes* notesOut = nullptr) {
    BuildNotes local;
    BuildNotes& notes = notesOut ? *notesOut : local;
    Sanitize(c, picks, a);
    const char* torso   = PickGear(c, picks, Gear_Torso);
    const char* legs    = PickGear(c, picks, Gear_Legs);
    const char* primary = PickGear(c, picks, Gear_Primary);
    const char* sidearm = PickGear(c, picks, Gear_Sidearm);
    const char* ammo    = PickGear(c, picks, Gear_Ammo);
    if (!ammo) ammo = MatchingMag(c, primary);

    const std::string sidearmTool = sidearm && IsMultitool(sidearm) && HasChoices(a, false)
        ? MultitoolXml(c, sidearm, a.sidearmToolModule, a.sidearmToolCanister, notes) : std::string();
    std::string primaryTool;
    if (primary && IsMultitool(primary) && HasChoices(a, true)) {
        if (sidearmTool.empty()) primaryTool = MultitoolXml(c, primary, a.primaryToolModule, a.primaryToolCanister, notes);
        else notes.primaryToolLegacy = true;
    }

    std::string onTorso, onLegs;
    if (const char* b = PickGear(c, picks, Gear_Backpack)) onTorso += Item("backpack", b);
    if (primary) {
        if (!primaryTool.empty()) onLegs += primaryTool;
        else onTorso += GunXml(c, "wep_stocked_3", primary, a.primary);
    }
    for (int i = 1; ammo && i <= (torso ? 4 : 2); ++i) onTorso += Item(("magazine_attach_" + std::to_string(i)).c_str(), ammo);
    if (const char* g = PickGear(c, picks, Gear_Grenade); g && torso)
        for (int i = 1; i <= 2; ++i) onTorso += Item(("grenade_attach_" + std::to_string(i)).c_str(), g);
    if (sidearm) onLegs += !sidearmTool.empty() ? sidearmTool : GunXml(c, "wep_sidearm", sidearm, a.sidearm);
    onLegs += Item("medPen_attach_1", "crlf_consumable_healing_01");

    std::string onSuit;
    if (const char* h = PickGear(c, picks, Gear_Helmet)) onSuit += Item("Armor_Helmet", h);
    if (torso) onSuit += Item("Armor_Torso", torso, onTorso); else onSuit += onTorso;
    if (const char* arms = PickGear(c, picks, Gear_Arms)) onSuit += Item("Armor_Arms", arms);
    if (legs) onSuit += Item("Armor_Legs", legs, onLegs); else onSuit += onLegs;

    const char* suit = PickGear(c, picks, Gear_Undersuit);
    return "<Loadout><Items>" + Item("Body_ItemPort", "body_01",
        Item("Armor_Undersuit", suit ? suit : "rsi_odyssey_undersuit_01_01_01", onSuit) + headAndMobiGlas)
        + "</Items></Loadout>\n";
}

// ---- the local plugin adapter's equip_xml / loadout_presets payload -----------------------------------
constexpr size_t kMaxLoadoutXml = 1024 * 1024;   // = SCO_STORAGE_MAX_VALUE, the presets' value limit
inline bool IsLoadoutXmlPayload(const char* xml, size_t n) {
    if (!xml || !n || n > kMaxLoadoutXml) return false;
    const std::string_view v(xml, n);
    return v.find("<Loadout") != std::string_view::npos && v.find("</Loadout>") != std::string_view::npos;
}

}  // namespace gear
