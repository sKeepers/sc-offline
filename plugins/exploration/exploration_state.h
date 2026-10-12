#pragma once
#include "local_api.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstdint>

namespace exploration {
constexpr uint32_t kSchema = 1;
constexpr int kMaxRoute = 32, kMaxVisits = 128, kMaxFavorites = 32;
struct Waypoint { char name[64]{}; Spot spot{}; double radius = 150; };
struct State {
    uint32_t schema = kSchema;
    Spot saved{};
    char ship[64]{};
    uint64_t savedAt = 0;
    int routeCount = 0, visitCount = 0, favoriteCount = 0;
    Waypoint route[kMaxRoute]{};
    char visits[kMaxVisits][96]{};
    char favorites[kMaxFavorites][64]{};
    uint64_t points = 0, completions = 0, bestRaceMs = 0;
};
inline bool ValidSpot(const Spot& s) {
    if (s.n < 0 || s.n > kMaxZoneDepth) return false;
    for (int i = 0; i < s.n; ++i) {
        if (!(std::find(std::begin(s.z[i].name), std::end(s.z[i].name), char(0)) != std::end(s.z[i].name)) || !s.z[i].name[0]) return false;
        for (double x : s.z[i].local) if (!std::isfinite(x) || std::abs(x) > 1e15) return false;
    }
    return true;
}
inline bool Valid(const State& s) {
    if (s.schema != kSchema || !ValidSpot(s.saved) || !(std::find(std::begin(s.ship), std::end(s.ship), char(0)) != std::end(s.ship)) ||
        s.routeCount < 0 || s.routeCount > kMaxRoute || s.visitCount < 0 || s.visitCount > kMaxVisits ||
        s.favoriteCount < 0 || s.favoriteCount > kMaxFavorites || s.points > 1000000000ull) return false;
    for (int i=0;i<s.routeCount;++i) if (!ValidSpot(s.route[i].spot) || !s.route[i].spot.n ||
        !(std::find(std::begin(s.route[i].name), std::end(s.route[i].name), char(0)) != std::end(s.route[i].name)) || !std::isfinite(s.route[i].radius) ||
        s.route[i].radius < 10 || s.route[i].radius > 10000) return false;
    for (int i=0;i<s.visitCount;++i) if (!(std::find(std::begin(s.visits[i]), std::end(s.visits[i]), char(0)) != std::end(s.visits[i]))) return false;
    for (int i=0;i<s.favoriteCount;++i) if (!(std::find(std::begin(s.favorites[i]), std::end(s.favorites[i]), char(0)) != std::end(s.favorites[i]))) return false;
    return true;
}
// Compare within the nearest shared named frame. Never persist streaming IDs.
inline double Distance(const Spot& here, const Spot& target) {
    for(int i=0;i<target.n;++i) for(int j=0;j<here.n;++j) if(strcmp(target.z[i].name,here.z[j].name)==0) {
        double d=0; for(int k=0;k<3;++k) {double v=target.z[i].local[k]-here.z[j].local[k]; d+=v*v;}
        return std::sqrt(d);
    }
    return -1;
}
inline bool Contains(const Spot& s, const char* zone) {
    for(int i=0;i<s.n;++i) if(strcmp(s.z[i].name,zone)==0) return true;
    return false;
}
inline bool Reached(double distance, double radius, uint32_t now, uint32_t& since, uint32_t dwellMs, bool armed=true) {
    if(!armed || distance<0 || !std::isfinite(distance) || distance>radius) {since=0;return false;}
    if(!since) since=now;
    return static_cast<uint32_t>(now-since)>=dwellMs;
}
}

