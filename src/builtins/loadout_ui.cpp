// The loadout built-in's menu tabs: Player (noclip, god mode, infinite ammo and the gear menu) and
// Squadron 42 (outfits, the SQ42 settings, the [sq42] spawn list, SQ42 ships and the console).
// Moved from menu.cpp; registered through sco.ui by loadout_plugin.cpp and drawn by the menu shell
// on the game thread. The Player tab's noclip, god mode and infinite ammo are the optional creative
// plugin's (data/plugins/creative): the checkboxes run its commands through sco_api's invoke.
#include "tabs.h"
#include "../menu.h"
#include "../menu_ui.h"
#include "../third_party/imgui/imgui.h"
#include "../build.h"
#include "../cvars.h"
#include "../spawner.h"
#include "../common.h"
#include <cstdio>
#include <cstring>
#include <string>

static const sco_api* g_api = nullptr;
static sco_plugin*    g_self = nullptr;

void RegisterPlayerTab(const sco_api* api, sco_plugin* self) {
    g_api = api;
    g_self = self;
    RegisterBuiltinTab(api, self, "loadout.player", "Игрок", kTabPlayer, DrawPlayerTab);
}

// Borrowed owner handles must not outlive the loadout plugin.
void ResetPlayerTab() { g_api = nullptr; g_self = nullptr; }

// The command's reply in the status strip (which also logs it). A refusal puts the checkbox back; a
// missing command means the creative plugin isn't loaded. SCO_FAILED (the command ran and failed)
// shows the command's own reply; SCO_UNAVAILABLE means the capability isn't on this build.
static void CreativeReply(sco_result r, const char* reply, void* ctx) {
    if (r == SCO_OK) {
        SetMenuStatus("%s", reply ? reply : "");
        return;
    }
    if (ctx) *static_cast<bool*>(ctx) = !*static_cast<bool*>(ctx);
    if (reply && reply[0]) SetMenuStatus("%s", reply);
    else if (r == SCO_NOT_FOUND) SetMenuStatus("Needs the creative plugin: turn it on in the launcher's Plugins page.");
    else if (r == SCO_UNAVAILABLE) SetMenuStatus("Not available on this game build.");
    else SetMenuStatus("The creative plugin refused (result %d).", static_cast<int>(r));
}

void InvokeCreativeToggle(const char* command, bool on, bool* flag) {
    if (!g_api || !g_self) { if (flag) *flag = !on; return; }
    sco_arg arg = {};
    arg.type = SCO_ARG_BOOL;
    arg.v.i = on ? 1 : 0;
    g_api->invoke(g_self, command, &arg, 1, CreativeReply, flag);
}

void InvokeCreativeSpeed(float metresPerSecond) {
    if (!g_api || !g_self) return;
    sco_arg arg = {};
    arg.type = SCO_ARG_FLOAT;
    arg.v.f = metresPerSecond;
    g_api->invoke(g_self, "creative.noclip_speed", &arg, 1, CreativeReply, nullptr);
}

static void GearCombo(int slot, const char* label, const char* none, int& pick, const char* filter, float width) {
    const int n = Menu_GearCount(slot);
    if (pick >= n) pick = -1;
    static char filters[Gear_SlotCount][128]{};
    const std::string preview=std::string(label)+": "+(pick>=0?ReadableListName(Menu_GearName(slot,pick)):none);
    char id[32]; snprintf(id,sizeof(id),"##gear%d",slot);
    ImGui::SetNextItemWidth(width);
    if (!ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge)) return;
    ImGui::PushID(slot);
    if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    SearchBox("##search", "Название или часть названия…",filters[slot],sizeof(filters[slot]));
    if (ImGui::Selectable(none, pick < 0)) pick = -1;
    int shown=0;
    for (int i=0;i<n;++i) {
        const char* name=Menu_GearName(slot,i);
        const char* display=ReadableListName(name);
        if(!ListMatchesFilter(name,display,filter) || !ListMatchesFilter(name,display,filters[slot])) continue;
        ++shown; ImGui::PushID(i);
        if(ImGui::Selectable(display,i==pick)) pick=i;
        ImGui::SetItemTooltip("Класс: %s",name);
        ImGui::PopID();
    }
    if(!shown) Hint("Совпадений нет. Очистите поиск в списке или общий поиск.");
    ImGui::TextDisabled("Найдено: %d / %d",shown,n);
    ImGui::PopID();
    ImGui::EndCombo();
}

