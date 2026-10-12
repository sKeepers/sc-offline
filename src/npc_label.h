#pragma once
#include <string>
#include <algorithm>
#include <cctype>

// Display only. Unknown tokens and variant numbers remain visible.
inline std::string NpcDisplayLabel(const char* entityClass) {
    struct Token { const char* key; const char* text; };
    static const Token tokens[] = {
        {"pu","PU"},{"sc","SC"},{"npc","NPC"},{"human","Человек"},
        {"enemy","Противник"},{"groundcombat","Наземный бой"},
        {"security","Охрана"},{"lawful","Законопослушный"},
        {"criminal","Преступник"},{"criminial","Преступник"},
        {"guard","Охранник"},{"male","Мужчина"},{"female","Женщина"},
        {"captain","Капитан"},{"elite","Элитный"},{"cqc","Ближний бой"},
        {"grunt","Рядовой"},{"soldier","Солдат"},{"sniper","Снайпер"},
        {"juggernaut","Джаггернаут"},{"sentry","Часовой"},
        {"techie","Техник"},{"pilot","Пилот"},{"acepilot","Пилот-ас"},
        {"grenadier","Гренадёр"},{"scav","Мусорщик"},
        {"shopkeeper","Продавец"},{"worker","Рабочий"},
        {"doctor","Врач"},{"engineer","Инженер"},{"gardener","Садовник"},
        {"homeless","Бездомный"},{"prisoner","Заключённый"},
        {"populace","Житель"},{"civilian","Гражданский"},
        {"heavy","Тяжёлый"},{"light","Лёгкий"},{"medium","Средний"},
        {"boss","Босс"},{"shotgun","Дробовик"},
        {"ninetails","Nine Tails"},{"genericcriminal","Преступник"},
        {"bounty","Цель охоты"},{"easy","Лёгкий"},{"hard","Сложный"},
        {"very","Очень"},{"rare","Редкий"},{"endgame","Высокий уровень"},
        {"powersuit","Силовой костюм"},{"quadruped","Четвероногое"}
    };
    const std::string raw=entityClass;
    std::string result;
    for(size_t at=0;at<raw.size();) {
        const auto end=raw.find_first_of("_-",at);
        const auto word=raw.substr(at,end==std::string::npos?end:end-at);
        auto lower=word;
        std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        const char* translated=nullptr;
        for(const auto& t:tokens) if(lower==t.key) {translated=t.text;break;}
        if(!word.empty()) {if(!result.empty()) result+=" · ";result+=translated?translated:word;}
        if(end==std::string::npos) break;
        at=end+1;
    }
    return result;
}
