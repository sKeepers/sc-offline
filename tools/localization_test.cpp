#include "../local/localization.h"
#include "../src/list_label.h"
#include <cassert>
#include <iostream>
int main(int argc,char** argv) {
    using namespace LocalI18n;
    assert(argc==3);auto root=std::filesystem::path(argv[1]);auto catalogs=std::filesystem::path(argv[2]);
    std::filesystem::create_directories(root/"data/Localization/korean_(south_korea)");
    std::filesystem::create_directories(root/"data/Localization/german_(germany)");
    std::ofstream(root/"data/languages.ini",std::ios::binary)<<"english=English\nkorean_(south_korea)=Русский\ngerman_(germany)=Deutsch\n";
    std::ofstream(root/"user.cfg")<<"g_language = korean_(south_korea)\ng_languageAudio = english\n";
    std::ofstream(root/"data/Localization/korean_(south_korea)/global.ini",std::ios::binary)<<"item_Name_Test=Предмет\nvehicle_Name_AEGS_Test=Корабль\n";
    std::ofstream(root/"data/Localization/german_(germany)/global.ini")<<"vehicle_Name_AEGS_Test=Schiff\n";
    auto s=Discover(root,"auto","auto","en");assert(s.ui=="ru"&&s.slot=="korean_(south_korea)"&&s.available.size()==2);
    s=Discover(root,"de","auto","ru");assert(s.ui=="de"&&s.slot=="german_(germany)");
    s=Discover(root,"ru","german_(germany)","en");assert(s.ui=="ru"&&s.slot=="german_(germany)");
    s=Discover(root,"auto","../bad","en");assert(s.slot=="korean_(south_korea)");
    assert(!SafeCode("../../file")&&!SafeCode("x\\y")&&!SafeCode("x:y"));
    std::filesystem::remove(root/"user.cfg");s=Discover(root,"auto","auto","ru");assert(s.ui=="ru");
    std::ofstream(root/"user.cfg")<<"g_language = english\n";
    s=Discover(root,"auto","auto","ru");assert(s.ui=="en"&&s.slot.empty());
    auto parsed=Parse("# comment\na = hello\\nworld\\s\na = duplicate\nb = x=y\nc = bad\\q\n",true);
    assert(parsed["a"]=="hello\nworld "&&parsed["b"]=="x=y"&&!parsed.count("c"));
    assert(!Utf8(std::string("\xC0\xAF",2))&&!Utf8(std::string("\xED\xA0\x80",3))&&!Utf8(std::string("\0",1))&&Utf8("Русский 日本語"));
    assert(Compatible("%ls %d","имя %ls %d")&&!Compatible("%ls %d","%s %d")&&!Compatible("text","%*s")&&!Compatible("%d","%n"));
    assert(!Compatible("Save##id","Сохранить##changed"));
    Catalog catalog;
    #include "../local/launcher_catalog.inc"
    assert(catalog.Load(catalogs/"ru.ini")>300);catalog.Seal();
    assert(std::string(catalog.Text("launcher.language.ui"))=="Язык лаунчера");
    assert(std::wstring(catalog.W("launcher.language.ui"))==L"Язык лаунчера");
    auto ruId=std::string(catalog.UI("launcher.language.ui"));assert(ruId.find("###launcher.language.ui")!=ruId.npos);
    Catalog english;english.Add("launcher.language.ui","Launcher language");english.Seal();
    assert(Id(ruId)==Id(english.UI("launcher.language.ui")));
    std::ofstream(root/"broken.ini")<<"launcher.language.ui = %n\nlauncher.not_a_key = ignore\n";
    assert(english.Load(root/"broken.ini")==0&&std::string(english.Text("launcher.language.ui"))=="Launcher language");
    assert(!english.Load(root/"missing.ini"));
    std::string bytes;assert(Read(root/"data/Localization/korean_(south_korea)/global.ini",bytes));
    std::istringstream names(bytes);auto labels=ReadLocalizedListNames(names);assert(labels["aegs_test"]=="Корабль");
    std::ofstream(root/"invalid.ini",std::ios::binary)<<"\xFF";assert(!Read(root/"invalid.ini",bytes));
    std::ofstream(root/"too_big.ini")<<"abcd";assert(!Read(root/"too_big.ini",bytes,3));
    std::cout<<"PASS localization: fallback, UTF-8/UTF-16, placeholders, stable UI IDs, slots, names, invalid/bounded input\n";
}
