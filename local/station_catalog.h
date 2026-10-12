#pragma once
#include <cstring>

// Local start identifiers, not game entity names. Coordinates come from the
// installed station's Child entry in the parent planet object container.
// Keep the parent explicit: three LEO stations share ObjectContainer_RestStop.
struct LocalStationStart { const char* id; const char* parent; double position[3]; };
inline const LocalStationStart kLocalStationStarts[] = {
    {"local_station_everus", "OOC_Stanton_1_Hurston", {-507742.3125,-903464.4375,496489.0625}},
    {"local_station_seraphim", "OOC_Stanton_2_Crusader", {5876098.201785,-1384206.723274,5565883.231907}},
    {"local_station_baijini", "OOC_Stanton_3_ArcCorp", {-771960.5625,-321347.21875,-359509.34375}},
    {"local_station_tressler", "OOC_Stanton_4_Microtech", {561471.046151,545853.017029,808831.762531}},
    {"local_station_grimhex", "OOC_Stanton_2c_Yela", {-568658.6875,383565.25,-2042}},
};
inline const LocalStationStart* FindLocalStationStart(const char* id) {
    for (const auto& station : kLocalStationStarts) if (std::strcmp(id, station.id)==0) return &station;
    return nullptr;
}
