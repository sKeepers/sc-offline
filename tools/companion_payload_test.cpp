#include "../local/host/companion_payload_store.h"
#include <cstdio>
#include <cassert>
#include <iostream>
int main() {
 companion::PayloadStore store;
 companion::Payload p{};
 std::snprintf(p.xml,sizeof(p.xml),"<Graph marker='first'/>");
 std::snprintf(p.name,sizeof(p.name),"SCO_CompanionOrder_1");
 std::snprintf(p.path,sizeof(p.path),"Data/Libs/FlowgraphModules/first.xml");
 std::snprintf(p.npc,sizeof(p.npc),"200006373057");
 std::snprintf(p.player,sizeof(p.player),"14618994950939869184");
 const auto* first=store.add(p);assert(first && store.size()==1);
 assert(store.find("DATA/LIBS/FLOWGRAPHMODULES/FIRST.XML")==first);
 // A subsequent command and mutations of the caller's buffers cannot alter the first graph/IDs.
 for(int i=2;i<=16;++i) {
  std::snprintf(p.xml,sizeof(p.xml),"<Graph marker='%d'/>",i);
  std::snprintf(p.path,sizeof(p.path),"Data/Libs/FlowgraphModules/order%d.xml",i);
  std::snprintf(p.name,sizeof(p.name),"SCO_CompanionOrder_%d",i);
  std::snprintf(p.npc,sizeof(p.npc),"%d",i);
  assert(store.add(p));
  assert(store.find("Data/Libs/FlowgraphModules/first.xml")==first);
  assert(std::strcmp(first->npc,"200006373057")==0);
  assert(std::strcmp(first->xml,"<Graph marker='first'/>")==0);
 }
 assert(!store.add(p));assert(store.size()==16);
 companion::PayloadStore invalid;
 assert(!invalid.add({}));
 p={};std::memset(p.path,'x',sizeof(p.path));p.xml[0]='x';p.name[0]='n';p.npc[0]='1';p.player[0]='2';
 assert(!invalid.add(p));assert(invalid.size()==0);
 companion::PayloadStore duplicate;
 p={};std::snprintf(p.path,sizeof(p.path),"same.xml");p.xml[0]='x';p.name[0]='n';p.npc[0]='1';p.player[0]='2';
 assert(duplicate.add(p));assert(!duplicate.add(p));
 assert(!duplicate.find(nullptr));assert(!duplicate.find("absent.xml"));
 std::cout<<"PASS payload lifetime, exact lookup, caller independence, limit, duplicate and invalid input\n";
}
