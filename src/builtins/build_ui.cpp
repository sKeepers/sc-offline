// The build built-in's menu tab: Build (objects by group, placing and the base). Moved from
// menu.cpp unchanged; registered through sco.ui by build_plugin.cpp and drawn by the menu shell.
#include "tabs.h"
#include "../menu.h"
#include "../menu_ui.h"
#include "../third_party/imgui/imgui.h"
#include <cstdio>
#include <cstring>
#include <string>

void DrawBuildTab(void*, void*) {
    static int  build = 0, buildTab = 0;
    static char buildFilter[64] = "";
    const int buildables = Menu_BuildCount();
    SectionHeading("Объекты");
    if (buildables < 0) { Hint("Загрузка объектов — сначала войдите в игровой мир…"); return; }
    if (buildables == 0) { Hint("Объекты не найдены. Проверьте data\\buildables.txt."); return; }
    if (build >= buildables) build = 0;
    if (ImGui::BeginTabBar("##buildTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (int c = 0; c < Menu_BuildCategoryCount(); ++c) {
            const std::string tab=std::string(ReadableListName(Menu_BuildCategoryName(c)))+"##cat"+std::to_string(c);
            if (ImGui::BeginTabItem(tab.c_str())) { buildTab = c; ImGui::EndTabItem(); }
        }
        ImGui::EndTabBar();
    }
    SearchBox("##buildFilter", "Поиск среди всех объектов", buildFilter, sizeof(buildFilter));
    const bool searching = buildFilter[strspn(buildFilter, " _")] != 0;
    if (ImGui::BeginChild("##buildList", ImVec2(0, 260), ImGuiChildFlags_Borders)) {
        int shown=0;
        for(int i=0;i<buildables;++i) {
            const char* raw=Menu_BuildName(i);
            const char* display=ReadableListName(raw);
            const std::string search=std::string(display)+" "+Menu_BuildCategory(i)+" "+ReadableListName(Menu_BuildCategory(i));
            if(searching ? !ListMatchesFilter(raw,search.c_str(),buildFilter) : Menu_BuildCategoryOf(i)!=buildTab) continue;
            ++shown; ImGui::PushID(i);
            std::string label=display;
            if(searching) label+="  ("+std::string(ReadableListName(Menu_BuildCategory(i)))+")";
            if(ImGui::Selectable(label.c_str(),i==build))build=i;
            ImGui::SetItemTooltip("%s",raw); ImGui::PopID();
        }
        if(!shown) Hint("Совпадений нет.");

    }
    ImGui::EndChild();

    SectionHeading("Размещение");
    ImGui::Text("Выбрано: %s", ReadableListName(Menu_BuildName(build)));
    float reach = Menu_BuildReach();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##reach", &reach, 5.0f, 300.0f, "Дальность %.0f м");
    ImGui::SetItemTooltip("Дальность размещения. Объекты ставятся на землю в направлении взгляда.");
    Menu_SetBuild(build, reach);
    const bool building = Menu_BuildModeActive();
    if (PrimaryButton(building ? "Закончить строительство (F6)" : "Начать строительство (F6)")) {
        Menu_ToggleBuildMode();
        if (!building) MenuClose();
    }
    const float half = Columns(2);
    if (ImGui::Button("Отменить последнее", ImVec2(half, 0))) Menu_BuildUndo();
    ImGui::SameLine();
    char clearLabel[48];
    snprintf(clearLabel, sizeof(clearLabel), "Очистить (%d)###clearBase", Menu_BuildPlacedCount());
    if (ImGui::Button(clearLabel, ImVec2(half, 0))) Menu_BuildClear();
    Hint("ЛКМ — поставить; R — повернуть; [ и ] — дальность; Backspace — отменить; F6 — закончить.");
}
