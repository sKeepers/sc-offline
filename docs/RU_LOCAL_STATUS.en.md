# sc-offline RU / Local — Development Status

[Русская версия](RU_LOCAL_STATUS.md)

Updated: **October 12, 2026**.

This document describes a separate local edition based on [scubamount/sc-offline](https://github.com/scubamount/sc-offline). Original project attribution and the GPL-3.0 license are retained. Local changes are developed with assistance from Codex; some research models are developed with assistance from Claude.

**Current local build: sc-offline 0.7.7 RU / Local 1.** Base: tag `v0.7.7`, commit `1ac5308a65ac0ba7955e4108bd1dc800449cfa69`; sco-core: `2197dfbf5a630e5a16a201526e628f1d28df908a`.

This document reports local development results. The complete RU / Local source set and its binary release have not yet been published in this fork. The main branch and upstream Download links in the README should not be treated as a package of this edition.

## Implemented locally

| Area | Additions | Validation and limits |
|---|---|---|
| Russian interface | Menu and diagnostic translations; readable ship, NPC, item and object names; partial-name and original-class search | Parts of the interface have been checked by the user; complete translation of all game resources is not claimed |
| Launcher | Version in the native window title and visible header; flight profile; player display name | The name changes in the mod UI; the client's internal player identifier remains unchanged |
| Local server | Fleet, inventory ledger and storage-location dashboard; selection catalogs; dashboard data refresh | Separate SQLite bookkeeping, not a complete implementation of the game's servers |
| Server controls | Automatic startup when Play is selected; start and graceful-stop buttons; reuse of an already running server | Start/stop/restart tested; data and pairing token retained |
| Plugins | Exploration and loadout presets moved into separate plugins with a host adapter | Loading, UI and preset persistence/application tested outside the game; this does not persist physical character inventory |
| Menu window | Adjustable width and height; size persistence across sessions, constrained to the monitor work area | Resizing confirmed; restoration in a subsequent game session still needs verification |
| Starting locations | Stanton/Pyro location selection in the flight profile; travel between loaded system zones | Manual arrival at ArcCorp confirmed; corrected automatic startup order, player → ship → seat, installed and awaiting an in-game test |
| Updates | Migrated to base 0.7.7, including the competing death-hook fix; protection against upstream auto-update overwriting RU / Local | Matching EXE/DLL/plugins built; installation backed up and protected-data hashes checked; gameplay after migration not yet verified |

## Not ready yet

- **NPC companion:** following and guarding are not confirmed. Regular commands are temporarily disabled after a crash. Relationship diagnostics and a limited, one-shot trial without assigning a buddy leader are available; the trial requires an NPC that is actually peaceful. `Security_Lawful` does not guarantee friendliness toward the player.
- **Game-item persistence:** a source for real character inventory is still unavailable. Physical cargo, ship damage and shop purchases are not restored by the inventory ledger.
- **Bed and medical registration:** exact restoration from a ship bed or medical-terminal registration is not implemented.
- **Power and engines:** sending Flight Ready does not prove that engines are running; missing seat dashboards remain an issue in some cases.
- **Network play:** LAN/VPN preparation and local checks are complete. A two-PC session still needs testing. Another player's visual presence does not imply shared physics, missions, cargo or inventory.

## Independent validation completed

- Updated sco-core: **20** death-event checks and **217** storage checks, with no failures.
- SDK boundary checks: **19** tests; release manifest: **12** tests.
- Server, catalog and server controls: **24** tests using isolated databases and ports.
- Local plugins: loading, rejection when the adapter is unavailable, UI rendering, unloading, a Cyrillic preset name and persistence across plugin reload.
- Key lookup on pinned SQLite 3.53.4: **107,963** comparisons between two SQL queries and an independent reference, with no discrepancies. Near the end of a test range of 3,000 keys, VM steps fell from 12,015 to 11. This optimization is not yet enabled in the working SDK.

Pure models and standalone checks do not replace in-game validation of NPC behavior, streaming, engines or UI. Reported tests of third-party research packages are distinguished from checks independently executed here.

## Next steps

1. Verify world loading after the 0.7.7 migration, automatic arrival at the selected location and menu-size restoration.
2. Continue companion research: relationships, stable target/assignment identity, native cancellation and evidence of actual movement.
3. Test the SQL optimization through the real host API; independently validate the snapshot model, compatibility with older saves and quota estimates.
4. Connect a reliable game-item source before implementing restoration; keep large storage operations out of the game frame.
5. Test a session with a friend over VPN and document the capabilities actually observed.

Personal databases, tokens, memory dumps, game archives and original game assets are excluded from public reports. Updating this description does not constitute a binary release or completion of the research listed above.
