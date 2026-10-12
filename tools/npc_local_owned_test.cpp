#include "../plugins/npc/npc.h"
#include "sc_actors.h"
#include "sc_spatial.h"
#include <map>
#include <string>
#include <cassert>
#include <cstring>
#include <cstdio>
static std::map<std::string,sco_command> commands;
static const npc_local_owned_v1* owned;
static sc_actors_v1 actors{};
static sc_spatial_v1 spatial{};
static uint64_t nextId=100;
static sco_result provideResult=SCO_OK;
static sco_result removeResult=SCO_OK;
static int ownerTag;
static sco_plugin* self=reinterpret_cast<sco_plugin*>(&ownerTag);
static int Has(const char*) { return 1; }
static void Log(sco_plugin*,sco_log_level,const char*) {}
static sco_result Register(sco_plugin*,const sco_command* c) { commands[c->name]=*c;return SCO_OK; }
static sco_result Query(const char* n,uint32_t,const void** out) {
    *out=!std::strcmp(n,SC_ACTORS_NAME)?static_cast<const void*>(&actors):static_cast<const void*>(&spatial);
    return SCO_OK;
}
static sco_result Provide(sco_plugin* owner,const char* name,uint32_t version,const void* table) {
    assert(owner==self && std::strcmp(name,NPC_LOCAL_OWNED_NAME)==0 && version==NPC_LOCAL_OWNED_VERSION);
    owned=static_cast<const npc_local_owned_v1*>(table);return provideResult;
}
static int Pose(double* p,double* q,uint64_t* z) { p[0]=p[1]=p[2]=0;q[0]=q[1]=q[2]=0;q[3]=1;*z=1;return 1; }
static sco_result Spawn(sco_plugin*,const char*,uint64_t,const double*,uint64_t* id) { *id=++nextId;return SCO_OK; }
static sco_result Remove(sco_plugin*,uint64_t) { return removeResult; }
static sco_result Invoke(const char* name,const sco_arg* args,uint32_t n) {
    char reply[256]{};const auto c=commands.at(name);return c.fn(args,n,c.ctx,reply,sizeof(reply));
}
int main() {
    actors.size=sizeof(actors);actors.spawn_npc=Spawn;actors.despawn=Remove;
    spatial.size=sizeof(spatial);spatial.player_pose=Pose;
    sco_api api{};api.size=sizeof(api);api.major=SCO_API_MAJOR;api.minor=SCO_API_MINOR;
    api.has=Has;api.log=Log;api.register_command=Register;api.query_service=Query;api.provide_service=Provide;
    assert(sco::features::npc::Load(&api,self)==SCO_OK && owned && owned->size==sizeof(*owned));
    assert(owned->latest_owned()==0);
    sco_arg args[2]{};args[0].type=SCO_ARG_STRING;args[0].v.s="SyntheticNPC";args[1].type=SCO_ARG_INT;args[1].v.i=2;
    assert(Invoke("npc.spawn",args,2)==SCO_OK && owned->latest_owned()==102);
    sco_arg id{};id.type=SCO_ARG_INT;id.v.i=102;
    removeResult=SCO_FAILED;assert(Invoke("npc.remove",&id,1)==SCO_FAILED && owned->latest_owned()==102);
    removeResult=SCO_OK;assert(Invoke("npc.remove",&id,1)==SCO_OK && owned->latest_owned()==101);
    id.v.i=999;assert(Invoke("npc.remove",&id,1)==SCO_NOT_FOUND && owned->latest_owned()==101);
    assert(Invoke("npc.clear",nullptr,0)==SCO_OK && owned->latest_owned()==0);
    sco::features::npc::Unload();assert(owned->latest_owned()==0);
    provideResult=SCO_FAILED;assert(sco::features::npc::Load(&api,self)==SCO_FAILED && owned->latest_owned()==0);
    provideResult=SCO_OK;assert(sco::features::npc::Load(&api,self)==SCO_OK && owned->latest_owned()==0);
    sco::features::npc::Unload();
    std::puts("PASS owned NPC service: identity, refusal retention, remove/clear, failed load, reload and unload");
}