// Weapon attachments and Pyro RYT multitool parts. The lists, ports and default-SRT rule follow
// Alpha-Rome0's sc-offline PR #127 (7ea9731); the Russian UI, readable names, per-list search and
// the selection rules (gear_attachments.h) are Claude's Local5 integration. A class shown here is
// known to the game; whether it fits the chosen weapon is not checked.
static void ExtraCombo(bool primary, int list, const char* label, const char* none, int& pick, float width) {
    const int n = Menu_GearExtraCount(list);
    if (pick >= n) pick = -1;
    static char filters[2][gear::Extra_Count][128]{};
    char* filter = filters[primary ? 0 : 1][list];
    const std::string preview=std::string(label)+": "+(pick>=0?ReadableListName(Menu_GearExtraName(list,pick)):none);
    char id[32]; snprintf(id,sizeof(id),"##gearx%d_%d",primary?1:0,list);
    ImGui::SetNextItemWidth(width);
    if (!ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge)) return;
    ImGui::PushID(id);
    if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    SearchBox("##search", "Название или часть названия…",filter,sizeof(filters[0][0]));
    if (ImGui::Selectable(none, pick < 0)) pick = -1;
    int shown=0;
    for (int i=0;i<n;++i) {
        const char* name=Menu_GearExtraName(list,i);
        const char* display=ReadableListName(name);
        const char* alias=gear::EnglishAlias(name);
        if(!ListMatchesFilter(name,display,filter) && !(alias && MatchesFilter(alias,filter))) continue;
        ++shown; ImGui::PushID(i);
        if(ImGui::Selectable(display,i==pick)) pick=i;
        if(alias) ImGui::SetItemTooltip("Класс: %s\n%s",name,alias);
        else ImGui::SetItemTooltip("Класс: %s",name);
        ImGui::PopID();
    }
    if(!shown) Hint(n?"Совпадений нет. Очистите поиск в списке.":"Список пуст: раздела нет в items.txt или игра не знает его классы.");
    ImGui::TextDisabled("Найдено: %d / %d",shown,n);
    ImGui::PopID();
    ImGui::EndCombo();
}

static void HandAttachments(bool primary, const char* weapon, MenuWeaponAttachments& a, float half) {
    static const char* const kLabels[Attachment_SlotCount] = { "Прицел", "Дульная насадка", "Подствольный модуль" };
    gear::Hand h = gear::HandOf(a, primary);
    SectionHeading(primary ? "Насадки основного оружия" : "Насадки пистолета");
    switch (gear::Classify(weapon)) {
    case gear::WeaponKind::None:
        Hint("Оружие не выбрано: насадки и модули не надеваются.");
        break;
    case gear::WeaponKind::Tool:
        Hint("Для этого инструмента насадки не предлагаются.");
        break;
    case gear::WeaponKind::Firearm:
        for (int s = 0; s < Attachment_SlotCount; ++s) {
            if (s % 2) ImGui::SameLine();
            ExtraCombo(primary, s, kLabels[s], "Нет", h.firearm[s], half);
        }
        Hint("В списках только классы, известные игре. Подходит ли насадка к этому оружию, мод не проверяет: неподходящую игра может не установить.");
        break;
    case gear::WeaponKind::Multitool: {
        const bool defaultSrt = gear::IsDefaultSrtTool(weapon);
        ExtraCombo(primary, gear::Extra_ToolModule, "Модуль", defaultSrt ? "По умолчанию (SRT класса)" : "Нет", *h.module, half);
        const char* module = *h.module >= 0 ? Menu_GearExtraName(gear::Extra_ToolModule, *h.module) : nullptr;
        if (gear::UsesSrtCanister(weapon, module)) {
            ImGui::SameLine();
            ExtraCombo(primary, gear::Extra_ToolCanister, "Картридж SRT", defaultSrt && !module ? "По умолчанию" : "Нет", *h.canister, half);
        } else *h.canister = -1;
        Hint(defaultSrt ? "Без выбора остаются стандартные вложения SRT этого класса. Явный выбор ставит базовый мультитул с выбранным модулем и батареей."
                        : "При выборе модуля батарея из раздела [toolbattery] добавляется сама.");
        break;
    }
    }
}

