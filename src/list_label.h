#pragma once
#include <string>
#include <unordered_map>
#include <cctype>
#include <istream>
#include <cstring>

inline std::unordered_map<std::string,std::string> ReadLocalizedListNames(std::istream& input) {
    std::unordered_map<std::string,std::string> result;
    std::string line;
    while(std::getline(input,line)) {
        if(!line.empty() && line.back()=='\r') line.pop_back();
        if(line.rfind("\xEF\xBB\xBF",0)==0)line.erase(0,3);
        const auto eq=line.find('=');
        if(eq==std::string::npos || eq+1==line.size())continue;
        std::string key=line.substr(0,eq);
        const char* prefixes[]={"item_Name_","item_Name","vehicle_Name_","vehicle_Name"};
        for(auto prefix:prefixes)if(key.rfind(prefix,0)==0) {
            key.erase(0,strlen(prefix));
            for(char& c:key)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            auto value=line.substr(eq+1);
            if(!key.empty() && value.find("\\n")==std::string::npos && value[0]!='@')result.insert_or_assign(key,std::move(value));
            break;
        }
    }
    return result;
}

// Display only: never replace a class passed to the game with this label.
inline std::string ListDisplayLabel(const char* raw) {
    static const std::unordered_map<std::string, const char*> words = {
        {"outpost parts","Модули аванпостов"},{"sq42","Эскадрилья 42"},
        {"outlaw","Преступники"},{"otlw","Преступники"},{"depot","Склад"},
        {"layout","Планировка"},{"cluster","Группа модулей"},{"shelter","Убежище"},
        {"hab","Жилой модуль"},{"ruin","Руины"},{"building","Здание"},
        {"cargo","Груз"},{"counter","Прилавок"},{"bench","Скамья"},
        {"rack","Стеллаж"},{"mannequin","Манекен"},{"fruit","Фрукты"},
        {"plant","Растение"},{"weapon","Оружие"},{"watertank","Бак воды"},
        {"shop","Магазин"},{"containers","Контейнеры"},{"stall","Торговая стойка"},
        {"box","Ящик"},{"food","Еда"},{"stool","Табурет"},{"booth","Кабина"},
        {"display","Дисплей"},{"components","Компоненты"},{"stand","Подставка"},
        {"full","Полный"},{"single","Одиночный"},{"static","Статичный"},
        {"lootable","С содержимым"},{"int","Интерьер"},{"ext","Внешний"},
        {"xs","Очень малый"},{"sml","Малый"},{"med","Средний"},{"lge","Большой"},
        {"entry","Вход"},{"sand","Песок"},{"red","Красный"},{"frost","Лёд"},
        {"bar","Бар"},{"hub","Центральный модуль"},{"human","Человек"},
        {"armor","Броня"},{"combat","Боевой"},{"light","Лёгкая"},
        {"medium","Средняя"},{"heavy","Тяжёлая"},{"superheavy","Сверхтяжёлая"},
        {"core","Корпус"},{"helmet","Шлем"},{"arms","Руки"},{"legs","Ноги"},
        {"undersuit","Поддоспешник"},{"flightsuit","Лётный костюм"},{"suit","Костюм"},
        {"backpack","Рюкзак"},{"pistol","Пистолет"},{"rifle","Винтовка"},
        {"sniper","Снайперская"},{"shotgun","Дробовик"},{"smg","Пистолет-пулемёт"},
        {"lmg","Пулемёт"},{"glauncher","Гранатомёт"},{"crossbow","Арбалет"},
        {"energy","Энергетический"},{"ballistic","Баллистический"},{"mag","Магазин"},
        {"gren","Граната"},{"grenade","Граната"},{"frag","Осколочная"},
        {"multitool","Мультитул"},{"medgun","Медицинский инструмент"},{"vial","Картридж"},
        {"repair","Ремонт"},{"salvage","Утилизация"},{"filled","Заполненный"},
        {"resource","Ресурс"},{"env","Для опасной среды"},{"xenothreat","XenoThreat"},
        {"seat","Кресло"},{"pilot","Пилот"},{"copilot","Второй пилот"},
        {"gunner","Стрелок"},{"turret","Турель"},{"captain","Капитан"},
        {"engineer","Инженер"},{"operator","Оператор"},{"bridge","Мостик"},
        {"navy","Флот"},{"marine","Морпех"},{"fatigues","Полевая форма"},
        {"jumpsuit","Комбинезон"},{"medic","Медик"},{"commander","Командир"},
        {"lieutenant","Лейтенант"},{"officer","Офицер"},{"commodore","Коммодор"},
        {"deck","Палубный"},{"crew","Экипаж"},{"training","Учебный"},
        {"shorts","Шорты"},{"become","Образ"},{"experimental","Экспериментальный"},
        {"outposts","Аванпосты"},{"outpost","Аванпост"},{"parts","Части"},
        {"guards","Охрана"},{"bunkers","Бункеры"},{"habs","Жилые модули"},
        {"ruins","Руины"},{"fob","Передовая база"},{"cover","Укрытия"},
        {"market","Рынок"},{"base","База"},{"furniture","Мебель"},
        {"storage","Хранение"},{"lights","Освещение"},{"turrets","Турели"},
        {"deco","Декорации"},{"props","Предметы"},{"displays","Дисплеи"},
        {"crate","Ящик"},{"container","Контейнер"},{"chair","Стул"},
        {"table","Стол"},{"bed","Кровать"},{"door","Дверь"},{"wall","Стена"},
        // Weapon attachment and multitool class parts (Claude, Local5 2026-10-13): our readable
        // fallback for class tokens, not global.ini text. Ambiguous tokens (barrel, lamp) stay as is.
        {"optics","Прицел"},{"ubarrel","Подствольный модуль"},{"holo","Голографический"},
        {"rdot","Коллиматорный"},{"supp","Глушитель"},{"comp","Компенсатор"},
        {"stab","Стабилизатор"},{"flsh","Фонарь"},{"lasr","Лазерный целеуказатель"},
        {"mining","Добыча"},{"cutter","Резак"},{"tractorbeam","Тяговый луч"},{"healing","Лечение"}
    };
    std::string whole=raw?raw:"";
    for(char& c:whole) if(static_cast<unsigned char>(c)<128)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if(auto it=words.find(whole);it!=words.end())return it->second;
    std::string result, token;
    auto append = [&] {
        if (token.empty()) return;
        std::string key=token;
        for (char& c:key) if (static_cast<unsigned char>(c)<128) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        auto it=words.find(key);
        if (!result.empty()) result += " · ";
        result += it==words.end()?token:it->second;
        token.clear();
    };
    for (const unsigned char* p=reinterpret_cast<const unsigned char*>(raw?raw:""); *p; ++p) {
        if (*p=='_' || *p=='-' || *p==' ' || *p=='/' || *p=='\\') append();
        else token+=static_cast<char>(*p);
    }
    append();
    return result;
}
