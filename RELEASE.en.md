# 0.7.7 RU / Local 6 — development pre-release

Windows x64; bring your own installed game. The optional localhost dashboard needs Python 3.11+ with the standard library only.

Extract to a separate folder and run sc-offline.exe. Set game= in sc-offline.ini if detection fails. Open the flight profile to choose the starting place, ship and language. Restart the launcher after changing language. M opens the mod menu; I toggles engines. Start the dashboard using the launcher buttons or Play, then open http://127.0.0.1:18870/ . Use server/Connect-Mod.cmd to pair the fleet ledger.

For upgrades, close the game and launcher, back up the old folder, copy your sc-offline.ini, data saves/settings/profile and server/server-state to the new folder, then overlay this release. Keep user files absent from the release. No game files or user data are supplied.

306 launcher texts use flat UTF-8 catalogs with compiled English fallback. Game translations are read locally for item/ship names. The M menu, local plugins and dashboard remain Russian; a shared plugin localization ABI is a future step.

Station starts are experimental exterior ship positions, not medical/bed/hangar spawns. Relocation is not verified for every destination. Native mobiGlas DEFAULT_1 is unresolved; custom naming currently covers the mod UI. Companion leader/follow/guard are quarantined after a crash. Opt-in diagnostic trials are not gameplay validation.

Multiplayer provides pose exchange and limited visual co-presence, not shared physics, damage, missions or inventory. Configure multiplayer_allow for your VPN on both PCs. If Windows reserves UDP64091, choose and verify a free port (for example45091).

Build and autonomous tests passed; the new localization has not been visually verified and this exact bundle has not received a complete in-game acceptance run. Original updates do not automatically replace local features. Read docs/launcher.md for configurable temporary firewall/EAC-related system changes and normal cleanup before online play.

The localhost ledger is not a dedicated game simulation server. Inventory accounting does not mean physical items exist in game. This archive includes the upstream project's OfflineDB template, not personal player data. No credentials, saves, logs, databases, game translations or game assets are included.


Local6 features and verification / Возможности и проверки: [Русский](LOCAL6.ru.md) · [English](LOCAL6.en.md).
