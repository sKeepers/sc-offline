#include "local_api.h"
#include "ui_bind.h"
#include "sco_api.h"
#include "sco_ui.h"
#include "scosdk/storage.hpp"
#include <string>
#include <vector>
#include <cstdio>
#include <algorithm>

namespace {
const sco_api* api=nullptr;
sco_plugin* self=nullptr;
const LocalFeaturesV1* h=nullptr;
const sco_ui_v1* ui=nullptr;
const sco_storage_v1* storage=nullptr;
std::vector<std::string> names;
sco::sdk::Storage db;
std::string selected;
char note[256]="Сначала наденьте снаряжение через вкладку «Игрок», затем сохраните комплект здесь.";
const sco_plugin_info info={sizeof(sco_plugin_info),SCO_API_MAJOR,SCO_API_MINOR,"loadout_presets","1.0.0","sc-offline RU Local"};
bool Adapter(){const void* p=nullptr;if(api->query_service(LOCAL_FEATURES_NAME,LOCAL_FEATURES_VERSION,&p)!=SCO_OK)return false;h=static_cast<const LocalFeaturesV1*>(p);return h && h->size>=sizeof(LocalFeaturesV1);}
void Note(const char* text){snprintf(note,sizeof(note),"%s",text);api->status(self,text);}
void Refresh(){
 names.clear();char key[256]{};std::string after;
 for(int i=0;i<128;++i){uint32_t n=sizeof(key);auto r=storage->next_key(self,"preset.",after.empty()?nullptr:after.c_str(),key,&n);if(r!=SCO_OK)break;after=key;names.push_back(after.substr(7));}
}
sco_result SavePreset(const char* name){
 if(!Adapter())return SCO_UNAVAILABLE;
 if(!name || !name[0] || strlen(name)>95)return SCO_BAD_ARG;
 const std::string key=std::string("preset.")+name;
 if(std::find(names.begin(),names.end(),std::string(name))!=names.end() || names.size()>=128)return SCO_BAD_ARG;
 uint64_t version=0;const auto bytes=h->last_xml(nullptr,0,&version);
 if(!bytes || bytes>1024*1024)return SCO_UNAVAILABLE;
 std::string xml(bytes,'\0');
 if(h->last_xml(xml.data(),bytes,&version)!=bytes)return SCO_FAILED;
 const auto r=db.Put(key.c_str(),std::string_view(xml));
 if(r==SCO_OK){selected=name;Refresh();}
 return r;
}
sco_result ApplyPreset(const char* name){
 if(!Adapter() || !h->loadout_ready())return SCO_UNAVAILABLE;
 if(!name || !name[0] || strlen(name)>95)return SCO_BAD_ARG;
 std::string xml;const auto key=std::string("preset.")+name;
 const auto r=db.Get(key.c_str(),xml);if(r!=SCO_OK)return r;
 if(xml.empty() || xml.size()>1024*1024)return SCO_BAD_ARG;
 h->equip_xml(xml.data(),static_cast<uint32_t>(xml.size()),"loadout_presets");
 return SCO_OK;
}
sco_result SaveCommand(const sco_arg* args,uint32_t count,void*,char* reply,uint32_t size){
 const auto r=count==1?SavePreset(args[0].v.s):SCO_BAD_ARG;
 snprintf(reply,size,"%s",r==SCO_OK?"Комплект сохранён.":"Комплект не сохранён: проверьте имя и наденьте снаряжение через мод.");return r;
}
sco_result ApplyCommand(const sco_arg* args,uint32_t count,void*,char* reply,uint32_t size){
 const auto r=count==1?ApplyPreset(args[0].v.s):SCO_BAD_ARG;
 snprintf(reply,size,"%s",r==SCO_OK?"Запрос надевания отправлен.":"Комплект не прочитан или персонаж ещё недоступен.");return r;
}
void Draw(void* frame,void*){
 sc_offline_imgui::Bind(frame);
 if(!Adapter()){ImGui::TextWrapped("Адаптер основной сборки недоступен.");return;}
 ImGui::TextWrapped("%s",note);
 ImGui::TextWrapped("Сохраняются комплекты, надетые через мод. Содержимое карманов, предметы из терминалов и груз корабля сюда не входят.");
 static char name[96]="Пилот";
 ImGui::InputText("Название комплекта",name,sizeof(name));
 uint64_t version=0;const auto bytes=h->last_xml(nullptr,0,&version);
 const std::string key=std::string("preset.")+name;
 const bool exists=std::find(names.begin(),names.end(),std::string(name))!=names.end();
 ImGui::BeginDisabled(!bytes || !name[0] || key.size()>255 || exists || names.size()>=128);
 if(ImGui::Button("Сохранить надетый через мод комплект",ImVec2(-1,0))){
  Note(SavePreset(name)==SCO_OK?"Комплект сохранён.":"Не удалось сохранить комплект.");
 }
 ImGui::EndDisabled();
 if(exists)ImGui::TextWrapped("Это имя уже используется. Укажите другое имя или удалите прежний комплект.");
 static char filter[128]{};ImGui::InputTextWithHint("##search","Поиск комплекта…",filter,sizeof(filter));
 int shown=0;
 if(ImGui::BeginChild("##presets",ImVec2(0,230),ImGuiChildFlags_Borders)){
  for(size_t i=0;i<names.size();++i)if(h->filter(names[i].c_str(),filter)){
   ++shown;ImGui::PushID(static_cast<int>(i));if(ImGui::Selectable(names[i].c_str(),selected==names[i]))selected=names[i];ImGui::PopID();
  }
  if(!shown)ImGui::TextDisabled("Комплектов нет или поиск не дал совпадений.");
 }
 ImGui::EndChild();
 ImGui::BeginDisabled(selected.empty() || !h->loadout_ready());
 if(ImGui::Button("Надеть выбранный комплект")){
  Note(ApplyPreset(selected.c_str())==SCO_OK?"Запрос надевания отправлен. Результат применения смотрите в mod.log.":"Не удалось прочитать комплект.");
 }
 ImGui::EndDisabled();
 ImGui::SameLine();ImGui::BeginDisabled(selected.empty());
 if(ImGui::Button("Удалить выбранный"))ImGui::OpenPopup("Удаление комплекта");
 ImGui::EndDisabled();
 if(ImGui::BeginPopupModal("Удаление комплекта",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
  ImGui::Text("Удалить «%s»?",selected.c_str());
  if(ImGui::Button("Удалить")){const auto k="preset."+selected;if(storage->del(self,k.c_str())==SCO_OK){selected.clear();Refresh();Note("Комплект удалён.");}else Note("Не удалось удалить комплект.");ImGui::CloseCurrentPopup();}
  ImGui::SameLine();if(ImGui::Button("Отмена"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
 }
}
}
SCO_EXPORT const sco_plugin_info* sco_plugin_query(){return &info;}
SCO_EXPORT sco_result sco_plugin_load(const sco_api* a,sco_plugin* p){
 api=a;self=p;
 if(!Adapter())return SCO_UNAVAILABLE;
 if(api->query_service(SCO_UI_NAME,SCO_UI_VERSION_1_0,reinterpret_cast<const void**>(&ui))!=SCO_OK)return SCO_UNAVAILABLE;
 if(api->query_service(SCO_STORAGE_NAME,SCO_STORAGE_VERSION_1_0,reinterpret_cast<const void**>(&storage))!=SCO_OK)return SCO_UNAVAILABLE;
 auto r=db.Open(api,self);if(r!=SCO_OK)return r;Refresh();
 static const sco_arg_def argument={"name",SCO_ARG_STRING,0,"Название комплекта"};
 sco_command command{};command.size=sizeof(command);command.arg_def_size=sizeof(sco_arg_def);command.args=&argument;command.nargs=1;
 command.name="loadout_presets.save";command.title="Сохранить комплект";command.help=command.title;command.fn=SaveCommand;
 r=api->register_command(self,&command);if(r!=SCO_OK)return r;
 command.name="loadout_presets.apply";command.title="Надеть комплект";command.help=command.title;command.fn=ApplyCommand;
 r=api->register_command(self,&command);if(r!=SCO_OK)return r;
 return ui->register_tab(self,"loadout_presets.main","Комплекты",150,Draw,nullptr);
}
SCO_EXPORT void sco_plugin_unload(){db={};names.clear();selected.clear();h=nullptr;}
