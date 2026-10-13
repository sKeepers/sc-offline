#pragma once
// Local launcher catalog. No plugin ABI changes and no game files are modified.
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#ifdef _WIN32
#include <windows.h>
#endif
namespace LocalI18n {
inline std::string Trim(std::string s) {
    auto a=s.find_first_not_of(" \t\r\n"); if(a==s.npos)return {};
    return s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);
}
inline bool Utf8(const std::string& s) {
    for(size_t i=0;i<s.size();) {
        unsigned char c=s[i++];if(c<128){if(!c)return false;continue;}
        unsigned n=0,v=0,min=0;
        if(c>=0xC2&&c<=0xDF){n=1;v=c&31;min=128;}
        else if(c>=0xE0&&c<=0xEF){n=2;v=c&15;min=2048;}
        else if(c>=0xF0&&c<=0xF4){n=3;v=c&7;min=65536;}else return false;
        while(n--){if(i==s.size())return false;unsigned char d=s[i++];if((d&192)!=128)return false;v=(v<<6)|(d&63);}
        if(v<min||v>0x10FFFF||(v>=0xD800&&v<=0xDFFF))return false;
    }return true;
}
inline bool Read(const std::filesystem::path& file,std::string& bytes,size_t max=2*1024*1024,bool requireUtf8=true) {
    std::error_code ec;auto size=std::filesystem::file_size(file,ec);if(ec||size>max)return false;
    std::ifstream f(file,std::ios::binary);if(!f)return false;
    bytes.clear();char chunk[4096];
    while(f){f.read(chunk,sizeof(chunk));auto n=(size_t)f.gcount();if(n>max-bytes.size())return false;bytes.append(chunk,n);}
    if(bytes.size()>=3&&bytes.compare(0,3,"\xEF\xBB\xBF")==0)bytes.erase(0,3);
    return !f.bad()&&bytes.size()<=max&&(!requireUtf8||Utf8(bytes));
}
inline std::map<std::string,std::string> Parse(const std::string& bytes,bool escapes) {
    std::map<std::string,std::string> out;std::istringstream input(bytes);std::string line;
    while(std::getline(input,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();auto at=line.find('=');
        if(Trim(line).empty()||Trim(line)[0]=='#'||Trim(line)[0]==';'||at==line.npos)continue;
        auto key=Trim(line.substr(0,at)),value=Trim(line.substr(at+1));
        if(key.empty()||key.size()>160||value.size()>32768||out.count(key))continue;
        if(escapes){std::string decoded;bool ok=true;for(size_t i=0;i<value.size();++i){if(value[i]!='\\'){decoded+=value[i];continue;}if(++i==value.size()){ok=false;break;}switch(value[i]){case 'n':decoded+='\n';break;case 'r':decoded+='\r';break;case 't':decoded+='\t';break;case '\\':decoded+='\\';break;case 's':decoded+=' ';break;default:ok=false;}}if(!ok)continue;value=decoded;}
        out.emplace(std::move(key),std::move(value));
    }return out;
}
inline bool SafeCode(const std::string& s) {
    return !s.empty()&&s.size()<80&&s!="."&&s!=".."&&std::all_of(s.begin(),s.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-'||c=='('||c==')';});
}
inline std::vector<std::string> Formats(const std::string& s) {
    static const std::regex pattern("%(?:[-+ #0]*[0-9]*(?:\\.[0-9]+)?(?:hh|ll|I64|[hljztL])?[diuoxXfFeEgGaAcspn]|%)");
    std::vector<std::string> r;
    for(size_t at=0;(at=s.find('%',at))!=s.npos;) {
        std::smatch match;auto rest=s.substr(at);
        if(!std::regex_search(rest,match,pattern,std::regex_constants::match_continuous)){r.push_back("<invalid>"+rest);break;}
        r.push_back(match.str());at+=match.length();
    }return r;
}
inline std::string Id(const std::string& text) {
    if(text.find('\n')!=text.npos)return {}; // Markdown reports contain headings, not widget IDs.
    auto p=text.find("##");return p==text.npos?std::string{}:text.substr(p);
}
inline bool Compatible(const std::string& base,const std::string& value) {
    return !value.empty()&&Utf8(value)&&Formats(base)==Formats(value)&&Id(base)==Id(value);
}
struct Language {std::string slot,label,code;std::filesystem::path global;};
struct Selection {std::string ui="en",slot;std::filesystem::path global;std::vector<Language> available;};
inline std::string LanguageCode(const std::string& slot,const std::string& label) {
    if(label.find("Рус")!=label.npos||label.find("рус")!=label.npos||slot=="russian"||slot.rfind("russian_",0)==0)return "ru";
    if(slot=="english"||slot.rfind("english_",0)==0)return "en";
    for(auto pair:{std::pair{"german","de"},{"french","fr"},{"spanish","es"},{"italian","it"},{"polish","pl"},{"portuguese","pt"},{"korean","ko"},{"japanese","ja"},{"chinese","zh"}})if(slot.rfind(pair.first,0)==0)return pair.second;
    return slot;
}
inline Selection Discover(const std::filesystem::path& root,const std::string& requested,const std::string& overrideSlot,const std::string& osCode) {
    Selection out;std::string bytes;std::map<std::string,std::string> labels;
    if(root.empty()){auto code=requested.empty()||requested=="auto"?osCode:requested;out.ui=SafeCode(code)?code:"en";return out;}
    if(Read(root/"data/languages.ini",bytes))labels=Parse(bytes,false);
    auto folder=root/"data/Localization";std::error_code ec;
    for(std::filesystem::directory_iterator i(folder,ec),end;!ec&&i!=end;i.increment(ec)){
        const auto name=i->path().filename().wstring();
        if(!std::all_of(name.begin(),name.end(),[](wchar_t c){return c>0&&c<128;}))continue;
        std::string slot;for(wchar_t c:name)slot+=static_cast<char>(c);
        auto global=i->path()/"global.ini";
        if(!i->is_directory(ec)||!SafeCode(slot)||!std::filesystem::is_regular_file(global,ec))continue;
        auto label=labels.count(slot)?labels[slot]:slot;
        out.available.push_back({slot,label,LanguageCode(slot,label),global});
    }
    std::sort(out.available.begin(),out.available.end(),[](auto& a,auto& b){return a.slot<b.slot;});
    std::string active;
    if(Read(root/"user.cfg",bytes,2*1024*1024,false)){auto settings=Parse(bytes,false);active=settings["g_language"];if(active.size()>1&&active.front()=='"'&&active.back()=='"')active=active.substr(1,active.size()-2);}
    auto findSlot=[&](const std::string& slot)->const Language*{for(auto& l:out.available)if(l.slot==slot)return &l;return nullptr;};
    const Language* selected=overrideSlot.empty()||overrideSlot=="auto"?nullptr:findSlot(overrideSlot);
    bool autoUi=requested.empty()||requested=="auto";
    std::string wanted=autoUi?(!active.empty()&&SafeCode(active)?LanguageCode(active,labels.count(active)?labels[active]:active):osCode):requested;
    if(!selected&&autoUi)selected=findSlot(active);
    if(!selected)for(auto& l:out.available)if(l.code==wanted){selected=&l;break;}
    if(selected){out.slot=selected->slot;out.global=selected->global;}
    out.ui=SafeCode(wanted)?wanted:"en";return out;
}
#ifdef _WIN32
inline std::wstring Wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);std::wstring w(n,L'\0');if(n)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),w.data(),n);return w;}
#else
inline std::wstring Wide(const std::string& s){return std::wstring(s.begin(),s.end());}
#endif
// Catalogs are immutable after Seal; each pre-Seal Load starts from compiled English.
struct Entry {std::string base,text,ui;std::wstring wide;};
class Catalog {
    std::map<std::string,Entry> entries;
    bool sealed=false;
    static void Set(const std::string& key,Entry& entry,const std::string& text){
        entry.text=text;entry.wide=Wide(text);auto p=text.find("##");
        entry.ui=(p==text.npos?text:text.substr(0,p))+"###"+key;
    }
public:
    void Add(const std::string& key,const std::string& english){if(sealed)return;auto& entry=entries[key];entry.base=english;Set(key,entry,english);}
    size_t Load(const std::filesystem::path& file){
        if(sealed)return 0;
        for(auto& [key,entry]:entries)Set(key,entry,entry.base);
        std::string bytes;if(!Read(file,bytes))return 0;size_t count=0;
        for(auto& [key,value]:Parse(bytes,true)){auto it=entries.find(key);if(it!=entries.end()&&Compatible(it->second.base,value)){Set(key,it->second,value);++count;}}
        return count;
    }
    void Seal(){sealed=true;}
    const char* Text(const char* key)const {auto i=entries.find(key);return i==entries.end()?key:i->second.text.c_str();}
    const char* UI(const char* key)const {auto i=entries.find(key);return i==entries.end()?key:i->second.ui.c_str();}
    const wchar_t* W(const char* key)const {auto i=entries.find(key);return i==entries.end()?L"":i->second.wide.c_str();}
    // Pointer identity only: equal translated labels may belong to different keys.
    // Callers that copy labels must retain and supply their key explicitly.
    const char* Stable(const char* text)const {for(auto& [key,entry]:entries)if(text==entry.text.c_str()||text==entry.ui.c_str())return key.c_str();return text;}
};
inline Catalog catalog;
inline Selection selection;
inline std::string activeUI="en";
inline size_t translatedKeys=0;
inline const char* T(const char* key){return catalog.Text(key);}
inline const char* U(const char* key){return catalog.UI(key);}
inline const wchar_t* W(const char* key){return catalog.W(key);}
}
