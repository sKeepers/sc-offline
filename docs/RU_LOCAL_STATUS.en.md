# sc-offline RU / Local — Local 6 status

> October 13, 2026 update: Local6 remains the published package; Local9 is the installed test build. [New changes, validation and limitations](DEVELOPMENT_LOCAL9.en.md).

[Русская версия](RU_LOCAL_STATUS.md) · [Release](https://github.com/sKeepers/sc-offline/releases/tag/ru-local-0.7.7-6) · [Windows ZIP](https://github.com/sKeepers/sc-offline/releases/download/ru-local-0.7.7-6/sc-offline-0.7.7-ru-local-6-windows-x64.zip) · [Source](https://github.com/sKeepers/sc-offline/tree/codex/release-0.7.7-ru-local-6)

Updated: **October 13, 2026**. Published: **0.7.7 RU / Local 6, Windows x64 development pre-release**.

Base: v0.7.7 (`1ac5308a65ac0ba7955e4108bd1dc800449cfa69`) plus upstream changes through `85774b784d4cc8eb5dda75148387318486971ef6`. sco-core `cd9a686bda4184f1befbdf0b74b503a1565540dd`, SDK 2.0 / ABI 1.1. Local source branch `codex/release-0.7.7-ru-local-6`.

## Included features

| Area | Implemented | Validation and limits |
|---|---|---|
| Launcher localization | 306 UTF-8 key=value strings, en/ru catalogs, compiled English fallback, language selection | Key/placeholder/UTF-8/UTF-16/stable-ID/font-cmap tests passed; visual verification pending |
| Game names | Discovery of Localization folders, global.ini and languages.ini; auto reads g_language in user.cfg | Russian in the Korean slot recognized; ru/de source selection checked autonomously. Labels never replace class identifiers |
| Lists | Readable names and partial name/class search | An incomplete query cannot be saved as a ship class |
| Flight | Stanton/Pyro profiles, favorites/routes, continuation and autosave | Limited state only, without cargo/damage/orientation restoration. Confirmed arrival precedes ship creation; destinations are not all verified |
| Stations | Experimental Grim HEX, Seraphim, Everus Harbor, Baijini Point and Port Tressler starts | Exterior ship positions about 3 km away, not bed/medical/hangar spawns |
| Menu window | Resizing and persisted dimensions constrained to the display | User confirmed resizing; restoration in further sessions still needs verification |
| Dashboard | Catalogs, fleet/storage ledger, page refresh, service controls and fleet bridge | 24 packaged service/catalog/control tests passed; bookkeeping is separate from physical items/hangars |
| Plugins | Creative, Discord, Exploration, Loadout Presets, NPC, Inspector; external SDK modules | Autonomous SDK checks passed; Creative off by default; full in-game acceptance pending |
| NPC | Owned lifecycle and diagnostic readouts with a read-only ID source for UI | Leader/follow/guard quarantined after a crash; opt-in probes are not proof of safety or friendly behavior |
| Networking | UDP diagnostics, poses and limited visual co-presence, LAN/VPN configuration | Two-PC VPN gameplay still needs verification; no shared physics, combat, damage, missions or inventory |
| Name | Custom name in mod/network UI | Native mobiGlas DEFAULT_1 remains unresolved |
| Updating | Upstream0.7.7/SDK2 alignment and separate local adapters; upstream releases are notification-only | Local features need explicit migration/testing; original auto-updates do not migrate them |

## Setup and upgrades

Download the Windows ZIP, extract to a separate folder and run sc-offline.exe. Set game= in sc-offline.ini if needed. Python3.11+ with the standard library is required for the optional http://127.0.0.1:18870/ dashboard. Use server/Connect-Mod.cmd for fleet pairing.

Close the game/launcher and back up the existing folder before upgrading. Preserve sc-offline.ini, data saves/settings and server/server-state. Copy these to the new folder, then overlay release files; retain user files absent from the release. Read RELEASE.en.md/docs/launcher.md inside the ZIP for setup, temporary system changes and normal offline cleanup.

## Publication and evidence

Release assets: Windows ZIP, complete source including sco-core/licenses, and SHA256SUMS.txt. GitHub asset digests match local hashes. User profiles/saves, passwords/tokens, databases/logs and game resources/translations are excluded. OfflineDB is the public upstream template.

Local checks passed: Release EXE/DLL build, native localization tests, SDK gate/tests, exact ImGui pin, report/profile checks, unpacked launcher self-test and 24 Python service tests. These do not establish gameplay correctness or whole GitHub CI success.

The M menu/local plugins/dashboard remain Russian; a shared additive sco-core localization C service is a later phase. A dedicated world simulator, physical shared inventory/combat, medical/ship-bed spawning and reliable companion following are not implemented.

Main contains documentation on the earlier code base. Local6 implementation is on codex/release-0.7.7-ru-local-6, tagged ru-local-0.7.7-6. Development assisted by Codex; some research models assisted by Claude. ChrisWareOffline/scubamount attribution and licenses are retained.



## Local6 / Насадки и исходники

106новых классов/6списков с поиском; подробности: [RU](../LOCAL6.ru.md) / [EN](../LOCAL6.en.md).1526собственных проверок/0 и13host-checks/0. В игре ещё не проверено. Исправлен пропуск2tests/bridge_*.cpp из предыдущего complete-source ZIP; новая распакованная CMake-конфигурация проверяется.

106new classes/6searchable lists,1526independent checks and13host checks. In-game acceptance pending. The source archive now includes missing bridge test files.
