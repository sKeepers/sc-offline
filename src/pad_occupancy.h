#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// A cleared ATC token does not prove that the physical ship disappeared.
// Keep observations until the entity is gone or observed at another pad.
class PadOccupancy {
    struct Entry { uint64_t pad, vehicle; };
    std::vector<Entry> entries_;
public:
    void Observe(uint64_t pad, uint64_t vehicle) {
        if (!pad || !vehicle) return;
        for (auto& entry : entries_)
            if (entry.vehicle == vehicle) { entry.pad = pad; return; }
        entries_.push_back({pad, vehicle});
    }
    template<class Alive> uint64_t BlockedBy(uint64_t pad, Alive alive) {
        for (size_t i = 0; i < entries_.size();) {
            if (!alive(entries_[i].vehicle)) {
                entries_.erase(entries_.begin() + i);
                continue;
            }
            if (entries_[i].pad == pad) return entries_[i].vehicle;
            ++i;
        }
        return 0;
    }
};
