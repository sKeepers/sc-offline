#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
// Local adapter 1.0. POD data, borrowed strings; game-thread calls only.
struct ZoneFrame { uint64_t id; char name[96]; double origin[3]; double axis[3][3]; };
constexpr int kMaxZoneDepth=12;
struct ZoneSpot { char name[96]; double local[3]; };
struct Spot { int n=0; ZoneSpot z[kMaxZoneDepth]={}; };
struct MenuShip { char name[64]; int size; float length; };
enum TravelKind { Place_Planet,Place_Moon,Place_Other,Place_Minor };
struct TravelPlace { char system[32]; char name[64]; char entity[96]; double radius; int kind; char body[8]; };
struct LocalCap { char name[128]; char reason[256]; bool ready; };
#define LOCAL_FEATURES_NAME "loadout.local_features"
#define LOCAL_FEATURES_VERSION 0x00010000u
struct LocalFeaturesV1 {
 uint32_t size;
 const char* (*capture)(Spot&);
 int (*chain)(uint64_t,ZoneFrame*,int);
 const char* (*go)(const Spot&,DWORD,const char*);
 bool (*spawn_ready)();
 void (*spawn)(const char*,float,bool,bool,bool);
 int (*ship_count)();
 const MenuShip* (*ships)();
 int (*places)(TravelPlace*,int);
 bool (*has_startup)();
 void (*travel)(const TravelPlace&,float);
 const char* (*ship_label)(const char*);
 bool (*filter)(const char*,const char*);
 bool (*list_filter)(const char*,const char*,const char*);
 bool (*loadout_ready)();
 void (*equip_xml)(const char*,uint32_t,const char*);
 uint32_t (*last_xml)(char*,uint32_t,uint64_t*);
 void (*status)(const char*);
 size_t (*caps)(LocalCap*,size_t);
 bool (*data_path)(char*,DWORD,const char*);
};
