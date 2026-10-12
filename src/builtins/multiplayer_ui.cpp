// The multiplayer built-in's menu tab: host, join or leave a session, and who's in it. Registered
// through sco.ui by multiplayer_plugin.cpp and drawn by the menu shell.
#include "multiplayer.h"
#include "tabs.h"
#include "../menu_ui.h"
#include "../third_party/imgui/imgui.h"
#include <cstdio>

namespace {

char g_message[192] = "";   // the last Host / Join answer

void Answer(const char* err, const char* ok) { snprintf(g_message, sizeof(g_message), "%s", err ? err : ok); }

void SessionForm(MpSettings& s) {
    SectionHeading("Вы");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##mpName", "Ваше имя для других игроков", s.name, sizeof(s.name));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##mpPass", "Пароль сессии (не менее 8 символов)", s.passphrase, sizeof(s.passphrase),
                             ImGuiInputTextFlags_Password);
    ImGui::Checkbox("Запомнить пароль на этом ПК", &s.rememberPass);
    ImGui::SetItemTooltip("Хранится без шифрования в data\\storage\\multiplayer.db. Если выключено, вводится каждый раз.");

    SectionHeading("Создание сессии");
    ImGui::SetNextItemWidth(Columns(2));
    ImGui::InputInt("Порт UDP##mpHostPort", &s.hostPort, 0);
    if (PrimaryButton("Создать сессию")) Answer(Mp_Host(), "Сессия создана. Передайте другим игрокам свой адрес в локальной сети, порт и пароль.");

    SectionHeading("Подключение к сессии");
    ImGui::SetNextItemWidth(Columns(2));
    ImGui::InputTextWithHint("##mpAddr", "Адрес ведущего (192.168.1.20)", s.joinAddress, sizeof(s.joinAddress));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputInt("##mpJoinPort", &s.joinPort, 0);
    if (PrimaryButton("Подключиться")) Answer(Mp_Join(), "Подключение…");
}

void PeerList() {
    const int n = Mp_PeerCount();
    if (!ImGui::BeginTable("##mpPeers", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) return;
    ImGui::TableSetupColumn("Игрок");
    ImGui::TableSetupColumn("Внешний вид");
    ImGui::TableSetupColumn("Корабль");
    ImGui::TableSetupColumn("Обмен и версия");
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    for (int i = 0; i < n; ++i) {
        MpPeerView v;
        if (!Mp_Peer(i, v)) continue;
        ImGui::PushID(i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s", v.name);
        ImGui::SetItemTooltip("Игрок %llu, ID сессии %016llx", static_cast<unsigned long long>(v.peer),
                              static_cast<unsigned long long>(v.sessionId));
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", v.avatar);
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", v.ship[0] ? v.ship : "-");
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s",v.build);
        if(v.versionKnown)ImGui::TextWrapped("Протокол позиций: %u%s",v.poseVersion,v.poseVersion==1?"":" — несовместим");
        if(!v.self) {
            if(v.gotPose)ImGui::TextWrapped("Позиция: %.1f с назад; пакетов: %llu",v.poseAgeMs/1000.0,v.posesReceived);
            else ImGui::TextWrapped("Позиции пока нет; принято: %llu",v.posesReceived);
        }
        ImGui::TableNextColumn();
        if (!v.self && ImGui::SmallButton("Перейти")) Answer(Mp_GoTo(v.peer), "Перемещение выполнено.");
        ImGui::PopID();
    }
    ImGui::EndTable();
}

}  // namespace

void DrawMultiplayerTab(void*, void*) {
    if (!Mp_Enabled()) {
        Hint("Сетевая сессия недоступна: multiplayer = off в sc-offline.ini или отсутствует sco.net. См. mod.log.");
        return;
    }
    MpSettings& s = Mp_Settings();
    SectionHeading("Сессия");
    ImGui::Text("%s", Mp_StateText());
    if (Mp_LastReason()[0]) ImGui::TextColored(kMuted, "Последняя сессия завершена: %s", Mp_LastReason());
    if (g_message[0]) ImGui::TextWrapped("%s", g_message);
    char diagnostics[768]{};Mp_Diagnostics(diagnostics,sizeof(diagnostics));
    ImGui::TextWrapped("%s",diagnostics);
    if(!Mp_InSession())Hint("Через интернет используйте IPv4-адрес ведущего в VPN. На обоих ПК разрешите диапазон VPN в multiplayer_allow; версии игры должны совпадать.");
    if (Mp_InSession()) {
        if (ImGui::Button("Отключиться", ImVec2(-1, 0))) { Mp_Leave(); g_message[0] = 0; }
        PeerList();
        if (!Mp_GhostsReady()) Hint("Отображение игроков выключено: сборка не поддерживает spawn.entities 1.2 или teleport.spatial.");
    } else {
        SessionForm(s);
    }

    SectionHeading("Этап проверки");
    ImGui::Checkbox("На этом ПК только проверка связи и позиций", &s.diagnosticOnly);
    ImGui::SetItemTooltip("В этом режиме сетевые NPC и корабли не создаются здесь. Сначала проверьте подключение, версии и обмен позициями на двух ПК, затем отключите этот режим для визуального теста.");
    if(s.diagnosticOnly)Hint("Сеть работает; создание копий игроков и кораблей на этом ПК отключено.");
    SectionHeading("Отображение других игроков");
    ImGui::BeginDisabled(s.diagnosticOnly);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##mpAvatar", "Класс NPC для вашего внешнего вида", s.avatarClass, sizeof(s.avatarClass));
    ImGui::SetItemTooltip("Класс персонажа из npcs.txt. Другой игрок увидит его, если он есть в его npcs.txt;\n"
                          "иначе — персонажа по своему выбору.");
    ImGui::Checkbox("Показывать корабли других игроков", &s.showShips);
    ImGui::EndDisabled();
    if (ImGui::Button("Сохранить настройки", ImVec2(-1, 0))) Mp_SaveSettings();
    Hint("Только локальная сеть (для VPN — multiplayer_allow в sc-offline.ini). Сообщения подписаны паролем, но не "
         "зашифрованы. Игроки отображаются визуально: общая физика, урон, миссии и инвентарь отсутствуют.");
    if (Mp_AllowText()[0]) ImGui::TextColored(kMuted, "Также разрешено: %s", Mp_AllowText());
}
