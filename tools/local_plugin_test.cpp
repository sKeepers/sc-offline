#include "../plugins/exploration/local_api.h"
#include "../plugins/exploration/exploration_state.h"
#include "../plugins/exploration/ui_bind.h"
#include "sco_api.h"
#include "sco_storage.h"
#include "sco_ui.h"
#include <map>
#include <string>
#include <cassert>
#include <iostream>
#include <fstream>

static std::map<std::string,std::string> values;
static LocalFeaturesV1 features{};
static sco_storage_v1 storage{};
static sco_ui_v1 ui{};
static sco_ui_draw_fn draw=nullptr;
static sco_event_fn tick=nullptr;
static bool available=true;
static std::string tab;
static std::map<std::string,sco_command_fn> commands;
static std::string lastXml,applied;
static sco_result Query(const char* name,uint32_t,const void** out){
 *out=nullptr;
 if(!strcmp(name,LOCAL_FEATURES_NAME) && available)*out=&features;
 if(!strcmp(name,SCO_STORAGE_NAME))*out=&storage;
 if(!strcmp(name,SCO_UI_NAME))*out=&ui;
 return *out?SCO_OK:SCO_NOT_FOUND;
}
int wmain(int argc,wchar_t** argv){
 assert(argc==3 || argc==4);
 features.size=sizeof(features);
 features.capture=[](Spot&)->const char*{return "no player in test";};
 features.last_xml=[](char* out,uint32_t n,uint64_t* v)->uint32_t{*v=1;if(out && n>=lastXml.size())memcpy(out,lastXml.data(),lastXml.size());return static_cast<uint32_t>(lastXml.size());};
 features.ship_count=[]()->int{return 0;};features.ships=[]()->const MenuShip*{return nullptr;};
 features.places=[](TravelPlace*,int)->int{return 0;};
 features.ship_label=[](const char* name)->const char*{return name;};
 features.loadout_ready=[]()->bool{return true;};
 features.equip_xml=[](const char* p,uint32_t n,const char*){applied.assign(p,n);};
 features.filter=[](const char*,const char*)->bool{return true;};
 storage.size=sizeof(storage);
 storage.get=[](sco_plugin*,const char* key,void* out,uint32_t* n)->sco_result{
  auto it=values.find(key);if(it==values.end()){*n=0;return SCO_NOT_FOUND;}
  auto size=static_cast<uint32_t>(it->second.size());if(*n<size){*n=size;return SCO_TOO_MANY;}memcpy(out,it->second.data(),size);*n=size;return SCO_OK;
 };
 storage.put=[](sco_plugin*,const char* key,const void* value,uint32_t n)->sco_result{values[key]=std::string(static_cast<const char*>(value),n);return SCO_OK;};
 storage.next_key=[](sco_plugin*,const char* prefix,const char* after,char* out,uint32_t* n)->sco_result{
  for(auto& [key,value]:values)if(key.rfind(prefix,0)==0 && (!after || key>after)){
   auto size=static_cast<uint32_t>(key.size()+1);if(*n<size){*n=size;return SCO_TOO_MANY;}memcpy(out,key.c_str(),size);*n=size;return SCO_OK;
  }return SCO_NOT_FOUND;
 };
 ui.size=sizeof(ui);ui.register_tab=[](sco_plugin*,const char* id,const char*,int32_t,sco_ui_draw_fn fn,void*)->sco_result{tab=id;draw=fn;return SCO_OK;};
 sco_api api{};api.size=sizeof(api);api.major=SCO_API_MAJOR;api.minor=SCO_API_MINOR;api.query_service=Query;
 api.log=[](sco_plugin*,sco_log_level,const char*){};api.status=[](sco_plugin*,const char*){};
 api.subscribe=[](sco_plugin*,const char*,sco_event_fn fn,void*)->sco_result{tick=fn;return SCO_OK;};
 api.register_command=[](sco_plugin*,const sco_command* c)->sco_result{commands[c->name]=c->fn;return SCO_OK;};
 for(int i=1;i<=2;++i){
  auto module=LoadLibraryW(argv[i]);assert(module);
  auto query=reinterpret_cast<sco_plugin_query_fn>(GetProcAddress(module,"sco_plugin_query"));
  auto load=reinterpret_cast<sco_plugin_load_fn>(GetProcAddress(module,"sco_plugin_load"));
  auto unload=reinterpret_cast<sco_plugin_unload_fn>(GetProcAddress(module,"sco_plugin_unload"));assert(query && load && unload);
  auto self=reinterpret_cast<sco_plugin*>(1);
  available=false;assert(load(&api,self)==SCO_UNAVAILABLE);unload();available=true;
  if(i==1){exploration::State old;old.points=321;old.routeCount=1;strcpy_s(old.route[0].name,"Старая точка");old.route[0].spot.n=1;strcpy_s(old.route[0].spot.z[0].name,"Stanton");values["state.v1"]=std::string(reinterpret_cast<const char*>(&old),sizeof(old));}
  if(i==1 && argc==4){std::ifstream f(argv[3],std::ios::binary);assert(f);values["state.v1"]=std::string(std::istreambuf_iterator<char>(f),{});std::cout<<"Old state bytes="<<values["state.v1"].size()<<", plugin layout="<<sizeof(exploration::State)<<std::endl;assert(values["state.v1"].size()==sizeof(exploration::State));exploration::State decoded;memcpy(&decoded,values["state.v1"].data(),sizeof(decoded));std::cout<<"Old state validation="<<exploration::Valid(decoded)<<", schema="<<decoded.schema<<std::endl;}
  const auto before=values;
  assert(load(&api,self)==SCO_OK);assert(draw);assert(tab==(i==1?"exploration.flight":"loadout_presets.main"));
  sc_offline_imgui::Bind(ImGui::CreateContext());
  auto& io=ImGui::GetIO();io.DisplaySize=ImVec2(780,900);io.DeltaTime=1.0f/60;io.IniFilename=nullptr;
  unsigned char* pixels=nullptr;int w=0,h=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
  ImGui::NewFrame();ImGui::Begin("Test");draw(ImGui::GetCurrentContext(),nullptr);ImGui::End();ImGui::Render();
  if(i==1){assert(tick);DWORD now=5000;tick("tick",&now,nullptr);assert(values==before);}
  available=false;
  ImGui::NewFrame();ImGui::Begin("Test");draw(ImGui::GetCurrentContext(),nullptr);ImGui::End();ImGui::Render();available=true;
  if(i==2){
   sco_arg arg{};arg.type=SCO_ARG_STRING;arg.v.s="Пилот";char reply[256];
   assert(commands.at("loadout_presets.save")(&arg,1,nullptr,reply,sizeof(reply))==SCO_UNAVAILABLE);
   lastXml="<Loadout><Items><Item itemName=\"test_helmet\"/></Items></Loadout>";
   assert(commands.at("loadout_presets.save")(&arg,1,nullptr,reply,sizeof(reply))==SCO_OK);
   assert(values.at("preset.Пилот")==lastXml);
   assert(commands.at("loadout_presets.save")(&arg,1,nullptr,reply,sizeof(reply))==SCO_BAD_ARG);
   assert(commands.at("loadout_presets.apply")(&arg,1,nullptr,reply,sizeof(reply))==SCO_OK);assert(applied==lastXml);
   unload();assert(load(&api,self)==SCO_OK);applied.clear();assert(commands.at("loadout_presets.apply")(&arg,1,nullptr,reply,sizeof(reply))==SCO_OK);assert(applied==lastXml);
   std::cout<<"PASS presets: empty capture refused, Cyrillic save, duplicate refusal, exact XML apply, reload persistence\n";
  }
  unload();ImGui::DestroyContext();assert(FreeLibrary(module));
  std::cout<<"PASS "<<(i==1?"exploration":"loadout_presets")<<": exports, adapter rejection, load, real ImGui draw, adapter withdrawal, unload\n";
 }
}
