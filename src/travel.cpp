#include "travel.h"
#include "teleport.h"
#include "spawner.h"
#include "menu.h"
#include "builtins/builtin_store.h"
#include "../plugins/exploration/exploration_state.h"
#include <share.h>
#include <cctype>
#include <algorithm>
#include "../local/station_catalog.h"

// =============================================================================================
// Shared state. The menu thread reads snapshots; the game thread owns every change.
// =============================================================================================

constexpr int kMaxPlaces    = 3000;
constexpr int kMaxBookmarks = 400;

static SRWLOCK        g_travelLock = SRWLOCK_INIT;
static TravelPlace    g_places[kMaxPlaces];
static int            g_placeCount = 0;
static TravelBookmark g_marks[kMaxBookmarks];
static Spot           g_markSpots[kMaxBookmarks];   // game thread only
static int            g_markCount = 0;
static char           g_currentSystem[32] = "";
static volatile LONG  g_placesVersion = 0;      // bumped whenever g_places changes

struct StartupDestination {
    uint32_t schema = 1;
    int mode = 0; // 0 normal launcher start, 1 listed place, 2 exact foot position
    TravelPlace place{};
    float altitude = 1000;
    Spot spot{};
};
static StartupDestination g_startup;
static bool g_startupPending = false;
static bool g_startupRelocationSent = false;
static char g_launcherPlace[96] = {};
static char g_launcherShip[64] = {};

static bool ValidStartup(const StartupDestination& s) {
    if (s.schema != 1 || s.mode < 0 || s.mode > 2 || !exploration::ValidSpot(s.spot)) return false;
    if (s.mode != 1) return s.mode != 2 || s.spot.n > 0;
    return (std::find(std::begin(s.place.name), std::end(s.place.name), char(0)) != std::end(s.place.name)) && s.place.name[0] &&
        (std::find(std::begin(s.place.entity), std::end(s.place.entity), char(0)) != std::end(s.place.entity)) && s.place.entity[0] &&
        (std::find(std::begin(s.place.system), std::end(s.place.system), char(0)) != std::end(s.place.system)) && (std::find(std::begin(s.place.body), std::end(s.place.body), char(0)) != std::end(s.place.body)) &&
        std::isfinite(s.place.radius) && s.place.radius >= 0 && s.place.radius < 1e10 &&
        s.place.kind >= Place_Planet && s.place.kind <= Place_Minor &&
        std::isfinite(s.altitude) && s.altitude >= 100 && s.altitude <= 20000;
}
void Travel_LoadStartup() {
    StartupDestination saved;
    if (g_quantumStore && g_quantumStore.S().Get("startup.v1", saved) == SCO_OK) {
        if (ValidStartup(saved)) g_startup = saved;
        else Log("[travel-start] invalid startup record ignored");
    }
    char choice[128] = {};
    const DWORD n = GetEnvironmentVariableA("SC_OFFLINE_START", choice, sizeof(choice));
    if (n && n < sizeof(choice) && strncmp(choice, "place:", 6) == 0 && strlen(choice + 6) < sizeof(g_launcherPlace)) {
        strcpy_s(g_launcherPlace, choice + 6);
        GetEnvironmentVariableA("SC_OFFLINE_START_SHIP", g_launcherShip, sizeof(g_launcherShip));
        g_startup = {}; g_startup.mode = 1; // resolved against the same catalogue on the first tick
    } else if (_stricmp(choice, "Daymar") == 0 || _stricmp(choice, "normal") == 0 || _stricmp(choice, "resume") == 0) {
        g_startup = {}; SuppressSavedSpotAutostart();
    } // explicit launcher choice wins over old menu start
    g_startupPending = g_startup.mode != 0;
    if (g_startupPending) { CancelDaymarStart(); SuppressSavedSpotAutostart(); }
    Log("[travel-start] loaded mode=%d", g_startup.mode);
}
bool Travel_HasStartup() {
    AcquireSRWLockShared(&g_travelLock);
    const bool enabled = g_startup.mode != 0;
    ReleaseSRWLockShared(&g_travelLock); return enabled;
}
void Travel_StartupDescription(char* out, size_t size) {
    AcquireSRWLockShared(&g_travelLock);
    if (g_startup.mode == 1) snprintf(out, size, "Старт: %s / %s", g_startup.place.system, g_startup.place.name);
    else if (g_startup.mode == 2) snprintf(out, size, "Старт в сохранённой точке: %s", g_startup.spot.z[0].name);
    else snprintf(out, size, "Старт: настройки лаунчера");
    ReleaseSRWLockShared(&g_travelLock);
}
static void SaveStartup(const StartupDestination& destination) {
    if (!ValidStartup(destination) || !g_quantumStore || g_quantumStore.S().Put("startup.v1", destination) != SCO_OK) {
        SetMenuStatus("Не удалось сохранить стартовую точку. Предыдущая настройка оставлена."); return;
    }
    AcquireSRWLockExclusive(&g_travelLock); g_startup = destination; ReleaseSRWLockExclusive(&g_travelLock);
    SetMenuStatus(destination.mode ? "Стартовая точка сохранена для следующего запуска. Автосохранение её не меняет." : "Выбор стартовой точки отключён. Следующий запуск использует настройки лаунчера.");
}
enum ReqKind { Req_None, Req_Place, Req_Bookmark, Req_Save, Req_Delete, Req_Scan, Req_StartPlace, Req_StartHere, Req_StartOff };
static struct { int kind; TravelPlace place; float altitude; int index; char name[64]; } g_req;

static void Queue(int kind, const TravelPlace* place = nullptr, float altitude = 0, int index = -1, const char* name = nullptr) {
    AcquireSRWLockExclusive(&g_travelLock);
    g_req = {};
    g_req.kind = kind;
    if (place) g_req.place = *place;
    g_req.altitude = altitude;
    g_req.index = index;
    if (name) strncpy_s(g_req.name, name, _TRUNCATE);
    ReleaseSRWLockExclusive(&g_travelLock);
}

