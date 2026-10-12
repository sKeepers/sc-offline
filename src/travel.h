#pragma once
#include "../plugins/exploration/local_api.h"
#include "common.h"

// Location teleporter: a list of places grouped by star system (data\locations.txt plus whatever the
// in-game scan finds), and named saved spots (data\bookmarks.txt).


struct TravelBookmark { char system[32]; char name[64]; };

const char* Travel_ResolveSpawn(const TravelPlace& place, float altitude, uint64_t& zoneId, double local[3]);
void ProcessTravel(DWORD now);   // game thread

// Menu side (any thread)
int  Travel_GetPlaces(TravelPlace* out, int max);
int  Travel_PlacesVersion();                       // changes whenever the place list does
int  Travel_GetBookmarks(TravelBookmark* out, int max);
void Travel_CurrentSystem(char* out, size_t n);
bool Travel_Scanning(float& progress);
void Travel_RequestPlace(const TravelPlace& place, float altitude);
void Travel_RequestBookmark(int index);
void Travel_RequestSaveBookmark(const char* name);
void Travel_RequestDeleteBookmark(int index);
void Travel_RequestScan();
void Travel_LoadStartup(); // after quantum storage opens, before any world ticks
bool Travel_HasStartup();
void Travel_StartupDescription(char* out, size_t size);
void Travel_RequestStartupPlace(const TravelPlace& place, float altitude);
void Travel_RequestStartupHere();
void Travel_RequestDisableStartup();
