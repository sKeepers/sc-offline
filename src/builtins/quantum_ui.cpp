// The quantum built-in's menu tab: Travel (places, the scan and saved spots). Moved from menu.cpp
// unchanged; registered through sco.ui by quantum_plugin.cpp and drawn by the menu shell.
#include "tabs.h"
#include "../menu.h"
#include "../menu_ui.h"
#include "../third_party/imgui/imgui.h"
#include "../travel.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

static bool PlaceMatches(const TravelPlace& p,const char* filter) {
    const std::string text=std::string(p.name)+" "+p.system;
    return ListMatchesFilter(p.entity,text.c_str(),filter);
}

static bool HasText(const char* filter) { return filter[strspn(filter, " _")] != 0; }

void DrawTravelTab(void*, void*) {
    static TravelPlace    places[3000];
    static int            order[3000];
    static TravelBookmark marks[400];
    static char  filter[64] = "";
    static char  markName[64] = "";
    static char  selected[96] = "";       // entity name of the selected place
    static float altitude = 2000.0f;
    static bool  showMinor = false;

    static int np = 0, placesVersion = -1;
    char here[32];
    Travel_CurrentSystem(here, sizeof(here));
    const int version = Travel_PlacesVersion();
    const bool placesChanged = version != placesVersion;
    if (placesChanged) { placesVersion = version; np = Travel_GetPlaces(places, 3000); }
    const int nm = Travel_GetBookmarks(marks, 400);
    const bool searching = HasText(filter);

    // Systems that have anything in them; the one you're in first, then A-Z.
    const char* systems[48];
    int ns = 0;
    auto addSystem = [&](const char* sys) {
        if (!sys[0]) return;
        for (int i = 0; i < ns; ++i) if (_stricmp(systems[i], sys) == 0) return;
        if (ns < 48) systems[ns++] = sys;
    };
    if (here[0]) addSystem(here);
    for (int i = 0; i < np; ++i) addSystem(places[i].system);
    for (int i = 0; i < nm; ++i) addSystem(marks[i].system);
    std::sort(systems + (here[0] ? 1 : 0), systems + ns, [](const char* a, const char* b) { return _stricmp(a, b) < 0; });

    if (placesChanged) {                           // OOC_Stanton_1, 1a, 1b, 2 ... reads in orbit order
        for (int i = 0; i < np; ++i) order[i] = i;
        std::sort(order, order + np, [&](int a, int b) { return _stricmp(places[a].entity, places[b].entity) < 0; });
    }

    SearchBox("##travelFilter", "Поиск мест и сохранённых точек", filter, sizeof(filter));

    SectionHeading("Места");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##altitude", &altitude, 100.0f, 20000.0f, "Прибыть на %.0f м над поверхностью", ImGuiSliderFlags_Logarithmic);
    ImGui::Checkbox("Показывать интерьеры и малые зоны", &showMinor);
    ImGui::SetItemTooltip("Холлы лифтов, ангары, участки пояса астероидов и другие малые зоны. По умолчанию скрыты.");
    const TravelPlace* pick = nullptr;
    bool go = false;
    for (int s = 0; s < ns; ++s) {
        int shown = 0;
        for (int k = 0; k < np; ++k) {
            const TravelPlace& p = places[order[k]];
            if (p.kind == Place_Minor && !showMinor) continue;
            if (_stricmp(p.system, systems[s]) == 0 && (!searching || PlaceMatches(p,filter))) ++shown;
        }
        if (!shown) continue;
        const bool isHere = here[0] && _stricmp(systems[s], here) == 0;
        char header[80];
        const bool unnamed = _strnicmp(systems[s], "SolarSystem", 11) == 0;
        snprintf(header, sizeof(header), "%s%s (%d)###sys_%s", unnamed ? "Безымянная система" : systems[s],
                 isHere ? ", вы здесь" : "", shown, systems[s]);
        if (searching) ImGui::SetNextItemOpen(true);
        if (!ImGui::CollapsingHeader(header, isHere ? ImGuiTreeNodeFlags_DefaultOpen : 0)) continue;
        char table[48];
        snprintf(table, sizeof(table), "##places_%s", systems[s]);
        const float rows = static_cast<float>(shown < 10 ? shown : 10);
        const float height = (rows + 1) * ImGui::GetFrameHeight() + 4;
        if (!ImGui::BeginTable(table, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY, ImVec2(0, height))) continue;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Место", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Тип", ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableHeadersRow();
        for (int k = 0; k < np; ++k) {
            const TravelPlace& p = places[order[k]];
            if (_stricmp(p.system, systems[s]) != 0 || (p.kind == Place_Minor && !showMinor)) continue;
            if (searching && !PlaceMatches(p,filter)) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char label[128];
            snprintf(label, sizeof(label), "%s##%s", p.name, p.entity);
            const bool isSel = _stricmp(selected, p.entity) == 0;
            if (ImGui::Selectable(label, isSel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                strcpy_s(selected, p.entity);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { pick = &p; go = true; }
            }
            ImGui::SetItemTooltip("%s", p.entity);
            ImGui::TableNextColumn();
            static const char* const kKind[] = { "Планета", "Спутник", "Место", "Интерьер" };
            const int kindIdx = p.kind >= 0 && p.kind <= 3 ? p.kind : 2;
            ImGui::TextDisabled("%s%s", kKind[kindIdx], kindIdx <= Place_Moon && p.radius <= 0 ? ", орбита" : "");
        }
        ImGui::EndTable();
    }
    if (!pick)
        for (int i = 0; i < np; ++i)
            if (_stricmp(places[i].entity, selected) == 0) { pick = &places[i]; break; }
    char goLabel[96];
    snprintf(goLabel, sizeof(goLabel), pick ? "Переместиться: %s" : "Выберите место", pick ? pick->name : "");
    ImGui::BeginDisabled(!pick);
    if (PrimaryButton(goLabel)) go = true;
    ImGui::EndDisabled();
    if (go && pick) Travel_RequestPlace(*pick, altitude);
    ImGui::BeginDisabled(!pick);
    if (ImGui::Button("Начинать игру в выбранном месте", ImVec2(-1, 0))) Travel_RequestStartupPlace(*pick, altitude);
    ImGui::EndDisabled();
    if (ImGui::Button("Начинать игру в моей текущей точке", ImVec2(-1, 0))) Travel_RequestStartupHere();
    char startup[256]; Travel_StartupDescription(startup, sizeof(startup)); Hint(startup);
    if (Travel_HasStartup() && ImGui::Button("Вернуть старт по настройкам лаунчера", ImVec2(-1, 0))) Travel_RequestDisableStartup();
    Hint("Загрузка PU_All обязательна для перехода между системами. Переносится персонаж. Для старта внутри города сохраните точку стоя на полу; кровать и груз корабля пока не восстанавливаются.");

    float progress = 0;
    if (Travel_Scanning(progress)) {
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kLeafDeep);
        ImGui::ProgressBar(progress, ImVec2(-1, 0), "Сканирование…");
        ImGui::PopStyleColor();
    } else if (ImGui::Button("Найти места в игровом мире", ImVec2(-1, 0))) {
        Travel_RequestScan();
    }
    Hint("Поиск находит планеты, спутники, станции, точки Лагранжа, ретрансляторы и точки перехода каждой загруженной "
         "системы и добавляет их сюда. Занимает несколько секунд и только читает данные.");

    SectionHeading("Сохранённые точки");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 160 - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##markName", "Название точки", markName, sizeof(markName));
    ImGui::SameLine();
    if (ImGui::Button("Сохранить точку", ImVec2(160, 0))) { Travel_RequestSaveBookmark(markName); markName[0] = 0; }
    if (!nm) Hint("Точек пока нет. Встаньте в нужном месте, введите название и нажмите «Сохранить точку».");
    for (int s = 0; s < ns; ++s) {
        int shown = 0;
        for (int i = 0; i < nm; ++i)
            if (_stricmp(marks[i].system, systems[s]) == 0 && (!searching || MatchesFilter(marks[i].name, filter))) ++shown;
        if (!shown) continue;
        char node[80];
        snprintf(node, sizeof(node), "%s (%d)###marks_%s", systems[s], shown, systems[s]);
        if (searching) ImGui::SetNextItemOpen(true);
        if (!ImGui::TreeNodeEx(node, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) continue;
        for (int i = 0; i < nm; ++i) {
            if (_stricmp(marks[i].system, systems[s]) != 0 || (searching && !MatchesFilter(marks[i].name, filter))) continue;
            ImGui::PushID(i);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(marks[i].name);
            const float buttons = 70 + 80 + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SameLine(ImGui::GetContentRegionMax().x - buttons);
            if (ImGui::Button("Перейти", ImVec2(70, 0))) Travel_RequestBookmark(i);
            ImGui::SameLine();
            if (ImGui::Button("Удалить", ImVec2(80, 0))) Travel_RequestDeleteBookmark(i);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    Hint("F7 и F8 сохраняют и восстанавливают позицию. Для перехода между системами целевая зона должна быть загружена в PU_All.");
}
