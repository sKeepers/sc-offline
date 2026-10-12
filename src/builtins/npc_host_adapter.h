#pragma once
#include <cstdint>
// Small host adapter for the local safety rule and companion selection.
// Ownership stays with game.actors, not the legacy removal list.
const char* LocalNpcSpawnError(const char* entityClass);
void LocalNpcSelection(uint64_t id);
