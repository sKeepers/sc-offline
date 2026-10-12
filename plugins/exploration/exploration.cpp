#include "local_api.h"
#include "ui_bind.h"
#include "exploration_state.h"
#include "sco_api.h"
#include "sco_ui.h"
#include "scosdk/storage.hpp"
#include <sc_spatial.h>
#include <sc_spawn.h>
#include <ctime>
#include <string>
#include <cstdarg>
#include <cstdio>

namespace {
const sco_api* g_api=nullptr;
sco_plugin* g_self=nullptr;
const LocalFeaturesV1* h=nullptr;
const sco_ui_v1* g_ui=nullptr;
bool Adapter() {
 const void* table=nullptr;
 if(g_api->query_service(LOCAL_FEATURES_NAME,LOCAL_FEATURES_VERSION,&table)!=SCO_OK)return false;
 h=static_cast<const LocalFeaturesV1*>(table);return h && h->size>=sizeof(LocalFeaturesV1);
}
void Log(const char* fmt,...) {char b[1024];va_list a;va_start(a,fmt);vsnprintf(b,sizeof(b),fmt,a);va_end(a);g_api->log(g_self,SCO_LOG_INFO,b);}
class BuiltinStore {
 sco::sdk::Storage s;
public:
 explicit BuiltinStore(const char*){}
 void Open(const sco_api* api,sco_plugin* self){s.Open(api,self);}
 void Close(){s={};}
 explicit operator bool()const{return static_cast<bool>(s);}
 sco::sdk::Storage& S(){return s;}
};
bool SearchBox(const char* id,const char* hint,char* buf,size_t n){ImGui::SetNextItemWidth(-1);return ImGui::InputTextWithHint(id,hint,buf,n);}
void Hint(const char* s){ImGui::TextDisabled("%s",s);}
void SetMenuStatus(const char*,const char* s){h->status(s);}
void EquipLoadoutXml(const std::string& xml,const char* tag){h->equip_xml(xml.data(),static_cast<uint32_t>(xml.size()),tag);}
#define CaptureCurrentSpot h->capture
#define GoToSpot h->go
#define SpawnerReady h->spawn_ready
#define Menu_RequestSpawnClass h->spawn
#define Menu_ShipCount h->ship_count
#define Menu_Ships h->ships
#define Travel_GetPlaces h->places
#define Travel_HasStartup h->has_startup
#define Travel_RequestPlace h->travel
#define ReadableShipName h->ship_label
#define MatchesFilter h->filter
#define ListMatchesFilter h->list_filter
#define LoadoutApiReady h->loadout_ready
#define DataFilePath h->data_path
#define PlayerShipId() (g_spawn?g_spawn->player_ship_id():0)

using namespace exploration;
State g_state;
BuiltinStore g_store{"exploration"};
const sc_spatial_v1* g_sp = nullptr;
const sc_spawn_service_v1* g_spawn = nullptr;
Spot g_here;
char g_liveShip[64]{};
char g_note[256] = "Готово. Сохранения и маршруты находятся в exploration.db.";
DWORD g_lastTick=0, g_lastSave=0, g_readyAt=0, g_restoreAt=0;
bool g_autoPending=false, g_autoSave=true, g_restoring=false;
int g_job=0, g_stage=0;
DWORD g_jobAt=0;
uint32_t g_dwell=0;
bool g_leftStart=false;
TravelPlace g_visitTarget{};
Spot g_previous; int g_pointTarget=0;
double g_speed=-1;
std::string g_savedGear;
const sco_plugin_info kInfo={sizeof(sco_plugin_info),SCO_API_MAJOR,SCO_API_MINOR,"exploration","1.0.0","sc-offline RU"};

void Note(const char* text) { strncpy_s(g_note,text,_TRUNCATE); SetMenuStatus("%s",text); }
bool Persist() {
    if(!g_store || g_store.S().Put("state.v1",g_state)!=SCO_OK) {
        Note("Не удалось записать exploration.db. Предыдущее сохранение оставлено."); return false;
    }
    return true;
}
int Chain(uint64_t zone,ZoneFrame* out){return h->chain(zone,out,kMaxZoneDepth);}
bool Capture(Spot& out, char* ship) {
    if(CaptureCurrentSpot(out)) return false;
    ship[0]=0;
    double p[3],r[4]; uint64_t zone=0;
    if(!g_sp || !g_spawn || !g_sp->player_pose(p,r,&zone)) return true;
    ZoneFrame chain[kMaxZoneDepth]; const int n=Chain(zone,chain);
    const auto id=g_spawn->player_ship_id();
    for(int i=0;id && i<n;++i) if(chain[i].id==id) {
        char cls[96]; strcpy_s(cls,chain[i].name);
        for(int attempt=0;attempt<4;++attempt) {
            if(strlen(cls)<64 && g_spawn->class_exists(cls)) {strcpy_s(ship,64,cls);break;}
            auto cut=strrchr(cls,'_'); if(!cut || !cut[1]) break;
            bool digits=true; for(auto c=cut+1;*c;++c) if(*c<'0'||*c>'9') digits=false;
            if(!digits) break; *cut=0;
        }
        // Save the player's position in the frame outside their ship; the old ship will not exist on resume.
        if(i+1<n) {
            for(int k=0;k<out.n;++k) if(strcmp(out.z[k].name,chain[i+1].name)==0) {
                for(int j=k;j<out.n;++j) out.z[j-k]=out.z[j]; out.n-=k; break;
            }
        }
        break;
    }
    return ValidSpot(out);
}
bool SaveSession(bool announce) {
    Spot spot; char ship[64]; if(!Capture(spot,ship)) {if(announce) Note("Персонаж ещё недоступен для сохранения.");return false;}
    State before=g_state;
    g_state.saved=spot; strcpy_s(g_state.ship,ship); g_state.savedAt=static_cast<uint64_t>(time(nullptr));
    if(!Persist()) {g_state=before;return false;}
    Log("[exploration] session saved: %s, ship=%s",spot.z[0].name,ship[0]?ship:"on foot");
    if(announce) Note("Место и класс корабля сохранены. Последнее выбранное в моде снаряжение сохраняется отдельно.");
    return true;
}
void Resume(DWORD now) {
    if(!g_state.saved.n) {Note("Сохранённой сессии пока нет.");return;}
    if(PlayerShipId()) {Note("Для восстановления сначала выйдите из текущего корабля.");return;}
    if(const char* err=GoToSpot(g_state.saved,now,"exploration.resume")) {Note(err);return;}
    g_restoring=true; g_restoreAt=now; g_autoPending=false;
    g_job=0; Note("Восстанавливаю место; через 20 секунд будет создан новый корабль сохранённого класса.");
}
void SpawnSelected(const char* cls) {
    if(!SpawnerReady() || !g_spawn || !g_spawn->class_exists(cls)) {Note("Этот класс корабля недоступен в текущей игре.");return;}
    Menu_RequestSpawnClass(cls,30,true,true,false);
}
void Finish(DWORD now) {
    State before=g_state;
    const uint64_t reward=g_job==1?100:g_job==2?250:g_job==3?500:150;
    g_state.points+=reward; ++g_state.completions;
    const uint64_t elapsed=static_cast<DWORD>(now-g_jobAt);
    if(g_job==3 && (!g_state.bestRaceMs || elapsed<g_state.bestRaceMs)) g_state.bestRaceMs=elapsed;
    if(!Persist()) {g_state=before; g_job=0;return;}
    char line[192]; snprintf(line,sizeof(line),"Задание выполнено! +%llu очков исследования; время %.1f с.",reward,elapsed/1000.0);
    Note(line); Log("[exploration] mission completed: kind=%d, points=%llu, elapsed_ms=%llu",g_job,reward,elapsed); g_job=0;
}
void Missions(DWORD now) {
    if(!g_job) return;
    if(g_job==1) {
        if(Contains(g_here,g_visitTarget.entity)) {if(!g_dwell) g_dwell=now; if(now-g_dwell>=5000) Finish(now);}
        else g_dwell=0;
        return;
    }
    if(g_job==4||g_job==5) {
        if(g_pointTarget>=g_state.routeCount){g_job=0;return;}
        const auto& target=g_state.route[g_pointTarget];const auto d=Distance(g_here,target.spot);
        const bool stable=g_job==4 || (g_liveShip[0] && g_speed>=0 && g_speed<=1);
        if(Reached(d,target.radius,now,g_dwell,g_job==4?10000:15000,stable))Finish(now);
        return;
    }
    if(g_state.routeCount<2 || g_stage>=g_state.routeCount) {g_job=0;return;}
    const auto& target=g_state.route[g_stage]; const double d=Distance(g_here,target.spot);
    if(g_stage==0 && d>target.radius*2) g_leftStart=true;
    if(!Reached(d,target.radius,now,g_dwell,g_job==2?3000:0,g_leftStart))return;
    ++g_stage; g_dwell=0;
    if(g_stage==g_state.routeCount) Finish(now);
    else {char line[160];snprintf(line,sizeof(line),"Точка пройдена. Следующая: %s",g_state.route[g_stage].name);Note(line);}
}
void WriteCapabilities() {
    char path[MAX_PATH]; if(!DataFilePath(path,sizeof(path),"capabilities-ru.txt")) return;
    FILE* f=nullptr; fopen_s(&f,path,"wb"); if(!f) return;
    fprintf(f,"Функции sc-offline %s RU\nДоступность по найденному коду; не подтверждение игрового теста.\n","0.7.6 / exploration 1.0.0");
    LocalCap caps[256]; const auto n=h->caps(caps,256);
    for(size_t i=0;i<n && i<256;++i) fprintf(f,"%s: %s %s\n",caps[i].name,caps[i].ready?"доступно":"недоступно",caps[i].reason);
    fclose(f);
}
void OnTick(const char*,const void* data,void*) {
    if(!Adapter())return;
    static uint64_t lastVersion=0;
    uint64_t version=0;const uint32_t bytes=h->last_xml(nullptr,0,&version);
    if(bytes && bytes<=1024*1024 && version!=lastVersion) {
        std::string xml(bytes,'\0');
        if(h->last_xml(xml.data(),bytes,&version)==bytes && g_store.S().Put("gear.xml",std::string_view(xml))==SCO_OK){g_savedGear=std::move(xml);lastVersion=version;}
    }
    const DWORD now=data?*static_cast<const DWORD*>(data):GetTickCount();
    const DWORD elapsed=now-g_lastTick;
    if(elapsed<1000) return; g_lastTick=now;
    if(!Capture(g_here,g_liveShip)) {g_dwell=0;g_previous={};return;}
    // First sample has no speed; use a named non-system frame to avoid planet orbital motion.
    g_speed=-1;
    if(g_previous.n && g_here.n && strcmp(g_previous.z[0].name,g_here.z[0].name)==0) {
        const double d=Distance(g_here,g_previous);if(d>=0)g_speed=d*1000.0/elapsed;
    }
    g_previous=g_here;
    if(!g_readyAt) {g_readyAt=now;WriteCapabilities();}
    if(g_autoPending && Travel_HasStartup()) g_autoPending=false;
    if(g_autoPending && now-g_readyAt>=20000) {Resume(now); if(!g_restoring) {g_autoPending=false;g_autoSave=false;Note("Автовосстановление не выполнено. Автосохранение остановлено, чтобы сохранить прежнюю сессию.");}}
    if(g_restoring) {
        if(now-g_restoreAt>=20000) {
            if(Distance(g_here,g_state.saved)<0 || Distance(g_here,g_state.saved)>1000) {
                g_restoring=false;g_autoSave=false;Note("Не удалось подтвердить восстановление места. Автосохранение остановлено; прежняя сессия сохранена.");return;
            }
            if(g_state.ship[0]) SpawnSelected(g_state.ship);
            if(!g_savedGear.empty() && LoadoutApiReady()) EquipLoadoutXml(g_savedGear,"exploration.resume");
            g_restoring=false; g_lastSave=now;
            Note("Запрос восстановления завершён. Корабль создаётся заново; груз и повреждения не восстанавливаются.");
        }
        return;
    }
    static TravelPlace places[3000]; const int n=Travel_GetPlaces(places,3000);
    for(int i=0;i<n && g_state.visitCount<kMaxVisits;++i) if(Contains(g_here,places[i].entity)) {
        bool known=false;for(int j=0;j<g_state.visitCount;++j) if(strcmp(g_state.visits[j],places[i].entity)==0) known=true;
        if(!known) {strcpy_s(g_state.visits[g_state.visitCount++],places[i].entity);Persist();Log("[exploration] discovered: %s",places[i].entity);}
    }
    Missions(now);
    if(g_autoSave && !g_autoPending && now-g_readyAt>=60000 && now-g_lastSave>=60000) {g_lastSave=now;SaveSession(false);}
}
void Draw(void* frame,void*) {
    sc_offline_imgui::Bind(frame);
    if(!Adapter()){Hint("Адаптер основной сборки недоступен.");return;}
    ImGui::TextWrapped("%s",g_note);
    ImGui::Separator();
    if(ImGui::BeginTabBar("exploration.pages")) {
        if(ImGui::BeginTabItem("Свободный полёт")) {
            ImGui::TextWrapped("Автосохранение раз в минуту. Восстановление создаёт новый корабль; точная ориентация, груз и повреждения не сохраняются.");
            ImGui::Checkbox("Автосохранение в этой сессии",&g_autoSave);
            if(ImGui::Button("Сохранить сессию")) SaveSession(true);
            ImGui::SameLine(); ImGui::BeginDisabled(g_restoring||!g_state.saved.n);
            if(ImGui::Button("Продолжить сохранённую")) Resume(GetTickCount()); ImGui::EndDisabled();
            ImGui::Text("Сохранённый корабль: %s",g_state.ship[0]?ReadableShipName(g_state.ship):"пешком");
            if(g_state.savedAt) ImGui::Text("Место: %s",g_state.saved.z[0].name);
            static int selected=0; static char filter[64]{};
            ImGui::InputText("Поиск корабля",filter,sizeof(filter));
            const int count=Menu_ShipCount(); const auto ships=Menu_Ships();
            if(count>0 && ships) {
                if(selected>=count)selected=0;
                if(ImGui::BeginCombo("Корабль",ReadableShipName(ships[selected].name))) {
                    int shown=0;
                    for(int i=0;i<count;++i) if(ListMatchesFilter(ships[i].name,ReadableShipName(ships[i].name),filter)) {
                        ++shown; ImGui::PushID(i);
                        if(ImGui::Selectable(ReadableShipName(ships[i].name),i==selected))selected=i;
                        ImGui::SetItemTooltip("%s",ships[i].name); ImGui::PopID();
                    }
                    if(!shown)Hint("Совпадений нет.");
                    ImGui::EndCombo();
                }
                if(ImGui::Button("Создать и сесть"))SpawnSelected(ships[selected].name);
                ImGui::SameLine();if(ImGui::Button("В избранное") && g_state.favoriteCount<kMaxFavorites) {
                    bool exists=false;for(int i=0;i<g_state.favoriteCount;++i) if(strcmp(g_state.favorites[i],ships[selected].name)==0)exists=true;
                    if(!exists){strcpy_s(g_state.favorites[g_state.favoriteCount++],ships[selected].name);Persist();}
                }
            }
            for(int i=0;i<g_state.favoriteCount;++i) {ImGui::PushID(i);if(ImGui::Button(ReadableShipName(g_state.favorites[i])))SpawnSelected(g_state.favorites[i]);ImGui::SameLine();
                if(ImGui::SmallButton("Убрать")){for(int j=i+1;j<g_state.favoriteCount;++j)strcpy_s(g_state.favorites[j-1],g_state.favorites[j]);--g_state.favoriteCount;Persist();}ImGui::PopID();}
            static TravelPlace places[3000]; const int n=Travel_GetPlaces(places,3000);static int dest=0;
            if(n>0){if(dest>=n)dest=0;
                static char placeFilter[128]{};
                SearchBox("##explorationPlace","Поиск места или системы…",placeFilter,sizeof(placeFilter));
                if(ImGui::BeginCombo("Место исследования",places[dest].name)){
                    int shown=0;
                    for(int i=0;i<n;++i) {
                        const std::string extra=std::string(places[i].name)+" "+places[i].system;
                        if(!ListMatchesFilter(places[i].entity,extra.c_str(),placeFilter))continue;
                        ++shown; ImGui::PushID(i);
                        if(ImGui::Selectable(places[i].name,i==dest))dest=i;
                        ImGui::SetItemTooltip("%s",places[i].entity);ImGui::PopID();
                    }
                    if(!shown)Hint("Совпадений нет."); ImGui::EndCombo();
                }
                if(ImGui::Button("Переместиться"))Travel_RequestPlace(places[dest],2000);
                ImGui::SameLine();if(ImGui::Button("Задание: посетить")){g_visitTarget=places[dest];g_job=1;g_jobAt=GetTickCount();g_dwell=0;Note("Доберитесь до выбранного места и оставайтесь в его зоне 5 секунд.");}
            }
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Маршрут и гонка")) {
            static char name[64]="Точка";static float radius=150;
            ImGui::TextWrapped("Запишите минимум две точки, пролетая маршрут. Проверка идёт раз в секунду: радиус лучше делать от 150 м. Маршрут: задержка 3 с в точке. Гонка: время и рекорд. Телепортации не запрещены; это личная песочница, не соревновательный таймер.");
            ImGui::InputText("Название точки",name,sizeof(name));ImGui::SliderFloat("Радиус, м",&radius,10,1000);
            ImGui::BeginDisabled(g_job!=0);
            if(ImGui::Button("Добавить здесь") && g_state.routeCount<kMaxRoute){Spot s;char cls[64];if(Capture(s,cls)){auto& w=g_state.route[g_state.routeCount++];strncpy_s(w.name,name,_TRUNCATE);w.spot=s;w.radius=radius;g_state.bestRaceMs=0;Persist();}else Note("Не удалось прочитать позицию.");}
            ImGui::SameLine();if(ImGui::Button("Удалить последнюю") && g_state.routeCount){--g_state.routeCount;g_state.bestRaceMs=0;Persist();}
            ImGui::EndDisabled();
            if(g_state.routeCount>0) {
                if(g_pointTarget>=g_state.routeCount)g_pointTarget=0;
                static char routeFilter[128]{};
                SearchBox("##routeSearch","Поиск точки маршрута…",routeFilter,sizeof(routeFilter));
                if(ImGui::BeginCombo("Цель обследования / остановки",g_state.route[g_pointTarget].name)){
                    int shown=0;
                    for(int i=0;i<g_state.routeCount;++i) if(MatchesFilter(g_state.route[i].name,routeFilter)){
                        ++shown;ImGui::PushID(i);if(ImGui::Selectable(g_state.route[i].name,i==g_pointTarget))g_pointTarget=i;ImGui::PopID();
                    }
                    if(!shown)Hint("Совпадений нет.");ImGui::EndCombo();
                }
                ImGui::BeginDisabled(g_job!=0);
                if(ImGui::Button("Обследовать точку")){g_job=4;g_jobAt=GetTickCount();g_dwell=0;Note("Доберитесь до точки и оставайтесь внутри радиуса 10 секунд.");}
                ImGui::SameLine();if(ImGui::Button("Точная остановка")){g_job=5;g_jobAt=GetTickCount();g_dwell=0;Note("Остановите корабль у точки: скорость до 1 м/с в течение 15 секунд.");}
                ImGui::EndDisabled();
                ImGui::TextWrapped("Точная остановка подходит для тренировки посадки. Проверяются положение и скорость относительно зоны; касание поверхности и шасси не проверяются.");
            }
            ImGui::BeginDisabled(g_state.routeCount<2||g_job!=0);
            if(ImGui::Button("Начать маршрут")){g_job=2;g_stage=0;g_jobAt=GetTickCount();g_dwell=0;g_leftStart=false;Note("Вылетите за двойной радиус первой точки, затем вернитесь и пройдите точки по порядку.");}
            ImGui::SameLine();if(ImGui::Button("Начать гонку")){g_job=3;g_stage=0;g_jobAt=GetTickCount();g_dwell=0;g_leftStart=false;Note("Таймер запущен. Вылетите за двойной радиус первой точки и вернитесь к ней.");}
            ImGui::EndDisabled();
            if(g_job && ImGui::Button("Отменить задание")){g_job=0;Note("Задание отменено.");}
            if(g_job==2||g_job==3){ImGui::Text("Точка %d/%d; время %.1f с",g_stage+1,g_state.routeCount,(GetTickCount()-g_jobAt)/1000.0);double d=Distance(g_here,g_state.route[g_stage].spot);if(d>=0)ImGui::Text("До цели: %.0f м",d);else ImGui::TextUnformatted("Зона цели ещё не загружена.");}
            for(int i=0;i<g_state.routeCount;++i)ImGui::Text("%d. %s (%.0f м)",i+1,g_state.route[i].name,g_state.route[i].radius);
            if(g_state.bestRaceMs)ImGui::Text("Лучшее время: %.1f с",g_state.bestRaceMs/1000.0);
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Журнал")) {
            ImGui::Text("Очки исследования: %llu; выполнено заданий: %llu",g_state.points,g_state.completions);
            ImGui::TextWrapped("Очки — отдельная локальная награда; это не aUEC. Посещение фиксируется по входу в зону места из списка. Полное сохранение мира не выполняется.");
            for(int i=0;i<g_state.visitCount;++i)ImGui::BulletText("%s",g_state.visits[i]);
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Диагностика")) {
            ImGui::TextWrapped("Доступно означает, что мод нашёл нужный код. Это не подтверждает игровой тест. Отчёт сохраняется в data/capabilities-ru.txt.");
            if(ImGui::Button("Обновить отчёт"))WriteCapabilities();
            LocalCap caps[256];const auto n=h->caps(caps,256);
            for(size_t i=0;i<n && i<256;++i){ImGui::Text("%s: %s",caps[i].name,caps[i].ready?"доступно":"нет");if(caps[i].reason[0])ImGui::TextWrapped("%s",caps[i].reason);}
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}
const sco_plugin_info* Query(){return &kInfo;}
sco_result Load(const sco_api* api,sco_plugin* self) {
    g_api=api;g_self=self;
    if(!Adapter())return SCO_UNAVAILABLE;
    if(api->query_service(SCO_UI_NAME,SCO_UI_VERSION_1_0,reinterpret_cast<const void**>(&g_ui))!=SCO_OK)return SCO_UNAVAILABLE;
    g_store.Open(api,self);if(!g_store)return SCO_UNAVAILABLE;
    State loaded;const auto savedResult=g_store.S().Get("state.v1",loaded);
    if(savedResult==SCO_OK){if(Valid(loaded))g_state=loaded;else{Log("[exploration] invalid saved state; plugin stopped to preserve data");return SCO_BAD_ARG;}}
    else if(savedResult!=SCO_NOT_FOUND){Log("[exploration] saved state cannot be read; plugin stopped to preserve data");return savedResult;}
    g_store.S().Get("gear.xml",g_savedGear);if(g_savedGear.size()>1024*1024)g_savedGear.clear();
    api->query_service(SC_SPATIAL_SERVICE_NAME,SC_SPATIAL_SERVICE_VERSION,reinterpret_cast<const void**>(&g_sp));
    api->query_service(SC_SPAWN_SERVICE_NAME,SC_SPAWN_SERVICE_VERSION,reinterpret_cast<const void**>(&g_spawn));
    char value[16];g_autoPending=GetEnvironmentVariableA("SC_OFFLINE_RESUME",value,sizeof(value)) && _stricmp(value,"on")==0 && g_state.saved.n;
    g_autoSave=!(GetEnvironmentVariableA("SC_OFFLINE_AUTOSAVE",value,sizeof(value)) && _stricmp(value,"off")==0);
    auto r=api->subscribe(self,"tick",OnTick,nullptr);if(r!=SCO_OK){g_store.Close();return r;}
    if(g_ui->register_tab(self,"exploration.flight","Полёт и задания",250,Draw,nullptr)!=SCO_OK){g_store.Close();return SCO_FAILED;}
    Log("[exploration] loaded; saved=%s, resume=%s",g_state.saved.n?"yes":"no",g_autoPending?"on":"off");return SCO_OK;
}
void Unload(){g_store.Close();g_sp=nullptr;g_spawn=nullptr;}
}
SCO_EXPORT const sco_plugin_info* sco_plugin_query(){return Query();}
SCO_EXPORT sco_result sco_plugin_load(const sco_api* api,sco_plugin* self){return Load(api,self);}
SCO_EXPORT void sco_plugin_unload(){Unload();}