void Travel_RequestPlace(const TravelPlace& place, float altitude) { Queue(Req_Place, &place, altitude); }
void Travel_RequestBookmark(int index)                            { Queue(Req_Bookmark, nullptr, 0, index); }
void Travel_RequestSaveBookmark(const char* name)                 { Queue(Req_Save, nullptr, 0, -1, name); }
void Travel_RequestDeleteBookmark(int index)                      { Queue(Req_Delete, nullptr, 0, index); }
void Travel_RequestScan()                                         { Queue(Req_Scan); }
void Travel_RequestStartupPlace(const TravelPlace& p, float altitude) { Queue(Req_StartPlace, &p, altitude); }
void Travel_RequestStartupHere() { Queue(Req_StartHere); }
void Travel_RequestDisableStartup() { Queue(Req_StartOff); }

int Travel_PlacesVersion() { return g_placesVersion; }

int Travel_GetPlaces(TravelPlace* out, int max) {
    AcquireSRWLockShared(&g_travelLock);
    const int n = g_placeCount < max ? g_placeCount : max;
    memcpy(out, g_places, n * sizeof(TravelPlace));
    ReleaseSRWLockShared(&g_travelLock);
    return n;
}

int Travel_GetBookmarks(TravelBookmark* out, int max) {
    AcquireSRWLockShared(&g_travelLock);
    const int n = g_markCount < max ? g_markCount : max;
    memcpy(out, g_marks, n * sizeof(TravelBookmark));
    ReleaseSRWLockShared(&g_travelLock);
    return n;
}

void Travel_CurrentSystem(char* out, size_t n) {
    AcquireSRWLockShared(&g_travelLock);
    strncpy_s(out, n, g_currentSystem, _TRUNCATE);
    ReleaseSRWLockShared(&g_travelLock);
}

// =============================================================================================
// Files
// =============================================================================================

static void Trim(char* s) {
    char* start = s;
    while (*start == ' ' || *start == '\t') ++start;
    if (start != s) memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);
    while (len && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) s[--len] = 0;
}

static void Capitalize(char* s) { if (s[0] >= 'a' && s[0] <= 'z') s[0] -= 'a' - 'A'; }

static void Underscores(char* s) { for (char* c = s; *c; ++c) if (*c == '_') *c = ' '; }

// Interiors and small zones: real zones, but not places you'd pick from a list.
static bool IsMinorZone(const char* n) {
    static const char* const kMinor[] = {
        "_int", "int_", "entrance_", "elev", "lobby", "restaurant", "sfce", "ObjectContainer", "_segment",
        "social_", "gascloud", "LocationHarvestable", "hangar", "rstop_hatch", "rs_habs", "rs_cargo", "rs_cz_",
        "rs_comm_", "rs_entry_", "deck", "dummy", "_occu", "ab_mine_", "rs_refin", "reststop_",
    };
    char low[96];
    strncpy_s(low, n, _TRUNCATE);
    for (char* c = low; *c; ++c) *c = static_cast<char>(tolower(static_cast<unsigned char>(*c)));
    for (const char* m : kMinor) {
        char lm[32];
        strncpy_s(lm, m, _TRUNCATE);
        for (char* c = lm; *c; ++c) *c = static_cast<char>(tolower(static_cast<unsigned char>(*c)));
        if (strstr(low, lm)) return true;
    }
    return false;
}

// What the game calls its places, per system:
//   OOC_Stanton_2b_Daymar        Stanton, moon 2b, "Daymar"
//   OOC_Stanton_4_Microtech      Stanton, planet 4
//   OOC_Stanton1_L3              Stanton, Lagrange point L3 of body 1   (shown as "Hurston L3")
//   OOC_Stanton2a_CommArray      Stanton, comm array near body 2a
//   OOC_JumpPoint_stanton_pyro   Stanton, "Jump point to Pyro"
//   jumppoint_pyro_nyx           Pyro, "Jump point to Nyx"
//   pyro2 / pyro5a               Pyro, planet 2 / moon 5a
//   P3_L2                        (system from its zone), Lagrange point L2 of body 3
// Anything else keeps its own name, with the system taken from the zone it sits in.
// Returns false when nothing here applies; p.system is "" when only the zone can tell.
static bool DescribeEntity(const char* entity, TravelPlace& p) {
    p.kind = Place_Other;
    p.body[0] = 0;
    p.system[0] = 0;
    strncpy_s(p.entity, entity, _TRUNCATE);

    const char* jp = nullptr;
    if (_strnicmp(entity, "OOC_JumpPoint_", 14) == 0) jp = entity + 14;
    else if (_strnicmp(entity, "jumppoint_", 10) == 0) jp = entity + 10;
    if (jp) {
        const size_t len = strcspn(jp, "_");
        if (!len || len >= sizeof(p.system) || !jp[len]) return false;
        memcpy(p.system, jp, len);
        p.system[len] = 0;
        Capitalize(p.system);
        char to[48];
        strncpy_s(to, jp + len + 1, _TRUNCATE);
        Capitalize(to);
        Underscores(to);
        snprintf(p.name, sizeof(p.name), "Jump point to %s", to);
        return true;
    }

    // pyro2, pyro5a: a system word, one digit, maybe a moon letter
    {
        size_t letters = 0;
        while (isalpha(static_cast<unsigned char>(entity[letters]))) ++letters;
        const char* d = entity + letters;
        static const char* const kSystems[] = { "stanton", "pyro", "nyx", "castra", "terra", "magnus", "odin", "sol" };
        bool known = false;
        for (const char* k : kSystems) known |= strlen(k) == letters && _strnicmp(entity, k, letters) == 0;
        if (known && letters < sizeof(p.system) && isdigit(static_cast<unsigned char>(d[0]))
            && (d[1] == 0 || (isalpha(static_cast<unsigned char>(d[1])) && d[2] == 0))) {
            memcpy(p.system, entity, letters);
            p.system[letters] = 0;
            Capitalize(p.system);
            strncpy_s(p.body, d, _TRUNCATE);
            p.kind = d[1] ? Place_Moon : Place_Planet;
            snprintf(p.name, sizeof(p.name), "%s %s", p.system, d);
            return true;
        }
    }

    // P3_L2: Lagrange point of body 3
    if ((entity[0] == 'P' || entity[0] == 'p') && isdigit(static_cast<unsigned char>(entity[1])) && entity[2] == '_'
        && (entity[3] == 'L' || entity[3] == 'l') && isdigit(static_cast<unsigned char>(entity[4])) && entity[5] == 0) {
        p.body[0] = entity[1];
        p.body[1] = 0;
        snprintf(p.name, sizeof(p.name), "L%c", entity[4]);
        return true;
    }

    if (_strnicmp(entity, "OOC_", 4) != 0) return false;
    const char* rest = entity + 4;
    const size_t tokLen = strcspn(rest, "_");
    if (!tokLen || !rest[tokLen]) return false;
    size_t letters = 0;
    while (letters < tokLen && isalpha(static_cast<unsigned char>(rest[letters]))) ++letters;
    if (!letters || letters >= sizeof(p.system)) return false;
    memcpy(p.system, rest, letters);
    p.system[letters] = 0;
    const char* after = rest + tokLen + 1;

    if (letters < tokLen) {                                   // OOC_Stanton1a_CommArray
        const size_t idx = tokLen - letters;
        if (idx < sizeof(p.body)) { memcpy(p.body, rest + letters, idx); p.body[idx] = 0; }
        strncpy_s(p.name, after, _TRUNCATE);
        Underscores(p.name);
        return true;
    }
    const size_t digits = strspn(after, "0123456789");          // OOC_Stanton_2b_Daymar
    const size_t t2 = strcspn(after, "_");
    if (digits && after[t2] == '_' && t2 < sizeof(p.body)) {
        if (t2 == digits) p.kind = Place_Planet;
        else if (t2 == digits + 1 && isalpha(static_cast<unsigned char>(after[digits]))) p.kind = Place_Moon;
        if (p.kind != Place_Other) {
            memcpy(p.body, after, t2);
            p.body[t2] = 0;
            strncpy_s(p.name, after + t2 + 1, _TRUNCATE);
            Underscores(p.name);
            return true;
        }
    }
    strncpy_s(p.name, after, _TRUNCATE);
    Underscores(p.name);
    return true;
}

