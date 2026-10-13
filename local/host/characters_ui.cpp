// The npc built-in's menu tab: NPCs, and the NPC picker the Crew tab shares. Registered through
// sco.ui (RegisterNpcsTab, called by npc_plugin.cpp) and drawn by the menu shell. The buttons run
// the built-in's npc.spawn and npc.clear commands through sco_api's invoke.
#include "../../src/../src/builtins/tabs.h"
#include "../../src/menu.h"
#include "../../src/menu_ui.h"
#include "../../src/companion.h"
#include "../../src/npc.h"
#include "../../src/npc_label.h"
#include "../../src/builtins/npc_host_adapter.h"
#include "../../src/../plugins/npc/npc.h"
#include "../../src/third_party/imgui/imgui.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <unordered_map>

static const sco_api* g_api = nullptr;
static sco_plugin*    g_self = nullptr;

// The command's reply (a refusal, or "Spawning ...") in the menu's status strip, which also logs it.
static void ShowReply(sco_result, const char* reply, void*) { SetMenuStatus("%s", reply ? reply : ""); }

static void InvokeNpcCommand(const char* name, const sco_arg* args, uint32_t nargs) {
    if (g_api && g_self) g_api->invoke(g_self, name, args, nargs, ShowReply, nullptr);
}

void RegisterLocalCharactersTab(const sco_api* api, sco_plugin* self) {
    g_api = api;
    g_self = self;
    RegisterBuiltinTab(api, self, "loadout.npcs", "Персонажи", kTabNpcs, DrawNpcsTab);
}

void ResetLocalCharactersTab() { g_api = nullptr; g_self = nullptr; }

// --- NPC picker, shared by the NPCs and Crew tabs -------------------------------------------

void DrawNpcsTab(void*, void*) {
    const void* service = nullptr;
    uint64_t latest = 0;
    if (g_api && g_api->query_service(NPC_LOCAL_OWNED_NAME, NPC_LOCAL_OWNED_VERSION, &service) == SCO_OK) {
        const auto* owned = static_cast<const npc_local_owned_v1*>(service);
        if (owned && owned->size >= sizeof(npc_local_owned_v1) && owned->latest_owned) latest = owned->latest_owned();
    }
    LocalNpcSelection(latest);
    if (!service) Hint("Плагин NPC отключён или ещё не загружен. Включите его в лаунчере.");
    SectionHeading("Компаньон: проверка интерфейса ИИ");
    Hint(Menu_CompanionBridgeStatus());
    const char* result = Menu_CompanionResultStatus();
    if (result[0]) Hint(result);
    if (ImGui::Button("Проверить загрузку модуля", ImVec2(-1, 0))) Menu_RequestCompanionProbe();
    if (ImGui::Button("Проверить отношение NPC к игроку", ImVec2(-1, 0))) Menu_RequestCompanionAction(7);
    Hint("Чтение фракций, личной враждебности и агрессии. Отсутствие личной враждебности ещё не гарантирует мирное поведение.");
    Hint("Экспериментальные команды: последний NPC, созданный этим модом. Работа в игре ещё не подтверждена.");
    if (!CompanionOrdersAvailable()) Hint("Назначение лидера, следование, ожидание, возврат и охрана отключены после сбоя. Исправление исследуется.");
    static uint64_t checkedNpc = 0;
    static bool peaceful = false, followingObserved = false;
    const uint64_t currentNpc = LastSpawnedNpcId();
    if (checkedNpc != currentNpc) {
        checkedNpc = currentNpc; peaceful = false; followingObserved = false;
        Menu_ConfirmCompanionPeaceful(false);
    }
    if (ImGui::Checkbox("Этот NPC сейчас не атакует меня", &peaceful)) {
        Menu_ConfirmCompanionPeaceful(peaceful);
        followingObserved = false;
    }
    Hint("Одноразовая проба без назначения лидера: 10 секунд после появления задания, затем штатная отмена. Выполнение ещё проверяется.");
    ImGui::BeginDisabled(!peaceful || !currentNpc || !CompanionFollowTrialAvailable());
    if (ImGui::Button("Проба следования без лидера (10 с)", ImVec2(-1, 0))) Menu_RequestCompanionAction(9);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!CompanionOrdersAvailable());
    Hint("При неизвестной фракции игрока запрос лидера не меняет фракции. Союзничество этим ещё не подтверждено.");
    ImGui::BeginDisabled(!peaceful);
    if (ImGui::Button("1. Назначить игрока лидером", ImVec2(-1, 0))) Menu_RequestCompanionAction(1);
    if (ImGui::Button("2. Следовать (бой запрещён)", ImVec2(-1, 0))) Menu_RequestCompanionAction(2);
    if (ImGui::Button("Ожидать", ImVec2(-1, 0))) Menu_RequestCompanionAction(3);
    if (ImGui::Button("Вернуться ко мне", ImVec2(-1, 0))) Menu_RequestCompanionAction(5);
    ImGui::EndDisabled();
    ImGui::Checkbox("Проверил: NPC следует за мной и не атакует", &followingObserved);
    ImGui::BeginDisabled(!peaceful || !followingObserved);
    if (ImGui::Button("Охранять меня — пробная команда", ImVec2(-1, 0))) Menu_RequestCompanionAction(4);
    ImGui::EndDisabled();
    Hint("Сначала проверьте следование и отношение NPC к вам. Совпадение фракций ещё не доказывает отсутствие личной враждебности.");
    ImGui::EndDisabled();
    SectionHeading("Создание NPC");
    if (!NpcPicker()) return;
    static int howMany = 1;
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##howMany", &howMany, 1, 10, howMany == 1 ? "1 NPC" : "NPC: %d");
    if (PrimaryButton("Создать передо мной")) {
        if (const char* error = LocalNpcSpawnError(Menu_NpcName(g_npcPick))) { SetMenuStatus("%s", error); return; }
        sco_arg args[2] = {};
        args[0].type = SCO_ARG_STRING;
        args[0].v.s = Menu_NpcName(g_npcPick);
        args[1].type = SCO_ARG_INT;
        args[1].v.i = howMany;
        InvokeNpcCommand("npc.spawn", args, 2);
        MenuClose();
    }
    if (ImGui::Button("Убрать созданных NPC", ImVec2(-1, 0))) {
        InvokeNpcCommand("npc.clear", nullptr, 0);
    }
    Hint("Удаляет только NPC этого плагина; экипаж не затрагивается.");
}
