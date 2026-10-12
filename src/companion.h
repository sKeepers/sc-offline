#pragma once
#include "common.h"
void ResolveCompanionBridge(const Section& text, const Section& rdata);
void ProcessCompanionBridge(); // game thread only; queued experimental orders
void Menu_RequestCompanionProbe();
void Menu_ConfirmCompanionPeaceful(bool peaceful); // observation of current last NPC, game thread only
const char* Menu_CompanionBridgeStatus(); // menu draw also runs on game thread
const char* Menu_CompanionResultStatus(); // actual outputs of the latest numbered order
bool CompanionXmlPayload(const char* path, const char*& data, size_t& size);
void Menu_RequestCompanionAction(int action); // 1 faction, 2 follow, 3 idle, 4 guard, 5 return, 7 read-only relationship

// Observed asynchronous native crash: mutation orders remain quarantined until fixed.
bool CompanionOrdersAvailable();
bool CompanionFollowTrialAvailable(); // one short no-leader trial per session