// "L3" near body 1 -> "Hurston L3", once Hurston (planet 1) is in the list. Caller holds the lock.
static void NameByBody() {
    ++g_placesVersion;
    for (int i = 0; i < g_placeCount; ++i) {
        TravelPlace& p = g_places[i];
        if (p.kind != Place_Other || !p.body[0]) continue;
        for (int j = 0; j < g_placeCount; ++j) {
            const TravelPlace& b = g_places[j];
            if (b.kind == Place_Other || _stricmp(b.body, p.body) != 0 || _stricmp(b.system, p.system) != 0) continue;
            if (_strnicmp(p.name, b.name, strlen(b.name)) == 0) break;   // already renamed
            char renamed[64];
            snprintf(renamed, sizeof(renamed), "%s %s", b.name, p.name);
            strcpy_s(p.name, renamed);
            break;
        }
    }
}

static bool HavePlace(const char* entity) {
    for (int i = 0; i < g_placeCount; ++i)
        if (_stricmp(g_places[i].entity, entity) == 0) return true;
    return false;
}

static bool AddPlace(const TravelPlace& p) {     // caller holds the lock
    if (g_placeCount >= kMaxPlaces || HavePlace(p.entity)) return false;
    g_places[g_placeCount++] = p;
    ++g_placesVersion;
    return true;
}

// Line format: system | name shown | entity name | radius in metres (0 = unknown)
static void LoadPlaces(const char* file, bool curated) {
    char path[MAX_PATH];
    if (!DataFilePath(path, sizeof(path), file)) return;
    FILE* f = _fsopen(path, "r", _SH_DENYNO);
    if (!f) return;
    char line[512];
    int added = 0, stale = 0;
    AcquireSRWLockExclusive(&g_travelLock);
    while (fgets(line, sizeof(line), f)) {
        Trim(line);
        if (!line[0] || line[0] == '#') continue;
        char* field[4] = {};
        int n = 0;
        for (char* p = line; p && n < 4; ++n) {
            field[n] = p;
            p = strchr(p, '|');
            if (p) *p++ = 0;
        }
        if (n < 3) continue;
        for (int i = 0; i < n; ++i) Trim(field[i]);
        TravelPlace p = {};
        const bool parsed = DescribeEntity(field[2], p);
        if (!parsed) {
            strncpy_s(p.name, field[2], _TRUNCATE);
            Underscores(p.name);
            p.kind = IsMinorZone(field[2]) ? Place_Minor : Place_Other;
        }
        // Curated lines (locations.txt) choose their own names; found ones are re-read from the entity
        // name, so older files with rough names get the current naming.
        if (field[0][0] && (curated || !p.system[0])) strncpy_s(p.system, field[0], _TRUNCATE);
        if (field[1][0] && curated) strncpy_s(p.name, field[1], _TRUNCATE);
        // System zone ids change from session to session, so a "SolarSystem_<id>" saved earlier is meaningless.
        if (_strnicmp(p.system, "SolarSystem", 11) == 0) { ++stale; continue; }
        p.radius = n > 3 ? atof(field[3]) : 0.0;
        if (p.entity[0] && AddPlace(p)) ++added;
    }
    NameByBody();
    ReleaseSRWLockExclusive(&g_travelLock);
    fclose(f);
    Log("[travel] %d places from %s%s", added, file, stale ? " (older entries with unnamed systems skipped; run the scan again)" : "");
}

static void AppendFoundPlace(const TravelPlace& p) {
    char path[MAX_PATH];
    if (!DataFilePath(path, sizeof(path), "locations_found.txt")) return;
    const bool fresh = GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES;
    FILE* f = _fsopen(path, "a", _SH_DENYNO);
    if (!f) return;
    if (fresh) fprintf(f, "# Places found by the in-game scan (Travel tab). Same format as locations.txt.\n"
                          "# Radius 0 = unknown; put the real radius in metres to arrive right above the surface.\n");
    fprintf(f, "%s | %s | %s | 0\n", p.system, p.name, p.entity);
    fclose(f);
}

static void WriteBookmarksFile() {
    char path[MAX_PATH];
    if (!DataFilePath(path, sizeof(path), "bookmarks.txt")) return;
    FILE* f = _fsopen(path, "w", _SH_DENYNO);
    if (!f) { Log("[travel] couldn't write %s", path); return; }
    fprintf(f, "# Saved spots from the Travel tab.\n");
    for (int i = 0; i < g_markCount; ++i) {
        fprintf(f, "bookmark %s|%s\n", g_marks[i].system, g_marks[i].name);
        for (int z = 0; z < g_markSpots[i].n; ++z) {
            const ZoneSpot& e = g_markSpots[i].z[z];
            fprintf(f, "spot %.6f %.6f %.6f %s\n", e.local[0], e.local[1], e.local[2], e.name);  // secret-scan: allow e.local is a struct member, not a hostname
        }
    }
    fclose(f);
}

