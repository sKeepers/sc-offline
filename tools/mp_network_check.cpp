// Independent diagnostic app using sco.net and the mod's codecs. No game/native APIs.
#include <windows.h>
#include "sco/net/core.h"
#include "sco/net/udp.h"
#include "sco/net/scope.h"
#include "../src/builtins/multiplayer_wire.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
using namespace sco::net;
static uint64_t Now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static std::string ConsoleLine(bool hidden=false){
 HANDLE in=GetStdHandle(STD_INPUT_HANDLE);DWORD old=0;
 if(!GetConsoleMode(in,&old))return {};
 DWORD mode=old|ENABLE_LINE_INPUT;
 if(hidden)mode&=~ENABLE_ECHO_INPUT;
 if(!SetConsoleMode(in,mode))return {};
 wchar_t line[130]{};DWORD n=0;BOOL ok=ReadConsoleW(in,line,129,&n,nullptr);
 SetConsoleMode(in,old);
 if(!ok)return {};
 while(n && (line[n-1]==L'\r'||line[n-1]==L'\n'))--n;
 int len=WideCharToMultiByte(CP_UTF8,0,line,n,nullptr,0,nullptr,nullptr);
 std::string result(len,'\0');WideCharToMultiByte(CP_UTF8,0,line,n,result.data(),len,nullptr,nullptr);
 SecureZeroMemory(line,sizeof(line));return result;
}
static std::string Password(){
 std::cout<<"Пароль проверки (от 8 до 128 байт UTF-8, ввод скрыт): "<<std::flush;
 auto pass=ConsoleLine(true);std::cout<<"\n";return pass;
}

