# Development after Local6: Local7–Local9

Updated October 13, 2026. **Published release: 0.7.7 RU / Local6.
Current installed test build: Local9.** The changes below are not included in
the downloadable Local6 release and are not claimed as accepted in-game.

[Русский](DEVELOPMENT_LOCAL9.ru.md) · [Latest published release](https://github.com/sKeepers/sc-offline/releases/tag/ru-local-0.7.7-6)

| Area | Implementation | Validation and limits |
|---|---|---|
| Startup, Local7 | Bounded readiness waits, retries and arrival observation | 44 independent checks; cross-system travel and actual Stanton startup remain unverified |
| Map choice, Local8 | PU / PU_All / custom map is preserved; profile saving no longer forces PU_All; PU rejects Pyro/Nyx destinations | Build and profile read/write checks passed; game acceptance pending |
| ASOP, Local9 | Defaults to classes in the local ownership ledger, preserves stable identifiers; unavailable/empty ledger never exposes the full catalogue | 16 filter checks, protocol checks and 3 C++ HTTP scenarios passed; multiple instances of one class currently produce one row |
| ASOP performance | Faster GUID lookup and preparation-time diagnostics | Preparation time does not measure the entire terminal UI; in-game stalls still need testing |
| Power after boarding | Removed early dispatch; Flight Ready waits for a verified pilot seat, dashboard readiness and a 1.5-second delay | Dispatch success does not prove HUD or engines are powered; Andromeda acceptance pending |
| Web dashboard 0.2.3 | 1102 ship classes, 1590 unique items, 16 categories, localization-aware search/refresh | 32 checks passed; bookkeeping is separate from physical delivery and native purchases |
| Home location | Prepared validation, storage format, backups and process locking; fixed outer-frame coordinate validation | 200 independent checks passed; UI and game spawn integration are not connected |
| User acceptance | HTML checklist, persisted observations, local fresh-log monitoring and report export | 5 test scenarios and browser rendering checked; the user confirms gameplay outcomes |

## Remaining research

Native shop purchases, authoritative game prices, native aUEC deductions and
physical personal inventory delivery are not connected. Adding a ship to the web
ledger records ownership rather than performing a native purchase. ASOP storage,
retrieval and hangar state are not synchronized with SQLite. The M menu retains
a separate creative catalogue. Home, ship-bed and medical spawns are not complete.
NPC follow/guard commands remain disabled after a crash; DEFAULT_1 in mobiGlas is
unresolved. Networking does not provide shared physics, damage, missions or inventory.

## Next acceptance pass

1. Save PU and ordinary spawning, reopen the profile, then check the actual system and location.
2. Compare ASOP with the ownership ledger and measure initial/repeated opening times.
3. Spawn Andromeda through M with pilot boarding and power enabled; check HUD and engines separately after waiting up to one minute.
4. Verify profile and ledger persistence after a normal exit.

Build and synthetic checks cannot replace those observations. A newer package
will have a separate release entry; the Local6 download above remains current.

## Provenance

Base: scubamount/sc-offline `85774b784d4cc8eb5dda75148387318486971ef6`,
sco-core `cd9a686bda4184f1befbdf0b74b503a1565540dd`, SDK 2.0 / ABI 1.1.
Integration changes and independent checks: Codex. Home-storage proposal: Claude,
reviewed and corrected by Codex. Upstream attribution and GPL-3.0 are preserved.
Game assets, personal saves, credentials, logs and private exchange packets are excluded.