static void ReadBookmarksFile() {
    char path[MAX_PATH];
    if (!DataFilePath(path, sizeof(path), "bookmarks.txt")) return;
    FILE* f = _fsopen(path, "r", _SH_DENYNO);
    if (!f) return;
    char line[256];
    AcquireSRWLockExclusive(&g_travelLock);
    g_markCount = 0;
    int cur = -1;
    while (fgets(line, sizeof(line), f)) {
        Trim(line);
        if (strncmp(line, "bookmark ", 9) == 0) {
            if (g_markCount >= kMaxBookmarks) break;
            cur = g_markCount++;
            g_marks[cur] = {};
            g_markSpots[cur] = Spot{};
            char* bar = strchr(line + 9, '|');
            if (bar) { *bar = 0; strncpy_s(g_marks[cur].system, line + 9, _TRUNCATE); strncpy_s(g_marks[cur].name, bar + 1, _TRUNCATE); }
            else strncpy_s(g_marks[cur].name, line + 9, _TRUNCATE);
        } else if (cur >= 0 && strncmp(line, "spot ", 5) == 0 && g_markSpots[cur].n < kMaxZoneDepth) {
            ZoneSpot& e = g_markSpots[cur].z[g_markSpots[cur].n];
            if (sscanf_s(line, "spot %lf %lf %lf %95[^\r\n]", &e.local[0], &e.local[1], &e.local[2],  // secret-scan: allow e.local is a struct member, not a hostname
                         e.name, static_cast<unsigned>(sizeof(e.name))) == 4)
                ++g_markSpots[cur].n;
        }
    }
    // drop empty bookmarks
    int kept = 0;
    for (int i = 0; i < g_markCount; ++i)
        if (g_markSpots[i].n) { g_marks[kept] = g_marks[i]; g_markSpots[kept] = g_markSpots[i]; ++kept; }
    g_markCount = kept;
    ReleaseSRWLockExclusive(&g_travelLock);
    fclose(f);
}

// The saved spots in the quantum built-in's storage (data/storage/quantum.db): one row each, in the
// list's order, the spot as bookmarks.txt's "spot" lines. "bookmarks.txt.time" is bookmarks.txt's
// last-write time when it was imported (builtins/builtin_store.h has the rule). bookmarks.txt is no
// longer written while storage works.
constexpr const char* kMarksTable =
    "CREATE TABLE IF NOT EXISTS bookmarks(pos INTEGER PRIMARY KEY, system TEXT NOT NULL, name TEXT NOT NULL, spots TEXT NOT NULL)";
constexpr const char* kMarksTimeKey = "bookmarks.txt.time";

// The whole list in one transaction: a crash leaves the old list or the new one, never half of it.
// fileTime 0 leaves the recorded time as it is. Game thread (it owns g_marks).
static bool StoreBookmarks(int64_t fileTime) {
    using sco::sdk::SqlInt;
    using sco::sdk::SqlText;
    BuiltinStore& st = g_quantumStore;
    sco::sdk::StorageTransaction tx(st.S());
    sco_result r = tx.Result();
    if (r == SCO_OK) r = st.S().Exec(kMarksTable);
    if (r == SCO_OK) r = st.S().Exec("DELETE FROM bookmarks");
    std::string spots;
    for (int i = 0; r == SCO_OK && i < g_markCount; ++i) {
        SpotToText(g_markSpots[i], spots);
        r = st.S().Exec("INSERT INTO bookmarks(pos, system, name, spots) VALUES (?, ?, ?, ?)",
                        { SqlInt(i), SqlText(g_marks[i].system), SqlText(g_marks[i].name), SqlText(spots) });
    }
    if (r == SCO_OK && fileTime) r = st.PutInt(kMarksTimeKey, fileTime);
    if (r == SCO_OK) r = tx.Commit();
    if (r != SCO_OK) st.Failed("saving the saved spots", r, "using bookmarks.txt");
    return r == SCO_OK;
}

static sco_result ReadStoredBookmarks() {
    sco::sdk::Storage& s = g_quantumStore.S();
    sco_result r = s.Exec(kMarksTable);
    if (r != SCO_OK) return r;
    sco::sdk::StorageCursor c = s.Query("SELECT system, name, spots FROM bookmarks ORDER BY pos");
    if (!c) return c.Result();
    std::string system, name, spots;
    AcquireSRWLockExclusive(&g_travelLock);
    g_markCount = 0;
    while ((r = c.Next()) == SCO_OK && g_markCount < kMaxBookmarks) {
        Spot spot;
        if (c.Text(0, system) != SCO_OK || c.Text(1, name) != SCO_OK || c.Text(2, spots) != SCO_OK ||
            !SpotFromText(spots.c_str(), spot, false))
            continue;
        TravelBookmark& m = g_marks[g_markCount];
        m = {};
        strncpy_s(m.system, system.c_str(), _TRUNCATE);
        strncpy_s(m.name, name.c_str(), _TRUNCATE);
        g_markSpots[g_markCount++] = spot;
    }
    ReleaseSRWLockExclusive(&g_travelLock);
    return r == SCO_OK || r == SCO_NOT_FOUND ? SCO_OK : r;
}

static void SaveBookmarks() {
    if (!(g_quantumStore && StoreBookmarks(0))) WriteBookmarksFile();
}

