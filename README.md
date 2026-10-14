> **14.10.2026 — Local server 0.3.0:** local game shop catalog, acquired ships in owned fleet, read-only log monitoring, cargo route plans and refinery timers. [Details and limitations (RU)](docs/FORK_FEATURES.ru.md). Game DLL/launcher remain Local10; no native inventory delivery.

# sc-offline RU / Local

## Текущая разработка / Current development

**На тестовом ПК установлен Local10 candidate; последний опубликованный пакет — Local6.**
Добавлены выбор домашней точки и постоянный ID профиля, русские описания плагинов,
семь классов кораблей (включая Constellation Mk V Centaurus). Кандидат собран и установлен;
появление дома и вызов новых моделей ожидают проверки в игре. Посадка и питание одной
Andromeda подтверждены; двигатели пока включаются вручную через I.

**Local10 candidate is installed on the test PC; Local6 remains the published package.**
Home selection/persistent profile identity, plugin descriptions and seven ship classes
are added. Build and independent checks passed; home arrival and new ship spawning
await gameplay verification. One Andromeda boarding/power case is confirmed;
engines still need manual I. Local10 is not yet available as a release download.

Сервер 0.2.4: локальные приобретения с балансом/остатками/квитанциями (41 тест); физическая выдача не подключена. В чеклист добавлены скриншоты и ZIP-экспорт (6 тестов).

Server 0.2.4 adds ledger purchases with balance/stock/receipts (41 tests), without physical delivery. Checklist image attachments and ZIP export added (6 tests).

[Актуальный прогресс и ограничения](docs/DEVELOPMENT_LOCAL9.ru.md) · [Current progress and limitations](docs/DEVELOPMENT_LOCAL9.en.md)

Локальная редакция [scubamount/sc-offline](https://github.com/scubamount/sc-offline) для автономной игры в Star Citizen. Основана на ChrisWareOffline; авторство и лицензия GPL-3.0 сохранены.

**Опубликована сборка 0.7.7 RU / Local 6 — предварительный релиз для Windows x64.**

[Скачать Windows ZIP](https://github.com/sKeepers/sc-offline/releases/download/ru-local-0.7.7-6/sc-offline-0.7.7-ru-local-6-windows-x64.zip) · [Releases](https://github.com/sKeepers/sc-offline/releases/tag/ru-local-0.7.7-6) · [Описание на русском](docs/RU_LOCAL_STATUS.md) · [English status](docs/RU_LOCAL_STATUS.en.md) · [Исходники Local 6](https://github.com/sKeepers/sc-offline/tree/codex/release-0.7.7-ru-local-6)

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

**0.7.7 RU / Local 6 is available as a Windows x64 development pre-release.**

- Flat UTF-8 launcher catalogs: 306 strings, Russian/English UI, compiled English fallback.
- Automatic discovery of installed game localization and readable ship/item names; partial-name and identifier search.
- Flight profiles, exploration routes, loadout presets and persistent resizable mod menu.
- Local fleet/storage ledger dashboard with automatic service startup and start/stop controls.
- Six plugins, including external NPC/Inspector modules, on sco-core SDK 2.0.

Extract the Windows ZIP into a separate folder and run `sc-offline.exe`. Bring your own installed game; Python 3.11+ is needed for the optional dashboard. See `RELEASE.en.md` and `docs/launcher.md` in the archive.

Companion follow/guard remain quarantined; NPC friendliness is unproven. Multiplayer offers limited pose/visual co-presence, without shared physics, damage, missions or inventory. The ledger is not a dedicated game simulation server or physical inventory. Station starts are experimental; native mobiGlas DEFAULT_1 remains unresolved. The M menu/local plugins/dashboard remain Russian. This exact bundle has not completed full in-game acceptance or visual localization checks.

No saves, credentials, game resources or game translations are distributed. Back up user configuration/data before upgrading; use offline mode and the launcher's normal cleanup before online play.

## Source / Исходники

Local 6 source branch: [`codex/release-0.7.7-ru-local-6`](https://github.com/sKeepers/sc-offline/tree/codex/release-0.7.7-ru-local-6).

**Main сейчас содержит обновлённую документацию поверх прежней основы. Для исходников Local 6 используйте указанную ветку/тег или complete-source ZIP в релизе.**

**Main currently contains updated documentation on the earlier code base. Use the release branch/tag or the attached complete-source ZIP for Local 6 code.**

The complete-source ZIP includes sco-core and third-party licenses; GitHub's automatic source ZIP omits submodules. SHA256SUMS.txt covers the two release ZIPs. Local build/autonomous checks passed; they are separate from gameplay validation.

[Оригинальный проект / Upstream](https://github.com/scubamount/sc-offline) · [Лицензия / License](LICENSE)


## Local6

106новых насадок и модулей мультитула с читаемыми списками/поиском и XML-комплектами.1526собственных проверок прошли; работа насадок в игре ещё ожидает подтверждения. Подробности: [Русский](LOCAL6.ru.md) / [English](LOCAL6.en.md).

106weapon attachments/multitool parts, searchable readable lists and XML preset support.1526independent checks pass; game acceptance is pending. The complete-source ZIP now includes two missing bridge test files and was checked by configuring CMake after extraction.

[План работ](docs/IMPLEMENTATION_PLAN.ru.md) · [Local6 source commit](https://github.com/sKeepers/sc-offline/commit/5cbd556677625889d90aec2d773e9ccb9b80f9ed).

[Краткий HTML-отчёт об отличиях](docs/LOCAL_COMPARISON.ru.html). Результаты игровой проверки и статус исправления автопитания: [RU](docs/DEVELOPMENT_LOCAL9.ru.md#результаты-проверки-13-октября) / [EN](docs/DEVELOPMENT_LOCAL9.en.md#october-13-acceptance-results).