struct Node {
 std::unique_ptr<udp::Socket> socket;
 std::unique_ptr<ScopedTransport> scoped;
 std::unique_ptr<Core> core;
 uint64_t receivedPose=0;bool hello=false,spawn=false,despawn=false,ack=false,ackSent=false;
 bool helloSent=false,spawnSent=false,poseSent=false,despawnSent=false;uint64_t lastSend=0;bool malformed=false;
 bool ready()const{return hello&&spawn&&receivedPose&&despawn;}
 bool open(bool loopback,const Scope& scope,uint16_t port){
  std::string why;socket=udp::Socket::Open(port,loopback,&why);
  if(!socket){std::cerr<<"Не удалось открыть UDP: "<<why<<"\n";return false;}
  scoped=std::make_unique<ScopedTransport>(*socket,scope);
  Callbacks cb;
  cb.state=[](bool active,std::string_view why){std::cout<<(active?"Соединение установлено":"Сессия завершена")<<"; "<<why<<"\n";};
  cb.message=[this](const Message& m){
   if(m.channel=="netcheck.hello") {mpwire::Hello h{};hello=mpwire::DecodeHello(m.data,m.len,h)&&h.poseVersion==mpwire::kVersion;malformed|=!hello;}
   else if(m.channel=="netcheck.spawn") {mpwire::Spawn s{};spawn=mpwire::DecodeSpawn(m.data,m.len,m.from,s);malformed|=!spawn;}
   else if(m.channel=="netcheck.pose") {mpwire::Pose p{};bool ok=mpwire::DecodePose(m.data,m.len,p);if(ok)++receivedPose;else malformed=true;}
   else if(m.channel=="netcheck.despawn") {uint8_t k=0;despawn=mpwire::DecodeDespawn(m.data,m.len,m.from,k);malformed|=!despawn;}
   else if(m.channel=="netcheck.ack") ack=m.len==1&&m.data[0]==1;
  };
  core=std::make_unique<Core>(*scoped,cb);
  return core->RegisterChannel("netcheck.hello",kReliable,mpwire::kMaxHello)&&core->RegisterChannel("netcheck.spawn",kReliable,mpwire::kMaxSpawn)&&
    core->RegisterChannel("netcheck.pose",0,mpwire::kMaxPose)&&core->RegisterChannel("netcheck.despawn",kReliable,mpwire::kDespawn)&&core->RegisterChannel("netcheck.ack",kReliable,1);
 }
 void tick(){
  core->Pump(Now());if(!core->Active()||core->Peers().size()<2)return;
  const auto now=Now();if(now-lastSend<100)return;lastSend=now;
  uint8_t b[mpwire::kMaxPose]{};size_t n=0;
  if(!helloSent){mpwire::Hello h{};std::strcpy(h.build,"netcheck/net-2");n=mpwire::EncodeHello(h,b,sizeof(b));helloSent=core->Send("netcheck.hello",b,uint32_t(n))==SendResult::Ok;}
  if(!spawnSent){mpwire::Spawn s{};s.kind=mpwire::kAvatar;s.sessionId=mpwire::SessionId(core->Self(),s.kind);std::strcpy(s.cls,"TEST_Only_No_Game_Entity");n=mpwire::EncodeSpawn(s,b,sizeof(b));spawnSent=core->Send("netcheck.spawn",b,uint32_t(n))==SendResult::Ok;}
  mpwire::Pose p{};p.levels=1;p.seq=uint32_t(now);std::strcpy(p.lv[0].name,"TEST_Local_Frame");p.lv[0].rot[3]=1;p.lv[0].pos[2]=100;
  n=mpwire::EncodePose(p,b,sizeof(b));poseSent=core->Send("netcheck.pose",b,uint32_t(n))==SendResult::Ok;
  if(!despawnSent){n=mpwire::EncodeDespawn(mpwire::kAvatar,mpwire::SessionId(core->Self(),mpwire::kAvatar),b,sizeof(b));despawnSent=core->Send("netcheck.despawn",b,uint32_t(n))==SendResult::Ok;}
  if(ready()&&!ackSent){const uint8_t done=1;ackSent=core->Send("netcheck.ack",&done,1)==SendResult::Ok;}
 }
 void report()const{
  const auto& s=core->GetStats();
  std::cout<<"Объявление версии: "<<hello<<"; создание: "<<spawn<<"; позиции: "<<receivedPose<<"; удаление: "<<despawn<<"; подтверждение: "<<ack<<"\n"
    <<"UDP получено/отправлено: "<<s.datagramsIn<<"/"<<s.datagramsOut<<"; ошибки подписи: "<<s.badTag<<"; вне диапазона: "<<scoped->Dropped()<<"; причина: "<<core->LastReason()<<"\n";
 }
};
static int SelfTest(){
 Scope scope;Node a,b;if(!a.open(true,scope,0)||!b.open(true,scope,0))return 1;
 Options h;h.passphrase="local-test-only";h.playerName="TestHost";h.pbkdf2Iters=1000;
 Options j=h;j.playerName="TestJoin";
 Endpoint ep;ParseAddress("127.0.0.1",a.socket->Port(),&ep);
 if(!a.core->Host(h,Now())||!b.core->Join(j,ep,Now()))return 1;
 const auto end=Now()+10000;
 while(Now()<end && !(a.ready()&&b.ready()&&a.ack&&b.ack)) {a.tick();b.tick();a.socket->Wait(5);}
 const bool good=a.ready()&&b.ready()&&a.ack&&b.ack&&!a.malformed&&!b.malformed;
 a.report();b.report();
 b.core->Leave("local-test-finished");const auto leftEnd=Now()+2000;
 while(Now()<leftEnd&&a.core->Peers().size()!=1){a.core->Pump(Now());a.socket->Wait(5);}
 const bool left=a.core->Peers().size()==1;
 std::cout<<(good&&left?"PASS":"FAIL")<<" loopback: authenticated bidirectional app packets + disconnect\n";
 return good&&left?0:1;
}
int main(int argc,char** argv){
 SetConsoleOutputCP(CP_UTF8);
 if(argc==2 && !std::strcmp(argv[1],"--self-test"))return SelfTest();
 if(argc!=1){std::cerr<<"Запустите без аргументов для двух ПК, или --self-test для localhost.\n";return 2;}
 std::cout<<"Проверка связи без игры. Собственные каналы netcheck; в игровую сессию не подключайтесь.\n1 — ведущий; 2 — подключиться: ";
 std::string role=ConsoleLine();if(role!="1"&&role!="2")return 2;
 std::cout<<(role=="1"?"VPN IPv4 друга: ":"VPN IPv4 ведущего: ");std::string address=ConsoleLine();
 Scope scope;Cidr remote;Endpoint ep;
 if(!ParseAddress(address,64123,&ep)||!ParseCidr(address+"/32",&remote)){std::cerr<<"Нужен числовой IPv4.\n";return 2;}scope.allow.push_back(remote);
 auto pass=Password();if(pass.size()<8 || pass.size()>128){std::cerr<<"Нужен пароль от 8 до 128 байт UTF-8.\n";return 2;}
 Node node;if(!node.open(false,scope,role=="1"?64123:0))return 1;
 Options opt;opt.passphrase=pass;opt.playerName=role=="1"?"CheckHost":"CheckJoin";opt.maxPeers=2;
 bool start=role=="1"?node.core->Host(opt,Now()):node.core->Join(opt,ep,Now());
 SecureZeroMemory(pass.data(),pass.size());SecureZeroMemory(opt.passphrase.data(),opt.passphrase.size());
 if(!start){std::cerr<<"Сессию не удалось запустить.\n";return 1;}
 std::cout<<"UDP "<<node.socket->Port()<<"; ведущий использует 64123. Ожидание: до 120 секунд.\n";
 const auto end=Now()+120000;uint64_t completeAt=0;
 while(Now()<end && node.core->GetState()!=State::Idle){
  node.tick();if(node.ready()&&node.ack){if(!completeAt)completeAt=Now();else if(Now()-completeAt>=1000)break;}
  node.socket->Wait(10);
 }
 bool good=node.ready()&&node.ack&&!node.malformed;node.report();node.core->Leave("diagnostic-complete");
 std::cout<<(good?"ПРОВЕРКА ПРОЙДЕНА":"ПРОВЕРКА НЕ ПРОЙДЕНА")<<". Это транспорт и пакеты; совместная игра не проверялась.\nНажмите Enter для выхода.\n";
 ConsoleLine();return good?0:1;
}