// Storage, importing a bookmarks.txt it hasn't seen; bookmarks.txt as before without storage.
static void LoadBookmarks() {
    const char* from = "bookmarks.txt";
    BuiltinStore& st = g_quantumStore;
    if (!st) {
        ReadBookmarksFile();
    } else {
        char path[MAX_PATH];
        const int64_t fileTime = DataFilePath(path, sizeof(path), "bookmarks.txt") ? FileWriteTime(path) : 0;
        int64_t seen = 0;
        const sco_result rs = st.GetInt(kMarksTimeKey, seen);
        if (fileTime && (rs != SCO_OK || seen != fileTime)) {
            ReadBookmarksFile();
            if (StoreBookmarks(fileTime))
                Log("[storage] imported bookmarks.txt into quantum (%d saved spots)%s", g_markCount,
                    rs == SCO_OK ? " (bookmarks.txt changed since the last import)" : "");
        } else if (const sco_result r = ReadStoredBookmarks(); r == SCO_OK) {
            from = "storage";
        } else {
            st.Failed("reading the saved spots", r, "using bookmarks.txt");
            ReadBookmarksFile();
        }
    }
    Log("[travel] %d saved spots (%s)", g_markCount, from);
}

// =============================================================================================
// Location scan
//
// Everything the level places at load (planets, moons, stations, Lagrange points, comm arrays, jump
// points...) gets an entity id just above 200,000,000,000, well below the ids of things spawned while
// you play. Walking that range with the game's own id lookup and keeping the entities that host a
// zone finds every place in every loaded system in a few seconds, whatever it's called.
// =============================================================================================

constexpr uint64_t kStaticIdBase = 200000000000ull;
constexpr uint64_t kIdsPerTick   = 40000;
constexpr uint64_t kGiveUpGap    = 3000000;     // stop this far past the last entity found
constexpr uint64_t kMaxSpan      = 80000000;

static struct {
    bool active; uint64_t next, lastHit; int entities, zones, added, ships, deep; DWORD start;
    int newFrom;   // first g_places index added by this scan
} g_scan;

// SolarSystem_<id> zone name -> the name its object containers use ("Stanton", "Pyro").
struct SystemName { char zone[48]; char name[32]; };
static SystemName g_systemNames[16];
static int        g_systemNameCount = 0;

static void LearnSystemName(const char* zone, const char* name) {
    for (int i = 0; i < g_systemNameCount; ++i) if (_stricmp(g_systemNames[i].zone, zone) == 0) return;
    if (g_systemNameCount < 16) {
        strncpy_s(g_systemNames[g_systemNameCount].zone, zone, _TRUNCATE);
        strncpy_s(g_systemNames[g_systemNameCount].name, name, _TRUNCATE);
        ++g_systemNameCount;
        Log("[travel] %s is %s", zone, name);
    }
}

static const char* KnownSystemName(const char* zone) {
    for (int i = 0; i < g_systemNameCount; ++i) if (_stricmp(g_systemNames[i].zone, zone) == 0) return g_systemNames[i].name;
    return nullptr;
}

bool Travel_Scanning(float& progress) {
    if (!g_scan.active) return false;
    const double span = static_cast<double>(g_scan.lastHit - kStaticIdBase + kGiveUpGap);
    const double done = static_cast<double>(g_scan.next - kStaticIdBase);
    progress = static_cast<float>(done / span > 1.0 ? 1.0 : done / span);
    return true;
}

static void StartScan(DWORD now) {
    if (g_scan.active) { SetMenuStatus("Сканирование уже выполняется."); return; }
    g_scan = {};
    g_scan.active = true;
    g_scan.next = g_scan.lastHit = kStaticIdBase;
    g_scan.start = now;
    AcquireSRWLockShared(&g_travelLock);
    g_scan.newFrom = g_placeCount;
    ReleaseSRWLockShared(&g_travelLock);
    SetMenuStatus("Поиск мест…");
}

// What the scan learns about one entity. Kept apart from the bookkeeping so the fenced reads have
// nothing to unwind.
struct Probe { bool zoneHost, ship; int depth; char name[96]; char systemZone[48]; };

static bool ProbeEntity(uint64_t id, Probe& out) {
    out = {};
    __try {
        const uintptr_t e = VCall<uintptr_t>(*g_tp.entitySystem, 0x120, id);
        if (!e) return false;
        const char* name = VCall<const char*>(e, 0x78);
        strncpy_s(out.name, name ? name : "", _TRUNCATE);
        const uintptr_t zone = VCall<uintptr_t>(e, 0x6E0);
        if (!zone) return true;
        out.zoneHost = true;
        out.ship = EntityComponent(e, "IItemPortContainer") != 0;
        const uintptr_t sys = SystemZoneOf(zone);
        if (!sys || sys == zone) return true;
        const char* sysName = ZoneName(sys);
        strncpy_s(out.systemZone, sysName ? sysName : "", _TRUNCATE);
        for (uintptr_t z = zone; z && z != sys && out.depth < 8; z = ZoneParent(z)) ++out.depth;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out.zoneHost = false;
    }
    return true;
}

static void FinishScan(DWORD now) {
    g_scan.active = false;
    AcquireSRWLockExclusive(&g_travelLock);
    ++g_placesVersion;
    for (int i = 0; i < g_placeCount; ++i)             // name the systems we learned
        if (_strnicmp(g_places[i].system, "SolarSystem", 11) == 0)
            if (const char* known = KnownSystemName(g_places[i].system)) strncpy_s(g_places[i].system, known, _TRUNCATE);
    NameByBody();
    const int from = g_scan.newFrom, to = g_placeCount;
    static TravelPlace added[kMaxPlaces];
    int n = 0;
    for (int i = from; i < to; ++i) added[n++] = g_places[i];
    ReleaseSRWLockExclusive(&g_travelLock);
    int unnamed = 0;
    for (int i = 0; i < n; ++i) {
        if (_strnicmp(added[i].system, "SolarSystem", 11) == 0) { ++unnamed; continue; }   // ids change per session
        AppendFoundPlace(added[i]);
    }
    if (unnamed) Log("[travel] %d places are in a system whose name wasn't found (shown, but not saved)", unnamed);
    Log("[travel] scan: ids %llu..%llu, %d entities, %d zone hosts (%d ships skipped, %d too deep), %d new places, %.1f s",
        static_cast<unsigned long long>(kStaticIdBase), static_cast<unsigned long long>(g_scan.lastHit), g_scan.entities,
        g_scan.zones, g_scan.ships, g_scan.deep, n, (now - g_scan.start) / 1000.0);
    SetMenuStatus("Поиск завершён. Новых мест: %d%s.", n, "");
}

