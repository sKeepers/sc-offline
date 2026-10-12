// Host archetype picker shared with Crew; ownership and npc.* are external.
#include "tabs.h"
#include "../menu.h"
#include "../menu_ui.h"
#include "../npc_label.h"
#include "../third_party/imgui/imgui.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <unordered_map>
int g_npcPick = 0;

bool NpcPicker() {
    static char filter[64] = "";
    const int npcs = Menu_NpcCount();
    if (npcs < 0) { Hint("Загрузка NPC — сначала войдите в игровой мир…"); return false; }
    if (npcs == 0) { Hint("NPC не найдены. Проверьте data\\npcs.txt."); return false; }
    static std::vector<std::string> labels, searchable;
    if(static_cast<int>(labels.size())!=npcs) {
        labels.clear();searchable.clear();
        std::unordered_map<std::string,int> duplicates;
        for(int i=0;i<npcs;++i) { labels.push_back(NpcDisplayLabel(Menu_NpcName(i)));++duplicates[labels.back()]; }
        for(int i=0;i<npcs;++i) {
            searchable.push_back(labels[i]+" "+Menu_NpcName(i));
            if(duplicates[labels[i]]>1) labels[i]+=" ["+std::string(Menu_NpcName(i))+"]";
        }
    }
    if (g_npcPick >= npcs) g_npcPick = 0;
    if (ImGui::Button("Выбрать законопослушную охрану для проверки", ImVec2(-1, 0))) {
        strcpy_s(filter, "GroundCombat_NPC_Security_Lawful");
        for (int i = 0; i < npcs; ++i)
            if (MatchesFilter(Menu_NpcName(i), filter)) { g_npcPick = i; break; }
    }
    if (SearchBox("##npcFilter", "Поиск NPC", filter, sizeof(filter)))
        for (int i = 0; i < npcs; ++i)
            if (MatchesFilter(searchable[i].c_str(), filter)) { g_npcPick = i; break; }
    int matches=0;
    for(int i=0;i<npcs;++i) if(MatchesFilter(searchable[i].c_str(),filter)) ++matches;
    if(matches) ImGui::TextDisabled("Найдено: %d",matches);
    else Hint("Совпадений нет. Измените или очистите поиск.");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##npc", labels[g_npcPick].c_str(), ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < npcs; ++i) {
            const char* name = Menu_NpcName(i);
            if (!MatchesFilter(searchable[i].c_str(), filter)) continue;
            ImGui::PushID(i);
            if (ImGui::Selectable(labels[i].c_str(), i == g_npcPick)) g_npcPick = i;
            ImGui::SetItemTooltip("%s", name);
            if (i == g_npcPick) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return matches > 0;
}

