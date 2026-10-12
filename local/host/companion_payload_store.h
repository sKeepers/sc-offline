#pragma once
#include <cstddef>
#include <atomic>
#include <cstring>

namespace companion {
struct Payload {
    char xml[3072]{};
    char name[64]{};
    char path[128]{};
    char npc[32]{};
    char player[32]{};
};
// Owned copies are retained for the entire session. No erase/reuse while native modules may refer to them.
class PayloadStore {
    static bool samePath(const char* a,const char* b) {
        for(;;++a,++b) {
            const unsigned char ca=static_cast<unsigned char>(*a),cb=static_cast<unsigned char>(*b);
            const unsigned char x=ca>='A'&&ca<='Z'?ca+('a'-'A'):ca;
            const unsigned char y=cb>='A'&&cb<='Z'?cb+('a'-'A'):cb;
            if(x!=y)return false;
            if(!x)return true;
        }
    }
    Payload records_[16]{};
    std::atomic<std::size_t> count_{0};
public:
    std::size_t size() const { return count_.load(std::memory_order_acquire); }
    const Payload* add(const Payload& p) {
        const auto count = size(); // single game-thread publisher; readers may be native loader threads
        if (count == 16 || !p.path[0] || !p.xml[0] || !p.name[0] || !p.npc[0] || !p.player[0]) return nullptr;
        if (!std::memchr(p.xml,0,sizeof(p.xml)) || !std::memchr(p.name,0,sizeof(p.name)) ||
            !std::memchr(p.path,0,sizeof(p.path)) || !std::memchr(p.npc,0,sizeof(p.npc)) ||
            !std::memchr(p.player,0,sizeof(p.player))) return nullptr;
        if (find(p.path)) return nullptr;
        records_[count] = p;
        count_.store(count + 1, std::memory_order_release);
        return &records_[count];
    }
    const Payload* find(const char* path) const {
        if (!path) return nullptr;
        for(std::size_t i=0,n=size();i<n;++i) if(samePath(records_[i].path,path))return &records_[i];
        return nullptr;
    }
};
}
