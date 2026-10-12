#include "../src/list_label.h"
#include <sstream>
#include <cassert>
#include <iostream>
int main() {
    std::istringstream ini("\xEF\xBB\xBFitem_Name_ccc_medium_armor_core_01_01_01=Aves Core\r\n"
        "item_Namebehr_rifle_ballistic_01=P4-AR Rifle\n"
        "vehicle_NameAEGS_Avenger_Titan=Avenger Titan\n"
        "item_Desc_test=Ignore descriptions\nitem_Name_skip=@unresolved\n"
        "item_Name_multiline=Line1\\nLine2\nitem_Name_empty=\n"
        "item_Name_complex=Name=Variant\n");
    const auto names=ReadLocalizedListNames(ini);
    assert(names.size()==4);
    assert(names.at("ccc_medium_armor_core_01_01_01")=="Aves Core");
    assert(names.at("behr_rifle_ballistic_01")=="P4-AR Rifle");
    assert(names.at("aegs_avenger_titan")=="Avenger Titan");
    assert(names.at("complex")=="Name=Variant");
    const std::string raw="ccc_medium_armor_core_01_01_01";
    const auto label=ListDisplayLabel(raw.c_str());
    assert(raw=="ccc_medium_armor_core_01_01_01");
    assert(label.find("Средняя")!=std::string::npos);
    assert(label.find("Корпус")!=std::string::npos);
    assert(label.find("01 · 01 · 01")!=std::string::npos);
    assert(ListDisplayLabel("Gunner_Seat_2").find("Стрелок · Кресло · 2")!=std::string::npos);
    assert(ListDisplayLabel(nullptr).empty());
    assert(ListDisplayLabel("outpost parts")=="Модули аванпостов");
    assert(ListDisplayLabel("outlaw_outpost_a")=="Преступники · Аванпост · a");
    assert(ListDisplayLabel("xs_depot_otlw_c_001").find("001")!=std::string::npos);
    std::cout<<"List labels: localization parsing, class identity, variants and seat roles OK\n";
}
