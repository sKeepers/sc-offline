#pragma once
#include <cstddef>
#include <cstring>

// A registered class is not necessarily a complete spawnable actor.
// LIVE 4.10.196.36804 fatally asserts on Transport's missing actor components.
// Keep this check before the engine creation batch: SEH cannot recover that
// later, asynchronous fatal error. This is a known-crash denylist, not a
// guarantee that every other class is compatible with the current client.
inline const char* SpawnClassError(const char* name) {
    if (!name || !*name) return "не задан класс объекта";
    while (*name == ' ' || *name == '\t') ++name;
    std::size_t length = std::strlen(name);
    while (length && (name[length - 1] == ' ' || name[length - 1] == '\t')) --length;
    if (!length) return "не задан класс объекта";
    constexpr char blocked[] = "transport";
    if (length != sizeof(blocked) - 1) return nullptr;
    for (std::size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        const unsigned char lower = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
        if (lower != blocked[i]) return nullptr;
    }
    return "Transport отключён: отсутствуют компоненты персонажа; этот класс вызывает аварийное завершение игры";
}
