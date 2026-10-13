# Development after Local6: Local7–Local10

Updated October 14, 2026. **0.7.7 / Local10 candidate is installed on the test PC.
Local6 remains the latest published package.** Local7–Local10 changes are not in
the downloadable Local6 package. Main contains documentation on an earlier code base.

[Русский](DEVELOPMENT_LOCAL9.ru.md) · [Published release](https://github.com/sKeepers/sc-offline/releases/tag/ru-local-0.7.7-6)

| Area | Change | Evidence and limits |
|---|---|---|
| Startup/map | Bounded startup wait; preserved PU / PU_All / custom map; destination/map checks | 44 startup checks; user confirmed ordinary spawning in an Orison/Stanton bed with PU/normal |
| Ship terminal, Local9 | Classes from the local ownership ledger by default; faster GUID lookup and timing logs | Filter/protocol checks passed; user confirmed responsive opening, reopening and ledger changes. Multiple instances of one class currently produce one row |
| Boarding/power | Fixed two fresh seat-verification errors; request power after pilot confirmation | 17 checks passed. User confirmed pilot boarding and power on one Andromeda after installation. Automatic engines failed; pressing I once enabled them |
| Plugin tooltips | Wrapped purpose descriptions in the launcher language, using stable plugin IDs | Build/localization checks passed; user confirmed readable Russian descriptions |
| Spawn catalogue | Seven missing classes added from the installed 4.10.2 DataCore | Records and installed list checked; spawning the new models is not yet gameplay-verified |
| Home, Local10 candidate | Persistent profile ID; Exploration “Make this home”; launcher home selection; system/zone/seat checks, arrival observation, backups and storage locks | EXE/DLL/Exploration built against the real SDK and installed. 28 independent runner checks plus identity/profile/localization checks passed; storage previously passed 200 checks. Actual arrival and floor safety await game acceptance |
| Web panel 0.2.4 | Owner-defined local offers, purchases, single balance, stock/receipts and idempotency | 41 server tests passed including concurrency/rollback/auth; browser fixture purchase verified. Installed with backup; no physical delivery or native prices |
| User acceptance | HTML checklist, persistent marks and fresh-log monitor | First Local9 pass finished; its monitor stopped. PNG/JPEG attachments and ZIP export added; 6 tests and browser verification passed; separate Local10 home step |

## Spawn catalogue additions

`RSI_Constellation_Mk5_Centaurus`, `RSI_Skylark`,
`RSI_Constellation_Andromeda_GS`, `RSI_Constellation_Aquila_GS`,
`RSI_Constellation_Phoenix_GS`, `RSI_Constellation_Taurus_GS`,
`RSI_Constellation_Taurus_GS_Military`.
Mk V resolves to the actual Centaurus entity class. A localization label alone
was not used to infer its identifier. These are additions to our catalogue,
not a release-date claim. Existing order is preserved; the menu validates registered
classes at the next launch. The creative M list is separate from owned ships.

## October 13 acceptance results

The user confirmed Orison/Stanton bed spawning, terminal responsiveness and reopening,
ledger updates and profile/fleet persistence after exiting. The initial pass found
power failures on Andromeda, Titan and Sabre. After the seat fix, one Andromeda
boarding/power case was confirmed. Automatic engine activation remains unfinished;
manual I works. This does not verify every ship or native hangar storage/retrieval.

## Next acceptance

Installed Local10 candidate: ordinary Stanton spawn → safe room on the floor →
Exploration / free-flight tab / “Make this home” → exit → launcher profile / Home,
PU/Stanton → relaunch. Confirm the actual room and safe footing; dispatch alone
does not prove arrival. Homes inside ships are refused. New mod messages are currently
Russian; launcher strings support EN/RU.

## Still research

Native shop purchases/prices, native aUEC debit and physical inventory delivery
are not integrated. The acquisition model is integrated with the localhost server: one profile.credits balance, transactions and receipts. Offers are owner-defined for LOCAL_CREDIT; native delivery/aUEC remain unavailable. Uncertain delivery cannot be retried.
Native ASOP storage/retrieval is not synchronized with SQLite. Ship-bed and medical
spawning are not ready claims. NPC follow/guard remain disabled; mobiGlas DEFAULT_1
is unresolved. Networking does not provide shared physics, damage, missions or inventory.

## Attribution

Upstream `85774b784d4cc8eb5dda75148387318486971ef6`, sco-core
`cd9a686bda4184f1befbdf0b74b503a1565540dd`, SDK 2.0 / ABI 1.1.
Home storage/planner and proposed integration patch: Claude. Persistent identity,
dictionary merge, integration, corrections, independent checks and build: Codex.
Original authorship and GPL-3.0 are retained. No game assets, personal saves,
tokens, logs or private exchange packets are published.

## Upstream review, October 14

Full review of 62 forks / 164 branches, releases and PRs completed without API errors. No new author guidance in issue121 or PR117/126/127; the changed third-party head equals our exact upstream base. No separate engine-on/getter found in this SDK: power_on sends the same Flight Ready. Automatic engines remain unfinished.
