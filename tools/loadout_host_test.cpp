// Actual loadout builtin and actual sco-core registry/UI host, with no game or renderer.
// Game endpoints and UI draw/borrow helpers are stubs; this verifies admission and cleanup,
// not ImGui drawing, native equipment or game behavior.
#include "../src/builtins/builtins.h"
#include "../src/builtins/tabs.h"
#include "../src/menu.h"
#include "../src/teleport.h"
#include "sco/host.h"
#include "sco/runtime.h"
#include "sco/ui.h"
#include <fstream>
#include <regex>
#include <iostream>
TeleportApi g_tp;
// The inline builtin index references other features; they are deliberately not loaded here.
const sco::plugins::Builtin kTeleportBuiltin={},kSpawnBuiltin={},kCrewBuiltin={},kQuantumBuiltin={},
    kBuildBuiltin={},kContractsBuiltin={},kMiningBuiltin={},kMultiplayerBuiltin={};
static const sco_api* realApi;
static sco_plugin* playerOwner;
static sco_plugin* charactersOwner;
static sco_result adapterResult=SCO_OK;
static std::string charactersId;
static int failures=0,checks=0;
static void Check(bool result,const char* message){++checks;if(!result){++failures;std::cout<<"FAIL "<<message<<'\n';}}
void ProcessLoadout(){} void ProcessOutfits(){}
int Menu_GearCount(int){return 0;}
const char* Menu_GearName(int,int){return "";}
void Menu_RequestEquip(const int[Gear_SlotCount],const MenuWeaponAttachments*){}
int Menu_OutfitCount(){return 0;}
const char* Menu_OutfitName(int){return "";}
void Menu_RequestWearOutfit(int){}
void DrawPlayerTab(void*,void*){} void DrawSq42Tab(void*,void*){}
static void DrawCharacters(void*,void*){}
void RegisterPlayerTab(const sco_api* api,sco_plugin* self){playerOwner=self;RegisterBuiltinTab(api,self,"loadout.player","Игрок",kTabPlayer,DrawPlayerTab);}
void ResetPlayerTab(){playerOwner=nullptr;}
void RegisterLocalCharactersTab(const sco_api* api,sco_plugin* self){charactersOwner=self;RegisterBuiltinTab(api,self,charactersId.c_str(),"Персонажи",kTabNpcs,DrawCharacters);}
void ResetLocalCharactersTab(){charactersOwner=nullptr;}
sco_result RegisterLocalPluginAdapter(const sco_api*,sco_plugin*){return adapterResult;}
static bool Tab(const char* id){for(const auto& tab:sco::ui::Tabs())if(tab.id==id)return true;return false;}
static sco_result RefuseCommand(sco_plugin*,const sco_command*){return SCO_BAD_ARG;}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    std::ifstream input(argv[1]);const std::string source((std::istreambuf_iterator<char>(input)),{});
    std::smatch match;
    if(!std::regex_search(source,match,std::regex("RegisterBuiltinTab\\(api, self, \"([^\"]+)\"")))return 2;
    charactersId=match[1];
    sco::SetGameThread();realApi=sco::host::BuildApi({"Local5 host regression"});
    Check(sco::ui::Start()==sco::Result::Ok,"real core UI service starts");
    auto* owner=sco::host::NewPlugin("loadout");
    Check(kLoadoutBuiltin.load(realApi,owner)==SCO_OK,"actual loadout builtin loads through real command registry");
    Check(LoadoutBuiltinOwnsTick(),"successful load owns tick");
    Check(Tab("loadout.player")&&Tab("loadout.sq42")&&Tab("loadout.npcs"),"all three actual metadata ids admitted by real UI service");
    const sco::Command* commands[32]{};size_t count=sco::ListCommands(commands,32);bool equip=false,wear=false;
    for(size_t i=0;i<count;++i){equip|=std::string(commands[i]->name)=="loadout.equip";wear|=std::string(commands[i]->name)=="loadout.wear";}
    Check(equip&&wear,"both actual loadout commands registered");
    const void* service=nullptr;realApi->query_service(SCO_UI_NAME,SCO_UI_VERSION_1_0,&service);
    Check(service!=nullptr,"real UI table published");
    if(service){auto* ui=static_cast<const sco_ui_v1*>(service);Check(ui->register_tab(owner,"npc.npcs","Персонажи",500,DrawCharacters,nullptr)==SCO_BAD_ARG,"old cross-owner id refused by real core");}
    kLoadoutBuiltin.unload();sco::Release(owner);
    Check(!playerOwner&&!charactersOwner&&!LoadoutBuiltinOwnsTick(),"unload drops borrowed handles and tick");
    Check(!Tab("loadout.player")&&!Tab("loadout.npcs"),"real owner release withdraws UI");
    owner=sco::host::NewPlugin("loadout");adapterResult=SCO_FAILED;
    Check(kLoadoutBuiltin.load(realApi,owner)==SCO_FAILED,"late adapter failure returned");
    Check(!playerOwner&&!charactersOwner&&!LoadoutBuiltinOwnsTick(),"late failure drops both borrowed handles");sco::Release(owner);
    sco_api refusal=*realApi;refusal.register_command=RefuseCommand;
    owner=sco::host::NewPlugin("loadout");adapterResult=SCO_OK;
    Check(kLoadoutBuiltin.load(&refusal,owner)==SCO_BAD_ARG,"early command registration failure returned");
    Check(!playerOwner&&!charactersOwner&&!LoadoutBuiltinOwnsTick(),"early failure leaves no borrowed handles/tick");sco::Release(owner);
    sco::ui::Stop();std::cout<<checks<<" real-host admission/cleanup checks, "<<failures<<" failures\n";
    return failures?1:0;
}
