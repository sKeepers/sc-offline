// Local regression test, written independently of the incoming Claude test programs.
#ifdef LOCAL5_BASELINE
#include "../../research/local5-hotfix-20261013/before/local/localization.h"
#else
#include "../local/localization.h"
#endif
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    const std::filesystem::path root=std::filesystem::path(argv[1]);
    std::filesystem::create_directories(root);
    int failures=0,checks=0;
    auto check=[&](bool ok,const char* description){++checks;if(!ok){++failures;std::cout<<"FAIL "<<description<<std::endl;}};
    auto write=[&](const char* name,const std::string& bytes){std::ofstream(root/name,std::ios::binary)<<bytes;};
    LocalI18n::Catalog duplicate;
    duplicate.Add("a","Remove");duplicate.Add("b","Delete");
    write("duplicate.ini","a=Удалить\nb=Удалить\n");
    check(duplicate.Load(root/"duplicate.ini")==2,"duplicate translations load");duplicate.Seal();
    check(std::string(duplicate.Stable(duplicate.Text("a")))=="a","first translated widget identity");
    check(std::string(duplicate.Stable(duplicate.Text("b")))=="b","second translated widget identity");
    const char* retained=duplicate.Text("b");
    write("changed.ini","a=Changed\nb=Changed\n");
    check(duplicate.Load(root/"changed.ini")==0,"sealed catalog refuses reload");
    // Access retained only on fixed implementation: baseline reload may invalidate its storage.
    if(std::string(duplicate.Text("b"))=="Удалить")check(std::string(retained)=="Удалить","sealed pointer remains valid");
    LocalI18n::Catalog partial;partial.Add("a","Remove");partial.Add("b","Delete");
    partial.Load(root/"duplicate.ini");write("partial.ini","a=Entfernen\n");
    partial.Load(root/"partial.ini");
    check(std::string(partial.Text("b"))=="Delete","partial second language uses English fallback");
    partial.Load(root/"absent.ini");
    check(std::string(partial.Text("a"))=="Remove","missing second language resets English");
    std::filesystem::create_directories(root/"data/Localization/korean_(south_korea)");
    write("data/Localization/korean_(south_korea)/global.ini","x=y\n");
    write("data/languages.ini","korean_(south_korea)=Русский\n");
    write("user.cfg","# \xC0\ng_language=korean_(south_korea)\n");
    auto chosen=LocalI18n::Discover(root,"auto","auto","en");
    check(chosen.ui=="ru"&&chosen.slot=="korean_(south_korea)","ANSI comment does not hide ASCII language setting");
    std::filesystem::create_directories(root/L"data/Localization/日本語");
    try {chosen=LocalI18n::Discover(root,"auto","auto","en");check(chosen.available.size()==1,"non-ASCII directory safely skipped");}
    catch(const std::exception&){check(false,"non-ASCII directory safely skipped");}
    std::filesystem::remove(root/L"data/Localization/日本語");
    const auto saved=std::filesystem::current_path();std::filesystem::current_path(root);
    chosen=LocalI18n::Discover({},"auto","auto","en");std::filesystem::current_path(saved);
    check(chosen.ui=="en"&&chosen.available.empty()&&chosen.slot.empty(),"no game root never reads current directory");
    std::cout<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