static void StepScan(DWORD now) {
    if (!g_scan.active) return;
    for (uint64_t k = 0; k < kIdsPerTick; ++k) {
        const uint64_t id = g_scan.next++;
        Probe pr;
        if (!ProbeEntity(id, pr)) continue;
        g_scan.lastHit = id;
        ++g_scan.entities;
        if (!pr.zoneHost || !pr.name[0] || !pr.systemZone[0]) continue;
        ++g_scan.zones;
        if (pr.ship || _strnicmp(pr.name, "StreamingSOC", 12) == 0) { ++g_scan.ships; continue; }
        if (pr.depth > 2) { ++g_scan.deep; continue; }
        TravelPlace p = {};
        if (!DescribeEntity(pr.name, p)) {
            strncpy_s(p.name, pr.name, _TRUNCATE);
            Underscores(p.name);
            p.kind = IsMinorZone(pr.name) ? Place_Minor : Place_Other;
        }
        // Only names that say which system they're in (OOC_<system>_..., jumppoint_<system>_...,
        // pyro2) teach us the system's name; P3_L2 and the like just take it from their zone.
        if (p.system[0] && (_strnicmp(pr.name, "OOC_", 4) == 0 || _strnicmp(pr.name, "jumppoint_", 10) == 0))
            LearnSystemName(pr.systemZone, p.system);
        if (!p.system[0]) {
            const char* known = KnownSystemName(pr.systemZone);
            strncpy_s(p.system, known ? known : pr.systemZone, _TRUNCATE);
        }
        AcquireSRWLockExclusive(&g_travelLock);
        AddPlace(p);
        ReleaseSRWLockExclusive(&g_travelLock);
    }
    if (g_scan.next - g_scan.lastHit > kGiveUpGap || g_scan.next - kStaticIdBase > kMaxSpan) FinishScan(now);
}

// =============================================================================================
// Going places
// =============================================================================================

static struct { bool active; DWORD since; TravelPlace place; float altitude; Spot target; } g_arrival;

