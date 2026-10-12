#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace companion {
// Pointer identity is a conservative fence, not an engine generation token.
struct TrialObservation {
    uint64_t player = 0;
    uintptr_t entity = 0, component = 0, previous = 0, firstAssignment = 0;
    bool lateActivation = false, replacement = false, disappearance = false;
    bool matches(uint64_t currentPlayer, uintptr_t currentEntity, uintptr_t currentComponent) const {
        return player && entity && component && player == currentPlayer &&
            entity == currentEntity && component == currentComponent;
    }
    bool observe(uintptr_t current, bool cancelled) {
        if (current == previous) return false;
        if (current && cancelled) lateActivation = true;
        if (current && !firstAssignment) firstAssignment = current;
        if (current && previous) replacement = true;
        if (!current && previous) disappearance = true;
        previous = current;
        return true;
    }
    // A lost assignment must never regain permission by reusing its address.
    // This is only an experimental identity fence, not proof of graph ownership.
    bool mayCancel(uintptr_t current) const {
        return firstAssignment && current == firstAssignment && previous == current &&
            !replacement && !disappearance;
    }
    static bool finished(uint64_t now, uint64_t cancelledAt) {
        return now >= cancelledAt && now - cancelledAt >= 120000;
    }
};
enum ResultBits : unsigned { Pending = 0, SuccessSeen = 1, FailureSeen = 2, LoadFailed = 4 };
class Diagnostics {
    std::array<std::atomic<unsigned>, 16> results_{};
    std::atomic<unsigned> samples_{0};
public:
    void reserve(unsigned slot) { if (slot < 16) results_[slot].store(Pending); }
    unsigned result(unsigned slot) const { return slot < 16 ? results_[slot].load() : Pending; }
    void loadFailed(unsigned slot) { if (slot < 16) results_[slot].fetch_or(LoadFailed); }
    // Observation always runs; the returned flag controls the log line only.
    bool observe(const char* message, bool active, unsigned published) {
        if (active && message) {
            for (unsigned slot = 0; slot < published && slot < 16; ++slot) {
                char expected[96]{};
                std::snprintf(expected, sizeof(expected), "SCO_COMPANION_SUCCESS_SCO_CompanionOrder_%u", slot + 1);
                if (!std::strcmp(message, expected)) { results_[slot].fetch_or(SuccessSeen); break; }
                std::snprintf(expected, sizeof(expected), "SCO_COMPANION_FAILURE_SCO_CompanionOrder_%u", slot + 1);
                if (!std::strcmp(message, expected)) { results_[slot].fetch_or(FailureSeen); break; }
            }
        }
        return samples_.fetch_add(1) < 64;
    }
};
struct Request {
    int action = 0;
    uint64_t npc = 0, player = 0;
    bool matches(uint64_t selected, uint64_t localPlayer) const {
        return npc && player && npc == selected && player == localPlayer;
    }
};
// Latest request wins, but action and both identities are one coherent unit.
class RequestQueue {
    std::mutex mutex_;
    Request pending_{};
public:
    Request publish(Request request) {
        std::lock_guard<std::mutex> lock(mutex_);
        const Request old = pending_; pending_ = request; return old;
    }
    Request consume() {
        std::lock_guard<std::mutex> lock(mutex_);
        const Request out = pending_; pending_ = {}; return out;
    }
};
} // namespace companion
