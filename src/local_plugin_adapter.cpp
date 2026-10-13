#include "../plugins/exploration/local_api.h"
#include "menu.h"
#include "menu_ui.h"
#include "teleport.h"
#include "spawner.h"
#include "travel.h"
#include "loadout.h"
#include "sco_api.h"
#include "sco/caps.h"
#include <string>
#include <cstdio>
uint32_t CopyLastLocalLoadout(char*,uint32_t,uint64_t*);
static int Chain(uint64_t id,ZoneFrame* out,int max){
 if(!out || max<1 || max>kMaxZoneDepth)return 0;
 __try{auto z=ZoneFromId(id);return z?ReadZoneChain(z,out,max):0;}
 __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
static void Equip(const char* xml,uint32_t n,const char* tag){
 // Same check as before, shared with the tests (gear_attachments.h): old presets and the new
 // attachment XML are both plain <Loadout> documents up to 1 MiB.
 if(!gear::IsLoadoutXmlPayload(xml,n))return;
 EquipLoadoutXml(std::string(xml,n),tag?tag:"local.plugin");
}
static void Status(const char* text){SetMenuStatus("%s",text?text:"");}
static size_t Caps(LocalCap* out,size_t max){
 sco::caps::Entry entries[256];auto n=sco::caps::List(entries,256);
 if(max>256)max=256;
 for(size_t i=0;out && i<n && i<max;++i){snprintf(out[i].name,sizeof(out[i].name),"%s",entries[i].name);snprintf(out[i].reason,sizeof(out[i].reason),"%s",entries[i].reason);out[i].ready=entries[i].ready;}
 return n<max?n:max;
}
sco_result RegisterLocalPluginAdapter(const sco_api* api,sco_plugin* self){
 static const LocalFeaturesV1 table={sizeof(LocalFeaturesV1),CaptureCurrentSpot,Chain,GoToSpot,SpawnerReady,Menu_RequestSpawnClass,Menu_ShipCount,Menu_Ships,Travel_GetPlaces,Travel_HasStartup,Travel_RequestPlace,ReadableShipName,MatchesFilter,ListMatchesFilter,LoadoutApiReady,Equip,CopyLastLocalLoadout,Status,Caps,DataFilePath};
 return api->provide_service(self,LOCAL_FEATURES_NAME,LOCAL_FEATURES_VERSION,&table);
}
