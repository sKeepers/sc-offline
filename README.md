# sc-offline RU / Local

**Актуальный предварительный пакет: 0.7.7 / Local10 candidate + сервер 0.3.3.**

[Скачать сборку и исходники](https://github.com/sKeepers/sc-offline/releases/tag/ru-local-0.7.7-10-server-0.3.3) · [Описание релиза](docs/RELEASE_LOCAL10.ru.md) · [Инструкция проверки](docs/TESTING_SERVER_033.ru.md)

Windows x64. Для локального Python-сервера нужен Python 3.11+. Игра в пакет не входит.
Распакуйте в отдельную папку, сохраните личные данные предыдущей версии и начните с
`server/Start-Server.cmd`. Инструкция находится на http://127.0.0.1:18870/testing;
ссылки «Проверка игры» есть в панели и инструментах.

## Что нового после Local6

* Домашняя точка и постоянный ID профиля; возможность «Сделать домом» в Exploration.
* Русские описания плагинов; семь добавленных классов кораблей, включая Constellation Mk V Centaurus.
* Улучшения старта, фильтра принадлежащих кораблей, терминала, подтверждения кресла и питания.
* Сервер 0.3.3: локальные покупки и квитанции, флот, проверяемый каталог базовых цен,
  наблюдение журналов, планы маршрутов, таймеры переработки и встроенная инструкция.

83 серверных теста прошли; CMake configure полного исходного ZIP проверен. Бинарники
Local10 совпадают по SHA256 с установленной сборкой, ранее собранной с SDK2.0.
Они не пересобирались ради обновления версии Python-сервера. При упаковке игра не запускалась.

## Что ещё требует проверки

Полная игровая приёмка не закончена. Подтверждены появление, кресло и питание одной
Andromeda; двигатели включились вручную через I. Автоматический запуск двигателей,
безопасное появление дома и все новые модели пока не подтверждены.
LOCAL_CREDIT — локальный учёт: native aUEC и физическая выдача предметов не подключены.
Нет подтверждённого полного цикла ангара/лифта/ATC, общего multiplayer-мира или безопасных
безусловных межсистемных переходов. Следование/охрана NPC отключены. Схема сервера2;
экспериментальная миграция схемы3 не включена.

**Полные соответствующие исходники Local10 — в complete-source ZIP релиза.**
C++-основа main пока более ранняя; main содержит текущие документы и сервер 0.3.3.
Для пересборки Local10 используйте полный исходный архив, включая pinned sco-core.
Старые пакеты Local4–Local6 сохранены в Releases.

## English

The current Windows x64 **development pre-release** is Local10 candidate with local
server0.3.3. [Downloads, complete corresponding source and SHA256 checksums](https://github.com/sKeepers/sc-offline/releases/tag/ru-local-0.7.7-10-server-0.3.3).
83 server tests and complete-source CMake configuration passed. Native binaries match
the installed Local10 build; full gameplay acceptance is pending. Engine start still
needs manual I; home arrival and new ship models are unverified. Ledger purchases do
not deliver native inventory or spend native aUEC. No game assets, private state,
tokens or logs are distributed. Use the complete-source archive to rebuild Local10;
the main C++ tree is an earlier base, while main has current docs and server sources.

## Авторство и лицензия

Локальная редакция [scubamount/sc-offline](https://github.com/scubamount/sc-offline),
на основе ChrisWareOffline; GPL-3.0, [LICENSE](LICENSE).
SDK: [scubamount/sco-core](https://github.com/scubamount/sco-core), pinned cd9a686 (SDK2.0).
Локальный сервер и мост адаптированы из [nickcarmichaelw12/sc-offline](https://github.com/nickcarmichaelw12/sc-offline), b1d2721.
Claude подготовил домашнюю точку и проверенные исправления; Codex выполнил интеграцию,
русификацию, независимые проверки и упаковку. Подробности — в исходниках и ATTRIBUTION.md.

Ресурсы игры, извлечённые игровые JSON, личные базы/профили/дома, токены, журналы,
скриншоты и частный обмен не публикуются. Не используйте пакет для подключения к CIG.
