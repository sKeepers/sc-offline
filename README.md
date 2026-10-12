# sc-offline 0.7.7 RU / Local 4

Community development build based on [scubamount/sc-offline](https://github.com/scubamount/sc-offline), with sco-core SDK 2.0.

**Русский:** скачайте Windows ZIP из [Releases](https://github.com/sKeepers/sc-offline/releases), распакуйте в отдельную папку и запустите `sc-offline.exe`. Прочитайте [инструкцию](RELEASE.ru.md). Это предварительная сборка; часть функций ещё не подтверждена в игре.

**English:** download the Windows ZIP from [Releases](https://github.com/sKeepers/sc-offline/releases), extract into a separate folder and run `sc-offline.exe`. Read the [instructions](RELEASE.en.md). This is a development pre-release with unverified game paths.

- UTF-8 launcher language catalogs, English fallback and game-localization discovery.
- Readable names and partial search, flight profiles, experimental station starts, exploration/preset plugins.
- Resizable/persistent mod menu; local fleet ledger and dashboard; limited multiplayer diagnostics.
- External NPC/Inspector SDK plugins; companion follow/guard remain quarantined after a crash.

The dashboard is an accounting service, not a dedicated Star Citizen simulation server. Physical inventory, shared physics/damage/missions, reliable companion following and native mobiGlas name replacement are not provided.

Upstream main: `85774b784d4cc8eb5dda75148387318486971ef6`.
Core pin: `cd9a686bda4184f1befbdf0b74b503a1565540dd`.
Source: GPL-3.0; see LICENSE and third-party notices. No game installation, translations, user saves or credentials are distributed.

Based on ChrisWareOffline 0.9.0-rc1 and scubamount/sc-offline. All existing third-party licenses are retained.

The complete source archive attached to the release includes sco-core. For a Git clone, use `git -c core.autocrlf=false clone --recurse-submodules --branch ru-local-0.7.7-4 https://github.com/sKeepers/sc-offline.git`. Follow docs/build.md; NPC/Inspector additionally build using the published SDK as described in docs/sdk-workflows.md.
