# sc-offline RU / Local

Локальная редакция [scubamount/sc-offline](https://github.com/scubamount/sc-offline) для автономной игры в Star Citizen. Основана на ChrisWareOffline; авторство и лицензия GPL-3.0 сохранены.

**Опубликована сборка 0.7.7 RU / Local 5 — предварительный релиз для Windows x64.**

[Скачать Windows ZIP](https://github.com/sKeepers/sc-offline/releases/download/ru-local-0.7.7-5/sc-offline-0.7.7-ru-local-5-windows-x64.zip) · [Releases](https://github.com/sKeepers/sc-offline/releases/tag/ru-local-0.7.7-5) · [Описание на русском](docs/RU_LOCAL_STATUS.md) · [English status](docs/RU_LOCAL_STATUS.en.md) · [Исходники Local 5](https://github.com/sKeepers/sc-offline/tree/codex/release-0.7.7-ru-local-5)

## Русский

- Лаунчер с отдельными UTF-8 словарями: 306 текстов, русский/английский интерфейс и английский запасной перевод.
- Автоматическое обнаружение локализации в `LIVE/data/Localization`, включая русский перевод в другой языковой папке; чтение названий кораблей и предметов без изменения игровых файлов.
- Читаемые списки и поиск по части названия, профиль полёта, маршруты, комплекты снаряжения, изменение и сохранение размера меню.
- Локальная веб-панель для учёта флота/склада; автоматический старт сервиса и кнопки запуска/остановки.
- Шесть плагинов: Creative, Discord, Exploration, Loadout Presets, NPC и Inspector; основа sco-core SDK 2.0.

**Начало работы:** распакуйте Windows ZIP в отдельную папку и запустите `sc-offline.exe`. Требуется собственная установленная игра; для веб-панели — Python 3.11+. В профиле полёта выбираются старт, корабль и язык. Меню мода открывается M, двигатели переключаются I. Инструкции `RELEASE.ru.md` и описание системных изменений `docs/launcher.md` находятся в архиве.

**Статус:** это сборка для тестирования. Следование/охрана NPC отключены после сбоя; штатная дружественность NPC не гарантируется. Сеть поддерживает ограниченный обмен позициями и визуальное присутствие, без общей физики, урона, миссий и инвентаря. Веб-панель ведёт учёт и не является полноценным игровым dedicated server. Старты у станций экспериментальные; native DEFAULT_1 в mobiGlas ещё не исправлен. Меню M, собственные плагины и веб-панель пока русские. Полный игровой прогон этой комплектации и визуальная проверка новой локализации ещё ожидаются.

Перед обновлением сохраните настройки и данные. В релизе нет личных сохранений, паролей, токенов, ресурсов и переводов игры. Мод предназначен для автономного режима; изучите настройки лаунчера и штатную очистку перед возвращением к онлайн-игре.

## English

**0.7.7 RU / Local 5 is available as a Windows x64 development pre-release.**

- Flat UTF-8 launcher catalogs: 306 strings, Russian/English UI, compiled English fallback.
- Automatic discovery of installed game localization and readable ship/item names; partial-name and identifier search.
- Flight profiles, exploration routes, loadout presets and persistent resizable mod menu.
- Local fleet/storage ledger dashboard with automatic service startup and start/stop controls.
- Six plugins, including external NPC/Inspector modules, on sco-core SDK 2.0.

Extract the Windows ZIP into a separate folder and run `sc-offline.exe`. Bring your own installed game; Python 3.11+ is needed for the optional dashboard. See `RELEASE.en.md` and `docs/launcher.md` in the archive.

Companion follow/guard remain quarantined; NPC friendliness is unproven. Multiplayer offers limited pose/visual co-presence, without shared physics, damage, missions or inventory. The ledger is not a dedicated game simulation server or physical inventory. Station starts are experimental; native mobiGlas DEFAULT_1 remains unresolved. The M menu/local plugins/dashboard remain Russian. This exact bundle has not completed full in-game acceptance or visual localization checks.

No saves, credentials, game resources or game translations are distributed. Back up user configuration/data before upgrading; use offline mode and the launcher's normal cleanup before online play.

## Source / Исходники

Local 5 source branch: [`codex/release-0.7.7-ru-local-5`](https://github.com/sKeepers/sc-offline/tree/codex/release-0.7.7-ru-local-5).

**Main сейчас содержит обновлённую документацию поверх прежней основы. Для исходников Local 5 используйте указанную ветку/тег или complete-source ZIP в релизе.**

**Main currently contains updated documentation on the earlier code base. Use the release branch/tag or the attached complete-source ZIP for Local 5 code.**

The complete-source ZIP includes sco-core and third-party licenses; GitHub's automatic source ZIP omits submodules. SHA256SUMS.txt covers the two release ZIPs. Local build/autonomous checks passed; they are separate from gameplay validation.

[Оригинальный проект / Upstream](https://github.com/scubamount/sc-offline) · [Лицензия / License](LICENSE)


## Local 5 correction / Исправление Local 5

Исправлены регистрация loadout команд и вкладки персонажей, очистка handles и ошибки локализации. Подробности: [RU](LOCAL5-FIXES.ru.md) / [EN](LOCAL5-FIXES.en.md). В отличие от исходной Local4, новые проверки используют и настоящий sco-core host:13/0, с заглушками игровых функций и UI. В игре вкладки ещё не подтверждены. PR127 насадки не включены.

Corrects loadout command/tab admission, owner cleanup and localization. Real sco-core host checks13/0, with stubbed game/UI callbacks. In-game acceptance is pending; PR127 attachments are not included. Local4 remains available as the preceding release.
