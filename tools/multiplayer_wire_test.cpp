#include "../src/builtins/multiplayer_wire.h"
#include <cassert>
#include <iostream>
int main(){
 using namespace mpwire;
 uint8_t b[kMaxPose]{};const uint64_t peer=7;
 for(auto kind:{kAvatar,kShip}){
  const auto n=EncodeDespawn(kind,SessionId(peer,kind),b,kDespawn);
  assert(n==16);uint8_t decoded=0;assert(DecodeDespawn(b,n,peer,decoded)&&decoded==kind);
  assert(!DecodeDespawn(b,n,peer+1,decoded));
  for(size_t truncated=0;truncated<n;++truncated)assert(!DecodeDespawn(b,truncated,peer,decoded));
  assert(!DecodeDespawn(b,n+1,peer,decoded));
  b[2]=1;assert(!DecodeDespawn(b,n,peer,decoded));
  assert(EncodeDespawn(kind,SessionId(peer,kind),b,12)==0);
 }
 Hello h{};std::strcpy(h.build,"0.7.6 RU Local/net-2");auto n=EncodeHello(h,b,sizeof(b));Hello read{};
 assert(n && DecodeHello(b,n,read) && !std::strcmp(read.build,h.build) && read.poseVersion==kVersion);
 for(size_t t=0;t<n;++t)assert(!DecodeHello(b,t,read));
 assert(!DecodeHello(b,n+1,read));b[3]=1;assert(!DecodeHello(b,n,read));
 h.poseVersion=99;n=EncodeHello(h,b,sizeof(b));assert(DecodeHello(b,n,read)&&read.poseVersion!=kVersion);
 h.build[0]='\n';assert(!EncodeHello(h,b,sizeof(b)));
 Pose p{};p.seq=3;p.levels=1;std::strcpy(p.lv[0].name,"OOC_Stanton_1_Hurston");p.lv[0].pos[2]=1003000;p.lv[0].rot[3]=1;
 n=EncodePose(p,b,sizeof(b));Pose decoded{};assert(n && DecodePose(b,n,decoded)&&decoded.seq==3);
 for(size_t t=0;t<n;++t)assert(!DecodePose(b,t,decoded));
 std::cout<<"PASS despawn roundtrip/truncation/ownership, compatibility hello, unchanged pose protocol\n";
}
