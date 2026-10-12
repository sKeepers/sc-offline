#pragma once
#include <string>
#include <algorithm>

// Display only: keep the original entity class for spawning and persistence.
inline std::string ShipDisplayLabel(const char* entityClass) {
    std::string label = entityClass;
    const auto split = label.find('_');
    const std::string prefix = label.substr(0, split);
    struct Manufacturer { const char* code; const char* name; };
    static const Manufacturer manufacturers[] = {
        {"AEGS","Aegis"},{"ANVL","Anvil"},{"ARGO","Argo"},
        {"BANU","Banu"},{"CNOU","Consolidated Outland"},{"CRUS","Crusader"},
        {"DRAK","Drake"},{"ESPR","Esperia"},{"GAMA","Gatac"},
        {"GRIN","Greycat"},{"KRIG","Kruger"},{"MISC","MISC"},
        {"MRAI","Mirai"},{"ORIG","Origin"},{"RSI","RSI"},
        {"TMBL","Tumbril"},{"VNCL","Vanduul"},{"XIAN","Aopoa"}
    };
    if (split != std::string::npos)
        for (const auto& m : manufacturers)
            if (prefix == m.code) { label = std::string(m.name) + " " + label.substr(split + 1); break; }
    size_t variant = std::string::npos;
    for (const auto* token : {"_PU_", "_AI_", "_Unmanned_", "_Template", "_TEMP_"})
        variant = (std::min)(variant, label.find(token));
    if (variant != std::string::npos) label.replace(variant, 1, " — ");
    std::replace(label.begin(), label.end(), '_', ' ');
    return label;
}
