#include "../plugins/exploration/exploration_state.h"
#include "../src/spawn_safety.h"
#include <cstdio>
#include <limits>
#include <sstream>
#include <type_traits>
using namespace exploration;
int main() {
    int failures=0;
    auto check=[&](bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;};
    static_assert(std::is_trivially_copyable_v<State>);
    check(SpawnClassError("Transport") != nullptr, "known fatal Transport actor blocked before engine creation");
    check(SpawnClassError(" \tTRANSPORT\t ") != nullptr, "Transport block handles case and surrounding whitespace");
    check(SpawnClassError(nullptr) && SpawnClassError("\t "), "empty spawn classes rejected");
    check(!SpawnClassError("PU_Human_Enemy_GroundCombat_NPC_ASD") &&
          !SpawnClassError("RSI_Polaris_Collector_Military") &&
          !SpawnClassError("TransportCarriage_VehicleElevators_Polaris_Exterior"),
          "concrete classes are not blocked by partial name matches");
    Spot here;here.n=2;strcpy_s(here.z[0].name,"room");strcpy_s(here.z[1].name,"Daymar");here.z[1].local[0]=100;
    Spot target;target.n=1;strcpy_s(target.z[0].name,"Daymar");target.z[0].local[0]=400;
    check(Distance(here,target)==300,"distance uses shared frame, not room coordinates");
    strcpy_s(target.z[0].name,"Pyro");check(Distance(here,target)<0,"different systems have no distance");
    Spot oldPlanet; oldPlanet.n=2; strcpy_s(oldPlanet.z[0].name,"pyro5b");
    strcpy_s(oldPlanet.z[1].name,"SolarSystem_1"); oldPlanet.z[1].local[0]=1.0e11;
    Spot monox; monox.n=2; strcpy_s(monox.z[0].name,"pyro2");
    strcpy_s(monox.z[1].name,"SolarSystem_1"); monox.z[1].local[0]=-1.0e11;
    check(Distance(oldPlanet,monox)>1.0e11,"unchanged player on old planet cannot confirm destination arrival");
    monox.z[1].local[0]=oldPlanet.z[1].local[0]+75;
    check(Distance(oldPlanet,monox)==75,"arrival can compare ancestor frame before destination zone streams in");
    uint32_t dwell=0;
    check(!Reached(10,150,1000,dwell,3000),"entering point starts dwell");
    check(!Reached(160,150,2000,dwell,3000)&&dwell==0,"leaving point resets dwell");
    check(!Reached(10,150,3000,dwell,3000)&&Reached(10,150,6000,dwell,3000),"continuous dwell completes checkpoint");
    check(!Reached(0,150,7000,dwell,0,false)&&dwell==0,"route cannot complete before departure");
    check(!Reached(std::numeric_limits<double>::quiet_NaN(),150,7000,dwell,0),"NaN cannot complete mission");
    State s;s.saved=here;s.routeCount=1;s.route[0].spot=here;strcpy_s(s.ship,"DRAK_Cutlass_Black");s.points=250;
    check(Valid(s),"valid session and route accepted");
    std::stringstream bytes;bytes.write(reinterpret_cast<const char*>(&s),sizeof(s));State restored;bytes.read(reinterpret_cast<char*>(&restored),sizeof(restored));
    check(Valid(restored)&&restored.points==250&&strcmp(restored.ship,s.ship)==0&&Distance(restored.saved,here)==0,"binary session roundtrip retains frame, ship and reward");
    restored.routeCount=33;check(!Valid(restored),"oversized route rejected");restored=s;restored.schema=999;check(!Valid(restored),"unknown save schema rejected");
    restored=s;restored.saved.z[0].local[0]=std::numeric_limits<double>::infinity();check(!Valid(restored),"non-finite saved coordinates rejected");
    restored=s;memset(restored.ship,'x',sizeof(restored.ship));check(!Valid(restored),"unterminated ship class rejected");
    dwell=0;check(!Reached(0,150,0xfffffff0u,dwell,30)&&Reached(0,150,20,dwell,30),"dwell handles tick counter wraparound");
    return failures?1:0;
}