// Snapshot named coordinates, never retain game pointers across ticks. Ancestors let
// us confirm arrival even before the destination's own zone finishes streaming.
static bool ArrivalSpot(uintptr_t zone, const double local[3], Spot& out) {
    out = {};
    __try {
        double world[3];
        LocalToWorld(zone, local, world);
        int steps = 0;
        for (uintptr_t z = zone; z && out.n < kMaxZoneDepth && steps++ < 64; z = ZoneParent(z)) {
            const char* name = ZoneName(z);
            if (!name || !*name) continue;
            auto& frame = out.z[out.n];
            if (z == zone) memcpy(frame.local, local, sizeof(frame.local));
            else if (!WorldToLocal(z, world, frame.local)) continue;
            strncpy_s(frame.name, name, _TRUNCATE);
            ++out.n;
        }
        return out.n > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { out = {}; return false; }
}

const char* Travel_ResolveSpawn(const TravelPlace& p, float altitude, uint64_t& zoneId, double local[3]) {
    if (const auto* station = FindLocalStationStart(p.entity)) {
        uintptr_t parent = 0; uint64_t id = 0;
        if (!FindEntityByNameEx(station->parent, parent, id)) return "планета станции ещё не загружена; требуется PU_All";
        __try {
            const uintptr_t zone = VCall<uintptr_t>(parent, 0x6E0);
            if (!zone) return "зона планеты станции недоступна";
            zoneId = ZoneId(zone);
            if (!zoneId || ZoneFromId(zoneId) != zone) return "ID зоны станции не соответствует планете";
            const double r = sqrt(station->position[0]*station->position[0] + station->position[1]*station->position[1] + station->position[2]*station->position[2]);
            if (!std::isfinite(r) || r < 1) return "неверные координаты станции";
            const double clearance = (std::max)(3000.0, double(altitude));
            for (int i=0; i<3; ++i) local[i] = station->position[i] * (1.0 + clearance/r);
            return nullptr;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return "ошибка чтения зоны станции"; }
    }
    uintptr_t entity = 0; uint64_t id = 0;
    if (!FindEntityByNameEx(p.entity, entity, id)) return "место ещё не загружено; требуется PU_All";
    __try {
        uintptr_t zone = VCall<uintptr_t>(entity, 0x6E0);
        if (zone) {
            local[0] = local[1] = 0;
            local[2] = p.radius > 0 ? p.radius + altitude : p.kind == Place_Planet ? 9.0e6 : p.kind == Place_Moon ? 1.5e6 : (std::max)(2000.0, double(altitude));
        } else {
            zone = VCall<uintptr_t>(entity, 0x6B8);
            Vec3Out(entity, 0x2B8, local);
            local[2] += (std::max)(2000.0, double(altitude)); // outside station geometry, not an interior bed
        }
        if (!zone) return "у места нет доступной зоны";
        zoneId = ZoneId(zone);
        if (!zoneId || ZoneFromId(zoneId) != zone) return "зона места не соответствует ID";
        return nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return "ошибка чтения зоны старта"; }
}

static const char* GoToPlace(const TravelPlace& p, float altitude, Spot& target) {
    if (PlayerSeatLinkState() == 1)
        return "— сначала выйдите из кресла. Эта кнопка перемещает персонажа; корабль остаётся на месте";
    if (FindLocalStationStart(p.entity)) {
        uint64_t zoneId = 0; double local[3] = {};
        if (const char* err = Travel_ResolveSpawn(p, altitude, zoneId, local)) return err;
        const uintptr_t zone = ZoneFromId(zoneId);
        if (!zone || !ArrivalSpot(zone, local, target)) return "не удалось прочитать точку возле станции";
        Log("[travel-station] %s parent-zone=%llu; offset outside station, runtime verification pending", p.entity, zoneId);
        return TeleportIntoZone(zone, local);
    }
    uintptr_t entity;
    uint64_t id;
    if (!FindEntityByNameEx(p.entity, entity, id))
        return "isn't loaded - it may be in another system, or the name in the list is wrong (try the scan)";
    uintptr_t zone = 0;
    __try { zone = VCall<uintptr_t>(entity, 0x6E0); } __except (EXCEPTION_EXECUTE_HANDLER) { zone = 0; }
    if (zone) {
        // Planets and moons are zones of their own: arrive straight above the north pole.
        double height = altitude;
        if (p.radius > 0) height += p.radius;
        else if (p.kind == Place_Planet) height = 9.0e6;      // radius unknown: well clear of even a gas giant
        else if (p.kind == Place_Moon) height = 1.5e6;
        else height = altitude < 2000 ? 2000 : altitude;
        const double local[3] = { 0, 0, height };
        if (!ArrivalSpot(zone, local, target)) return "— не удалось прочитать координаты назначения";
        return TeleportIntoZone(zone, local);
    }
    if (!id) return "found, but it has no id to teleport to";
    __try {
        zone = VCall<uintptr_t>(entity, 0x6B8);
        double local[3]; Vec3Out(entity, 0x2B8, local);
        const double up = altitude < 50 ? altitude : 50;
        const double r = sqrt(local[0]*local[0] + local[1]*local[1] + local[2]*local[2]);
        if (r > 100000) for (int i=0; i<3; ++i) local[i] += local[i]/r*up;
        else local[2] += up;
        if (!zone || !ArrivalSpot(zone, local, target)) return "— не удалось прочитать координаты назначения";
    } __except (EXCEPTION_EXECUTE_HANDLER) { return "— ошибка чтения места назначения"; }
    return TeleportToEntity(id, altitude < 50 ? altitude : 50);
}

static void CheckArrival(DWORD now) {
    if (!g_arrival.active) return;
    Spot here;
    const bool sampled = CaptureCurrentSpot(here) == nullptr;
    const double distance = sampled ? exploration::Distance(here, g_arrival.target) : -1;
    if (distance >= 0 && std::isfinite(distance) && distance <= 150) {
        g_arrival.active = false;
        Log("[travel] arrival confirmed: %s, error=%.1f m", g_arrival.place.entity, distance);
        if (g_arrival.place.radius > 0)
            SetMenuStatus("Прибытие к %s подтверждено: %.0f м над расчётной поверхностью.", g_arrival.place.name, g_arrival.altitude);
        else
            SetMenuStatus("Прибытие к %s подтверждено. Радиус неизвестен; точка может быть далеко от поверхности.", g_arrival.place.name);
    } else if (now - g_arrival.since >= 15000) {
        g_arrival.active = false;
        Log("[travel] arrival NOT confirmed: %s, distance=%.1f m, position_read=%d", g_arrival.place.entity, distance, sampled);
        SetMenuStatus("Прибытие к %s не подтверждено. Выйдите из кресла и повторите; подробности в mod.log.", g_arrival.place.name);
    }
}

static void HandleRequest(DWORD now) {
    AcquireSRWLockExclusive(&g_travelLock);
    const auto req = g_req;
    g_req.kind = Req_None;
    ReleaseSRWLockExclusive(&g_travelLock);

    switch (req.kind) {
    case Req_StartPlace: {
        StartupDestination start; start.mode = 1; start.place = req.place; start.altitude = req.altitude;
        SaveStartup(start); break;
    }
    case Req_StartHere: {
        if (PlayerSeatLinkState() != 0 || PlayerShipId()) {
            SetMenuStatus("Для точной стартовой точки встаньте и выйдите из корабля. Кровать корабля пока не сохраняется."); break;
        }
        StartupDestination start; start.mode = 2;
        if (const char* err = CaptureCurrentSpot(start.spot)) SetMenuStatus("Не удалось прочитать точку: %s", err);
        else SaveStartup(start);
        break;
    }
    case Req_StartOff: { SaveStartup(StartupDestination{}); break; }
    case Req_Place: {
        g_startupPending = false; g_startupRelocationSent = false;
        Log("[travel-start] automatic startup superseded by manual destination");
        g_arrival = {};
        if (const char* err = GoToPlace(req.place, req.altitude, g_arrival.target)) SetMenuStatus("%s %s.", req.place.name, err);
        else {
            g_arrival.active = true; g_arrival.since = now;
            g_arrival.place = req.place; g_arrival.altitude = req.altitude;
            Log("[travel] request sent: %s; waiting for actual player position", req.place.entity);
            SetMenuStatus("Запрос к %s отправлен. Проверяю прибытие…", req.place.name);
        }
        break;
    }
    case Req_Bookmark: {
        g_startupPending = false; g_startupRelocationSent = false;
        g_arrival.active = false;
        if (req.index < 0 || req.index >= g_markCount) break;
        if (PlayerSeatLinkState() == 1) { SetMenuStatus("Сначала выйдите из кресла: сохранённая точка перемещает персонажа, а не корабль."); break; }
        const TravelBookmark mark = g_marks[req.index];
        if (const char* err = GoToSpot(g_markSpots[req.index], now, "bookmark")) SetMenuStatus("Не удалось переместиться к «%s»: %s.", mark.name, err);
        else SetMenuStatus("Перемещение к «%s» выполнено.", mark.name);
        break;
    }
    case Req_Save: {
        Spot s;
        if (const char* err = CaptureCurrentSpot(s)) { SetMenuStatus("Не удалось сохранить точку: %s.", err); break; }
        if (g_markCount >= kMaxBookmarks) { SetMenuStatus("Сохранено %d точек. Сначала удалите одну.", kMaxBookmarks); break; }
        TravelBookmark mark = {};
        SpotSystemName(s, mark.system, sizeof(mark.system));
        if (!mark.system[0]) strcpy_s(mark.system, "Unknown system");
        if (req.name[0]) strncpy_s(mark.name, req.name, _TRUNCATE);
        else strncpy_s(mark.name, s.z[0].name, _TRUNCATE);
        AcquireSRWLockExclusive(&g_travelLock);
        g_marks[g_markCount] = mark;
        g_markSpots[g_markCount] = s;
        ++g_markCount;
        ReleaseSRWLockExclusive(&g_travelLock);
        SaveBookmarks();
        SetMenuStatus("Сохранено: «%s» (%s).", mark.name, mark.system);
        break;
    }
    case Req_Delete: {
        if (req.index < 0 || req.index >= g_markCount) break;
        char name[64];
        strcpy_s(name, g_marks[req.index].name);
        AcquireSRWLockExclusive(&g_travelLock);
        for (int i = req.index; i + 1 < g_markCount; ++i) { g_marks[i] = g_marks[i + 1]; g_markSpots[i] = g_markSpots[i + 1]; }
        --g_markCount;
        ReleaseSRWLockExclusive(&g_travelLock);
        SaveBookmarks();
        SetMenuStatus("Удалено: «%s».", name);
        break;
    }
    case Req_Scan:
        StartScan(now);
        break;
    default:
        break;
    }
}

void ProcessTravel(DWORD now) {
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        LoadPlaces("locations.txt", true);
        LoadPlaces("locations_found.txt", false);
        LoadBookmarks();
        if (g_launcherPlace[0]) {
            bool found = false;
            AcquireSRWLockExclusive(&g_travelLock);
            for (int i = 0; i < g_placeCount; ++i) if (_stricmp(g_places[i].entity, g_launcherPlace) == 0) {
                g_startup.place = g_places[i]; g_startup.altitude = 3000; found = true; break;
            }
            ReleaseSRWLockExclusive(&g_travelLock);
            if (!found || !ValidStartup(g_startup)) {
                g_startupPending = false;
                Log("[travel-start] launcher destination absent/invalid: %s", g_launcherPlace);
                SetMenuStatus("Стартовая точка отсутствует в каталоге: %s", g_launcherPlace);
            }
        }
    }
    static DWORD lastSystem = 0;
    if (now - lastSystem > 2000) {
        lastSystem = now;
        char sys[48];
        CurrentSystemName(sys, sizeof(sys));
        if (!sys[0]) {                                   // no OOC_ names around: use the system zone
            CurrentSystemZoneName(sys, sizeof(sys));
            if (const char* known = KnownSystemName(sys)) strncpy_s(sys, known, _TRUNCATE);
        }
        AcquireSRWLockExclusive(&g_travelLock);
        strncpy_s(g_currentSystem, sys, _TRUNCATE);
        ReleaseSRWLockExclusive(&g_travelLock);
    }
    HandleRequest(now);
    if (g_startupPending) {
        static DWORD readyAt = 0, lastTry = 0;
        static unsigned attempts = 0;
        uintptr_t actor = 0, entity = 0;
        bool ready = false;
        __try { ready = GetLocalPlayer(actor, entity); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (!ready) readyAt = 0;
        if (ready && !readyAt) readyAt = now;
        // Dispatch success is not arrival: loading/bed transitions can overwrite the
        // first teleport. Observe coordinates every tick and create at most one ship,
        // only after the player is actually at the requested destination.
        if (ready && g_startupRelocationSent) {
            Spot here;
            const bool sampled = CaptureCurrentSpot(here) == nullptr;
            const double distance = sampled ? exploration::Distance(here, g_arrival.target) : -1;
            if (distance >= 0 && std::isfinite(distance) && distance <= 150) {
                CheckArrival(now);
                g_startupPending = false;
                Log("[travel-start] actual arrival confirmed before ship creation: distance=%.1f m", distance);
                if (g_launcherPlace[0] && g_launcherShip[0]) {
                    if (const char* err = StartShipAtPlace(g_startup.place, g_startup.altitude, g_launcherShip, now)) {
                        Log("[travel-start] ship creation failed after confirmed arrival: %s", err);
                        SetMenuStatus("Прибытие подтверждено, но корабль не создан: %s", err);
                    } else SetMenuStatus("Прибытие к %s подтверждено. Корабль создан; ожидаю посадку в кресло.", g_startup.place.name);
                }
                return;
            }
        }
        if (ready && now - readyAt >= 120000) {
            g_startupPending = false;
            g_arrival.active = false;
            SetMenuStatus("Стартовая точка недоступна. Проверьте PU_All и загрузку места; настройка сохранена.");
            Log("[travel-start] gave up after 120 seconds; destination retained");
        } else if (ready && now - readyAt >= 10000 && now - lastTry >= 5000) {
            lastTry = now;
            Spot observed;
            const bool sampled = CaptureCurrentSpot(observed) == nullptr;
            const double distance = sampled && g_startupRelocationSent ? exploration::Distance(observed, g_arrival.target) : -1;
            Log("[travel-start] attempt=%u elapsed=%lu ms seat=%d position_read=%d zone=%s previous_distance=%.1f m",
                ++attempts, now - readyAt, PlayerSeatLinkState(), sampled,
                sampled && observed.n > 0 ? observed.z[0].name : "unavailable", distance);
            if (PlayerSeatLinkState() == 1) {
                const bool unlinked = UnseatEntityById(LocalPlayerEntityId());
                Log("[travel-start] native delink requested=%d; retry after streaming", unlinked);
            } else if (PlayerSeatLinkState() == 0) {
                const char* err = nullptr;
                g_arrival = {};
                if (g_launcherPlace[0] && g_launcherShip[0]) {
                    // A remote ship alone does not stream its seats for the player in another
                    // system. Move the player first; the seat job then waits for local streaming.
                    err = GoToPlace(g_startup.place, g_startup.altitude, g_arrival.target);
                    if (!err) {
                        g_arrival.active = true; g_arrival.since = now;
                        g_arrival.place = g_startup.place; g_arrival.altitude = g_startup.altitude;
                        Log("[travel-start] player relocation requested before ship creation: %s; actual arrival pending", g_startup.place.entity);
                    }
                    if (!err) {
                        g_startupRelocationSent = true;
                        SetMenuStatus("Запрошен переход к %s. Проверяю прибытие; корабль будет создан после переноса.", g_startup.place.name);
                        return;
                    }
                }
                else if (g_startup.mode == 1) err = GoToPlace(g_startup.place, g_startup.altitude, g_arrival.target);
                else { g_arrival.target = g_startup.spot; err = GoToSpot(g_startup.spot, now, "startup"); }
                if (!err) {
                    g_startupRelocationSent = true; g_arrival.active = true; g_arrival.since = now;
                    g_arrival.place = g_startup.place; g_arrival.altitude = g_startup.altitude;
                    if (g_startup.mode == 2) { strcpy_s(g_arrival.place.name, "сохранённая стартовая точка"); strcpy_s(g_arrival.place.entity, "startup.spot"); }
                    Log("[travel-start] request sent mode=%d; actual arrival pending", g_startup.mode);
                    SetMenuStatus("Перемещение в стартовую точку отправлено. Проверяю прибытие…");
                } else { Log("[travel-start] waiting: %s", err); SetMenuStatus("Ожидаю стартовую точку: %s", err); }
            }
        }
    }
    CheckArrival(now);
    StepScan(now);
}

