#pragma once
#include "common.h"

void ResolveNpcApi(const Section& text);
void RemoveEntityById(uint64_t id);
void ProcessNpcs();
bool CanRemoveEntities();
void TrackSpawnedNpc(uint64_t id);
uint64_t LastSpawnedNpcId(); // only NPCs created by this mod; game thread
int32_t RemoveEntitySlot();   // entity system vtable offset of RemoveEntity, 0 if not found
