// The spawn built-in's menu tab: Vehicles (the ship list, boarding options and the current ship).
// Moved from menu.cpp unchanged; registered through sco.ui by spawn_plugin.cpp and drawn by the
// menu shell.
#include "tabs.h"
#include "../menu.h"
#include "../menu_ui.h"
#include "../bridge.h"
#include "../third_party/imgui/imgui.h"
#include <cstdio>
#include <cstring>

void DrawVehiclesTab(void*, void*) {
    static int  selected = 0;
    static char filter[64] = "";
    static MenuSpawnOptions opt;

    SectionHeading("Мой флот — локальный сервер");
    static double fleetNextRefresh = 0;
    if (ImGui::GetTime() >= fleetNextRefresh) {
        Bridge_Refresh();
        fleetNextRefresh = ImGui::GetTime() + 3.0;
    }
    BridgeShip owned[256];
    char serverStatus[256] = {};
    bool serverBusy = false;
    const int ownedCount = Bridge_Snapshot(owned, 256, serverStatus, sizeof(serverStatus), serverBusy);
    ImGui::TextWrapped("%s", serverStatus);
    if (ImGui::Button("Обновить мой флот", ImVec2(-1, 0))) Bridge_Refresh();
    static char ownedSelection[121] = {};
    int ownedIndex = -1;
    static char fleetFilter[128]{};
    SearchBox("##fleetSearch","Поиск в моём флоте…",fleetFilter,sizeof(fleetFilter));
    if (ImGui::BeginTable("##ownedFleet", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY, ImVec2(0, 125))) {
        ImGui::TableSetupColumn("Корабль");
        ImGui::TableSetupColumn("Состояние");
        ImGui::TableHeadersRow();
        for (int i = 0; i < ownedCount; ++i) {
            const std::string fleetText=std::string(owned[i].name)+" "+ReadableShipName(owned[i].cls);
            if(!ListMatchesFilter(owned[i].cls,fleetText.c_str(),fleetFilter))continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(owned[i].id);
            if (ImGui::Selectable(owned[i].name, strcmp(ownedSelection, owned[i].id) == 0, ImGuiSelectableFlags_SpanAllColumns))
                strcpy_s(ownedSelection, owned[i].id);
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(!strcmp(owned[i].state, "stored") ? "На хранении" : !strcmp(owned[i].state, "reserved") ? "Зарезервирован" : "Создан в сессии");
        }
        ImGui::EndTable();
    }
    for (int i = 0; i < ownedCount; ++i) if (!strcmp(ownedSelection, owned[i].id)) ownedIndex = i;
    ImGui::BeginDisabled(serverBusy || ownedIndex < 0 || (ownedIndex >= 0 && strcmp(owned[ownedIndex].state, "stored")));
    if (ImGui::Button("Создать выбранный корабль моего флота", ImVec2(-1, 0))) Bridge_RequestSpawn(owned[ownedIndex]);
    ImGui::EndDisabled();
    Hint("Запустите локальный сервер и свяжите его с этой сборкой. Создание на 30 м выше вас: используйте открытое место. Сохраняется учёт корабля; предметы, груз и повреждения пока не сохраняются.");

    SectionHeading("Создание корабля");
    const int count = Menu_ShipCount();
    if (count < 0) {
        Hint("Загрузка кораблей — сначала войдите в игровой мир…");
    } else if (count == 0) {
        Hint("Корабли не найдены. Проверьте data\\ships.txt.");
    } else {
        const MenuShip* ships = Menu_Ships();
        if (selected >= count) selected = 0;
        if (SearchBox("##shipFilter", "Поиск кораблей", filter, sizeof(filter)))
            for (int i = 0; i < count; ++i)
                if (ListMatchesFilter(ships[i].name,ReadableShipName(ships[i].name),filter)) { selected = i; break; }
        const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter
                                    | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("##ships", 3, flags, ImVec2(0, 230))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Корабль", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Размер", ImGuiTableColumnFlags_WidthFixed, 44);
            ImGui::TableSetupColumn("Длина", ImGuiTableColumnFlags_WidthFixed, 64);
            ImGui::TableHeadersRow();
            for (int i = 0; i < count; ++i) {
                const std::string readable = ReadableShipName(ships[i].name);
                if (!ListMatchesFilter(ships[i].name,readable.c_str(),filter)) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(i);
                if (ImGui::Selectable(readable.c_str(), i == selected, ImGuiSelectableFlags_SpanAllColumns)) selected = i;
                ImGui::SetItemTooltip("Класс: %s", ships[i].name);
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d", ships[i].size);
                ImGui::TableNextColumn();
                if (ships[i].length > 0) ImGui::TextDisabled("%.0f м", ships[i].length);
            }
            ImGui::EndTable();
        }

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##height", &opt.height, 0.0f, 500.0f, "Создать на %.0f м выше вас");
        static const char* const kBoard[] = { "Не садиться в корабль", "Сесть в кресло пилота", "Сесть в кресло по названию", "Выбрать кресло после создания" };
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##board", &opt.seatMode, kBoard, 4);
        if (opt.seatMode == SeatMode_Named) {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##seatName", "Название кресла, например copilot или turret left", opt.seatName, sizeof(opt.seatName));
            ImGui::SetItemTooltip("Каждое слово должно входить в название кресла. Названия есть на вкладке «Экипаж».");
        }
        const bool boarding = opt.seatMode == SeatMode_Pilot || opt.seatMode == SeatMode_Named;
        ImGui::BeginDisabled(!boarding);
        ImGui::Checkbox("Убрать NPC из моего кресла", &opt.replaceNpc);
        ImGui::SameLine(0, 24);
        ImGui::Checkbox("Включить питание", &opt.flightReady);
        ImGui::SetItemTooltip("Включает корабль после посадки в кресло пилота.");
        ImGui::EndDisabled();

        const std::string spawn = std::string("Создать ") + ReadableShipName(ships[selected].name);
        if (PrimaryButton(spawn.c_str())) {
            MenuSpawnOptions send = opt;
            if (!boarding) send.flightReady = false;
            Menu_RequestSpawn(selected, send);
            if (opt.seatMode != SeatMode_PickLater) MenuClose();
        }
    }

    SectionHeading("Текущий корабль");
    static bool shipAmmo = false;
    if (ImGui::Checkbox("Бесконечные боеприпасы корабля", &shipAmmo)) InvokeCreativeToggle("creative.ship_ammo", shipAmmo, &shipAmmo);
    ImGui::SetItemTooltip("Refills the magazines of the ship you're aboard. Needs the creative plugin (launcher's Plugins page).");
    if (ImGui::Button("Включить / выключить питание", ImVec2(-1, 0))) Menu_RequestFlightReady();
    Hint("Переключает готовность к полёту корабля на вкладке «Экипаж», как клавиша R в кресле пилота.");
}
