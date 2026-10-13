# Local6 — gear attachments and source archive correction

106new classes:53optics,35barrels,10underbarrels,5multitool modules,2SRTcanisters and1battery. Player UI has separate readable/searchable lists for primary and sidearm, exact class IDs and module search aliases. Default SRT and legacy XML are retained without new choices. Weapon changes reset only that hand. Presets retain their XML format.

Based on Alpha-Rome0 PR127head7ea9731 with reviewed Claude integration. Codex rejects extraIDs>=64bytes rather than truncating them, updates the host-test stub and adds independent tests. Incoming Claude test programs/scripts were not executed. Attribution is in GEAR-ATTACHMENTS-ORIGIN.txt.

Verified locally:1526independent parser/XML/catalog checks,13real-core admission/cleanup checks,native localization306keys,SDK47files300includes,21metadata titles,full isolated Windows MSVC Release EXE/DLL build. Installed self-test passes; user data preserved with backup.

The previous Local5 complete-source archive omitted tests/bridge_client.cpp and tests/bridge_protocol_test.cpp, preventing CMake configuration. These files are now included. Complete-source extraction/configuration is checked before publication.

Game acceptance is pending:class existence does not prove attachment compatibility, native tool ports or that all XML items are equipped. Check one suitable optic, untouched default SRT, an explicit tool module, and old/new presets. NPC follow/guard remain quarantined; this does not implement physical inventory or shared gameplay.
