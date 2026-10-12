// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco_api.h"

// RU Local additive, read-only service. Session IDs are never persisted. This
// keeps host companion diagnostics connected without private host code in DLL.
inline constexpr const char* NPC_LOCAL_OWNED_NAME = "npc.local.owned";
inline constexpr uint32_t NPC_LOCAL_OWNED_VERSION = 0x00010000u;
struct npc_local_owned_v1 {
    uint32_t size;
    uint64_t (*latest_owned)(); // game thread only; 0 when no owned NPC exists
};

namespace sco::features::npc {
const sco_plugin_info* Query();
sco_result Load(const sco_api* api, sco_plugin* self);
void Unload();
}  // namespace sco::features::npc