void DrawPlayerTab(void*, void*) {
    static const std::string profileName = [] {
        char path[MAX_PATH]={};wchar_t widePath[MAX_PATH]={},name[49]={};char utf8[196]={};
        if(DataFilePath(path,MAX_PATH,"player-profile.ini")) {
            MultiByteToWideChar(CP_ACP,0,path,-1,widePath,MAX_PATH);
            GetPrivateProfileStringW(L"player",L"display_name",L"default_1",name,49,
                widePath);
            WideCharToMultiByte(CP_UTF8,0,name,-1,utf8,sizeof(utf8),nullptr,nullptr);
        }
        return std::string(utf8[0]?utf8:"default_1");
    }();
    SectionHeading("Локальный профиль");
    ImGui::Text("Игрок: %s",profileName.c_str());
    SectionHeading("Движение");
    static bool  noclip = false;
    static float speed = 30.0f;
    if (ImGui::Checkbox("Полёт сквозь объекты", &noclip)) {
        InvokeCreativeSpeed(speed);
        InvokeCreativeToggle("creative.noclip", noclip, &noclip);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##noclipSpeed", &speed, 1.0f, 500.0f, "Скорость %.0f", ImGuiSliderFlags_Logarithmic))
        InvokeCreativeSpeed(speed);
    Hint("F7 сохраняет позицию, F8 возвращает к ней. На вкладке «Перемещение» есть места и сохранённые точки.");

    SectionHeading("Защита");
    static bool god = false, ammo = false;
    if (ImGui::Checkbox("Неуязвимость", &god)) InvokeCreativeToggle("creative.god", god, &god);
    ImGui::SameLine(0, 24);
    if (ImGui::Checkbox("Бесконечные боеприпасы", &ammo)) InvokeCreativeToggle("creative.ammo", ammo, &ammo);
    Hint("Полёт сквозь объекты, неуязвимость и боеприпасы доступны через плагин creative. Включите его на странице плагинов лаунчера.");

    SectionHeading("Снаряжение");
    static int  gear[Gear_SlotCount] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    static MenuWeaponAttachments attachments;
    static std::string lastWeapon[2];
    static char gearFilter[64] = "";
    static const struct { const char* label; const char* none; } kGear[Gear_SlotCount] = {
        { "Поддоспешник", "По умолчанию" }, { "Шлем", "Нет" }, { "Корпус", "Нет" }, { "Руки", "Нет" }, { "Ноги", "Нет" },
        { "Рюкзак", "Нет" }, { "Основное оружие", "Нет" }, { "Пистолет", "Нет" }, { "Боеприпасы", "Для выбранного оружия" }, { "Гранаты", "Нет" } };
    if (Menu_GearCount(0) < 0) { Hint("Загрузка снаряжения — сначала войдите в игровой мир…"); return; }
    SearchBox("##gearFilter", "Поиск снаряжения", gearFilter, sizeof(gearFilter));
    const float half = Columns(2);
    for (int s = 0; s < Gear_SlotCount; ++s) {
        if (s % 2) ImGui::SameLine();
        GearCombo(s, kGear[s].label, kGear[s].none, gear[s], gearFilter, half);
    }
    const char* primary = gear[Gear_Primary] >= 0 ? Menu_GearName(Gear_Primary, gear[Gear_Primary]) : "";
    const char* sidearm = gear[Gear_Sidearm] >= 0 ? Menu_GearName(Gear_Sidearm, gear[Gear_Sidearm]) : "";
    gear::ResetOnWeaponChange(lastWeapon[0], primary, attachments, true);
    gear::ResetOnWeaponChange(lastWeapon[1], sidearm, attachments, false);
    HandAttachments(true, primary, attachments, half);
    HandAttachments(false, sidearm, attachments, half);
    if (ImGui::SmallButton("Сбросить насадки и модули")) attachments = MenuWeaponAttachments{};
    if (PrimaryButton("Надеть снаряжение")) { Menu_RequestEquip(gear, &attachments); MenuClose(); }
}

void DrawSq42Tab(void*, void*) {
    static bool spoilerOk = false;
    if (!spoilerOk) {
        ImGui::SeparatorText("Предупреждение о спойлерах");
        ImGui::TextWrapped("Эта вкладка может содержать спойлеры Squadron 42.");
        ImGui::TextWrapped("Нажмите ОК, чтобы продолжить.");
        if (ImGui::Button("OK", ImVec2(120, 0))) spoilerOk = true;
        ImGui::SameLine();
        if (ImGui::Button("Назад", ImVec2(120, 0))) MenuSelectFirstTab();
        return;
    }

    ImGui::SeparatorText("Комплекты одежды");
    const int outfits = Menu_OutfitCount();
    static int outfit = 0;
    if (outfits < 0) {
        ImGui::TextWrapped("Загрузка комплектов одежды — сначала появитесь в игровом мире…");
    } else if (outfits == 0) {
        ImGui::TextWrapped("Комплекты одежды не найдены. Проверьте outfits.txt.");
    } else {
        static char filter[64] = "";
        if (outfit >= outfits) outfit = 0;
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##outfitFilter", "Поиск комплектов одежды…", filter, sizeof(filter));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##outfit", ReadableListName(Menu_OutfitName(outfit)), ImGuiComboFlags_HeightLargest)) {
            for (int i = 0; i < outfits; ++i) {
                const char* name = Menu_OutfitName(i);
                if (!ListMatchesFilter(name, ReadableListName(name), filter)) continue;
                ImGui::PushID(i);
                if (ImGui::Selectable(ReadableListName(name), i == outfit)) outfit = i;
                ImGui::SetItemTooltip("%s",name);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Надеть комплект SQ42", ImVec2(-1, 0))) {
            Menu_RequestWearOutfit(outfit);
            MenuClose();
        }
    }
    static bool visor = false;
    if (ImGui::Checkbox("Интерфейс шлема SQ42 (после смены снаряжения)", &visor))
        Menu_SetS42VisorHud(visor);

    ImGui::SeparatorText("Настройки");
    for (int i = 0; i < Menu_S42SettingCount(); ++i) {
        bool on = Menu_S42SettingOn(i);
        ImGui::PushID(i);
        const bool known = Menu_S42SettingKnown(i);   // greyed until the game thread has read the cvar
        ImGui::BeginDisabled(!known);
        if (ImGui::Checkbox(Menu_S42SettingLabel(i), &on)) Menu_RequestS42Setting(i, on);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", known ? Menu_S42SettingTip(i) : "Чтение настройки из игры…");
        ImGui::PopID();
    }

    ImGui::SeparatorText("Создать");
    {
        static int   thing = -1;
        static char  thingFilter[64] = "";
        static bool  inFront = true;
        static float ahead = 8.0f;
        const int buildables = Menu_BuildCount();
        if (buildables < 0) {
            ImGui::TextWrapped("Загрузка — сначала появитесь в игровом мире…");
        } else if (buildables == 0) {
            ImGui::TextWrapped("Объекты не найдены. Проверьте buildables.txt.");
        } else {
            if (thing < 0) {
                thing = 0;
                for (int i = 0; i < buildables; ++i)
                    if (_stricmp(Menu_BuildCategory(i), "sq42") == 0) { thing = i; break; }
            }
            if (thing >= buildables) thing = 0;
            ImGui::Checkbox("Создать перед вами", &inFront);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##thingFilter", "Поиск в группе [sq42]…", thingFilter, sizeof(thingFilter));
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##sq42thing", ReadableListName(Menu_BuildName(thing)), ImGuiComboFlags_HeightLargest)) {
                for (int i = 0; i < buildables; ++i) {
                    // Only the [sq42] group, like the original; the text filter narrows it further.
                    if (_stricmp(Menu_BuildCategory(i), "sq42") != 0) continue;
                    const char* name = Menu_BuildName(i);
                    if (!ListMatchesFilter(name, ReadableListName(name), thingFilter)) continue;
                    ImGui::PushID(i);
                    if (ImGui::Selectable(ReadableListName(name), i == thing)) thing = i;
                    ImGui::SetItemTooltip("%s",name);
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (inFront) {
                ImGui::SetNextItemWidth(200);
                ImGui::SliderFloat("Впереди (м)", &ahead, 1.0f, 50.0f, "%.0f");
            }
            if (ImGui::Button("Создать объект", ImVec2(-1, 0))) {
                Menu_RequestPlace(thing, inFront, ahead);
                MenuClose();
            }
            ImGui::SetItemTooltip("Кнопки отмены и очистки на вкладке строительства удаляют и эти объекты.");
        }
    }

    ImGui::SeparatorText("Корабли");
    struct Entry { const char* label; const char* cls; bool enemyWing; float height; bool sit; };
    static const struct { const char* label; const char* cls; } kSq42Ships[] = {
        { "Idris-P (класс Stanton)", "AEGS_Idris_P" },
        { "Gladius (истребитель SQ42)",        "AEGS_Gladius" },
        { "Retaliator (интерфейс S42)",   "AEGS_Retaliator" },
        { "Starfarer (главы 5, 7, 9)",        "MISC_Starfarer" },
        { "Avenger Stalker (обломки S42)",   "AEGS_Avenger_Stalker" },
        { "Hornet (корабль Кэла Мэйсона)",     "ANVL_Hornet_F7C" },
        { "Vanduul Blade (ИИ)",            "VNCL_Blade_PU_AI_VAN" },
        { "Vanduul Scythe (ИИ)",           "VNCL_Scythe_PU_AI_VAN" },
        { "Vanduul Glaive (ИИ)",           "VNCL_Glaive_PU_AI_VAN" },
        { "Vanduul Stinger (ИИ)",          "VNCL_Stinger_PU_AI_VAN" },
    };
    Entry list[16];
    static_assert(sizeof(kSq42Ships) / sizeof(kSq42Ships[0]) + 2 <= sizeof(list) / sizeof(list[0]),
                  "SQ42 ship table outgrew Entry list[]");
    int   n = 0;
    for (size_t i = 0; i < sizeof(kSq42Ships) / sizeof(kSq42Ships[0]); ++i)
        list[n++] = { kSq42Ships[i].label, kSq42Ships[i].cls, false, 0.0f, true };
    for (int i = 0; i < n; ++i)
        if (strncmp(list[i].cls, "VNCL_", 5) == 0) { list[i].height = 300.0f; list[i].sit = false; }

    // Not the original's dormant "[battle]" mode (two Bengals fighting each other): these
    // spawn one UEE Bengal, the second with a Vanduul wing when an enemy side was found.
    static char bengalWing[64];
    const bool enemySide = Menu_EnemySideAvailable();
    strcpy_s(bengalWing, enemySide ? "Bengal и звено Vanduul" : "Bengal и звено (UEE, противник не найден)");
    list[n++] = { "Bengal (UEE)", "RSI_Bengal_PU_AI_UEE", false,      1500.0f, false };
    list[n++] = { bengalWing,     "RSI_Bengal_PU_AI_UEE", enemySide, 1500.0f, false };

    static int   pick = 0;
    static char  sqFilter[64] = "";
    static float height = 30.0f;   // the original's fixed spawn height for your own ships
    static bool  sit = true;
    if (pick >= n) pick = 0;
    // Greys out classes this game build doesn't have, like the original, instead of failing
    // after the click with "unknown entity class".
    auto known = [](const char* cls) {
        const int count = Menu_ShipCount();
        if (count < 0) return true;   // ship list still loading: don't block
        const MenuShip* ships = Menu_Ships();
        for (int i = 0; i < count; ++i)
            if (_stricmp(ships[i].name, cls) == 0) return true;
        return false;
    };

    ImGui::TextWrapped("Ваши корабли сажают вас в кресло пилота. Корабли Vanduul появляются на 300 м выше и атакуют вас.");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##sq42shipFilter", "Поиск кораблей…", sqFilter, sizeof(sqFilter));
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##sq42ships", list[pick].label, ImGuiComboFlags_HeightLargest)) {
        for (int i = 0; i < n; ++i) {
            if (sqFilter[0] && !ListMatchesFilter(list[i].cls, list[i].label, sqFilter)) continue;
            ImGui::PushID(i);
            const bool have = known(list[i].cls);
            if (ImGui::Selectable(list[i].label, i == pick, have ? 0 : ImGuiSelectableFlags_Disabled)) pick = i;
            if (!have) ImGui::SetItemTooltip("%s отсутствует в списке кораблей этой сборки игры.", list[i].cls);
            if (i == pick) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    const bool pickKnown = known(list[pick].cls);
    if (list[pick].sit) {
        ImGui::SliderFloat("Высота над вами (м)", &height, 0.0f, 500.0f, "%.0f");
        ImGui::Checkbox("Посадить в кресло пилота", &sit);
    }
    ImGui::BeginDisabled(!pickKnown);
    const bool spawnClicked = ImGui::Button("Создать", ImVec2(-1, 42));
    ImGui::EndDisabled();
    if (spawnClicked) {
        Menu_RequestSpawnClass(list[pick].cls,
                               list[pick].sit ? height : list[pick].height,
                               list[pick].sit && sit, list[pick].sit && sit,
                               list[pick].enemyWing);
        MenuClose();
    }

    ImGui::SeparatorText("Консоль");
    static char cmd[256] = "";
    const bool consoleReady = Menu_ConsoleReady();
    ImGui::BeginDisabled(!consoleReady);
    const float runWidth = ImGui::CalcTextSize("Выполнить").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetNextItemWidth(-(runWidth + ImGui::GetStyle().ItemSpacing.x));
    bool run = ImGui::InputTextWithHint("##console",
                                        "Команда, например i_target_selector.targeting2_enabled 1",
                                        cmd, sizeof(cmd), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    run |= ImGui::Button("Выполнить");
    ImGui::EndDisabled();
    if (run && cmd[0]) {
        Menu_RunConsole(cmd);
        cmd[0] = 0;
    }
    if (!consoleReady) ImGui::TextDisabled("Игровая консоль пока не найдена.");
    ImGui::TextWrapped("Команда выполняется в консоли игры. Результат смотрите в Game.log.");
}
