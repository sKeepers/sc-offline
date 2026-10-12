// Included by launcher.cpp after Gui; the dialog edits configuration only, never starts the game.
#pragma once
static wstring ReportStamp() {
    SYSTEMTIME t; GetLocalTime(&t);wchar_t s[80];
    swprintf_s(s,L"%04u%02u%02u-%02u%02u%02u-%03u-%lu",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,GetCurrentProcessId());return s;
}
static bool FreshReportInput(const wstring& p,ULONGLONG since) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    return GetFileAttributesExW(p.c_str(),GetFileExInfoStandard,&a) && FileTimeU64(a.ftLastWriteTime)>=since;
}
static void WriteSessionReport(const wstring& here,const wstring& game,const std::string& build,DWORD exitCode,ULONGLONG since) {
    const wstring dir=here+L"\\data\\sessions"; CreateDirectoryW(dir.c_str(),nullptr);
    const wstring stamp=ReportStamp(), folder=dir+L"\\"+stamp; CreateDirectoryW(folder.c_str(),nullptr);
    std::string report=LocalI18n::T("launcher.sc_offline_launch_report_exploration_1_game_v.cf78bb3464")+build+LocalI18n::T("launcher.process_exit_code.ff31977a75")+std::to_string(exitCode)+"\n\n";
    report+=LocalI18n::T("launcher.the_report_records_mod_messages_feature_avail.a5267e311e");
    std::string text;
    if(FreshReportInput(here+L"\\data\\mod.log",since) && ReadAll(here+L"\\data\\mod.log",text)) {
        size_t from=0;
        while(from<text.size()) {const size_t end=text.find('\n',from);auto line=text.substr(from,end==std::string::npos?std::string::npos:end-from);
            if(line.find("[core]")!=std::string::npos || line.find("[exploration]")!=std::string::npos || line.find("[npc]")!=std::string::npos || line.find("[spawn] blocked")!=std::string::npos || line.find("MISSING")!=std::string::npos || line.find("FAILED")!=std::string::npos || line.find("crashed")!=std::string::npos) report+=line+"\n";
            if(end==std::string::npos)break;from=end+1;
        }
        CopyFileW((here+L"\\data\\mod.log").c_str(),(folder+L"\\mod.log").c_str(),FALSE);
    } else report+=LocalI18n::T("launcher.no_fresh_mod_log_found_an_old_log_was_not_use.98a2a0f615");
    report+=LocalI18n::T("launcher.feature_availability_text.52b4b7f20f");
    if(FreshReportInput(here+L"\\data\\capabilities-ru.txt",since) && ReadAll(here+L"\\data\\capabilities-ru.txt",text))report+=text;
    else report+=LocalI18n::T("launcher.no_feature_report_for_this_session_the_charac.4c88f2eea7");
    report+=LocalI18n::T("launcher.game_crash_text.83db85f913");
    if(FreshReportInput(game+L"\\Game.log",since) && ReadAll(game+L"\\Game.log",text)) {
        bool found=false;size_t from=0;
        while(from<text.size()){auto end=text.find('\n',from);auto line=text.substr(from,end==std::string::npos?std::string::npos:end-from);
            if(line.rfind("Exception ",0)==0 || line.rfind("Is GPU crash:",0)==0 || line.rfind("Is out of",0)==0 || line.rfind("Is fatal error:",0)==0 || line.rfind("- Fatal error message:",0)==0){report+=line+"\n";found=true;}
            if(end==std::string::npos)break;from=end+1;
        }
        if(!found)report+=LocalI18n::T("launcher.no_exception_markers_found_this_does_not_guar.0809a00bb0");
    } else report+=LocalI18n::T("launcher.no_fresh_game_log_found.bf8dddb74b");
    report+="```\n";
    if(WriteAll(folder+L"\\report.ru.md",report)) {
        WriteAll(dir+L"\\latest.ru.md",report);
        Out(LocalI18n::T("launcher.i_launch_report_ls_report_ru_md.ebc8b36a11"),folder.c_str());
    }else Out(LocalI18n::T("launcher.could_not_write_the_launch_report.3223978454"));
}
static void ShowSessionReport() {
    const auto file=g_gui.here+L"\\data\\sessions\\latest.ru.md";
    if(IsFile(file))ShellExecuteW(g_gui.wnd,L"open",L"notepad.exe",(L"\""+file+L"\"").c_str(),nullptr,SW_SHOWNORMAL);
    else AppendOutput(LocalI18n::W("launcher.the_report_will_appear_after_the_next_game_la.e68165fe55"));
}
static bool SelfTestProfileShipNames();
static int SelfTestExplorationReport(const wstring& fixture) {
    // Explicit fixture directory only: no game lookup, helper, network or user configuration.
    CreateDirectoryW(fixture.c_str(),nullptr);CreateDirectoryW((fixture+L"\\data").c_str(),nullptr);
    if(!WriteAll(fixture+L"\\data\\mod.log","[core] signatures: 70/70 OK\n[exploration] session saved: Daymar\n[npc] spawned 1 x Transport\n[spawn] blocked Transport\n") ||
        !WriteAll(fixture+L"\\data\\capabilities-ru.txt",LocalI18n::T("launcher.spawn_ship_available.814ce04800")) ||
        !WriteAll(fixture+L"\\Game.log","private-unrelated-line\nException EXCEPTION_ACCESS_VIOLATION\nIs GPU crash: No\n- Fatal error message: Missing some required components on actor Transport_123\n"))return 1;
    WriteSessionReport(fixture,fixture,"fixture-build",5,0);
    std::string report;if(!ReadAll(fixture+L"\\data\\sessions\\latest.ru.md",report))return 1;
    if(report.find("70/70")==std::string::npos || report.find("spawn.ship")==std::string::npos ||
       report.find("EXCEPTION_ACCESS_VIOLATION")==std::string::npos || report.find("private-unrelated-line")!=std::string::npos ||
       report.find("Missing some required components on actor Transport_123")==std::string::npos ||
       report.find("[npc] spawned 1 x Transport")==std::string::npos || report.find("[spawn] blocked Transport")==std::string::npos)return 1;
    Out("PASS fresh diagnostics and crash markers included; unrelated game lines excluded\n");
    WriteSessionReport(fixture,fixture,"fixture-build",0,~0ull);
    if(!ReadAll(fixture+L"\\data\\sessions\\latest.ru.md",report) || report.find("70/70")!=std::string::npos || report.find("spawn.ship")!=std::string::npos)return 1;
    Out("PASS stale log and capability report excluded\n");
    const auto ini=fixture+L"\\test.ini";
    if(!WriteAll(ini,"# keep comment\nresume = on\nautosave = off\nstart_ship = DRAK_Cutlass_Black\n") || !SetIniValue(ini,"start","Daymar"))return 1;
    Config cfg;ReadConfig(ini,cfg);std::string contents;ReadAll(ini,contents);
    if(!cfg.resume || cfg.autosave || cfg.start!=L"Daymar" || contents.find("# keep comment")==std::string::npos)return 1;
    Out("PASS flight configuration parsed; profile update preserves existing keys and comments\n");
    if(!SelfTestProfileShipNames())return 1;
    Out("PASS readable ship labels resolve to exact classes; partial queries cannot be saved\n");return 0;
}

enum { pfStart=1,pfShip,pfSave,pfCancel,pfAutosave,pfMatches,pfPlayerName,pfSaveName,pfUiLanguage,pfGameLanguage,pfSaveLanguage };
struct FlightProfileSearch {
    struct Ship { wstring cls,label; };
    std::vector<Ship> ships;
    std::vector<Ship> places; // exact entity identifier and readable system/place label
    std::vector<wstring> starts;
    std::vector<std::string> uiCodes,gameSlots;
    bool updating = false;
};
static void ParseProfilePlaces(const std::string& catalog,FlightProfileSearch& search) {
            size_t from=0;
            while(from<catalog.size()) {
                auto end=catalog.find('\n',from);
                auto row=catalog.substr(from,end==std::string::npos?std::string::npos:end-from);
                from=end==std::string::npos?catalog.size():end+1;
                std::vector<wstring> fields; size_t at=0;
                while(at<=row.size() && fields.size()<4) {auto bar=row.find('|',at);fields.push_back(Trim(Wide(row.substr(at,bar==std::string::npos?std::string::npos:bar-at))));if(bar==std::string::npos)break;at=bar+1;}
                if(fields.size()<4 || fields[0].empty() || fields[0][0]==L'#' || fields[2].empty() || fields[2].size()>95)continue;
                wstring low=fields[2];for(auto& c:low)c=(wchar_t)towlower(c);
                const bool compactBody=(low.rfind(L"pyro",0)==0 || low.rfind(L"nyx",0)==0) && low.find(L'_')==wstring::npos && low.find_first_of(L"0123456789")!=wstring::npos;
                const bool lagrange=low.size()==5 && low[0]==L'p' && iswdigit(low[1]) && low[2]==L'_' && low[3]==L'l' && iswdigit(low[4]);
                const bool mainPlace=low.rfind(L"local_station_",0)==0 || low.rfind(L"ooc_",0)==0 || low.rfind(L"jumppoint_",0)==0 || compactBody || lagrange || low.find(L"station")!=wstring::npos || low.rfind(L"rstop_",0)==0;
                bool skip=!mainPlace || fields[0].find(L"SolarSystem")!=wstring::npos;
                for(auto token:{L"_int",L"int_",L"segment",L"gascloud",L"ab_mine",L"social_",L"rs_comm",L"rs_entry",L"deck",L"hangar",L"entrance",L"elev",L"dummy",L"_occu",L"locationharvestable"})if(low.find(token)!=wstring::npos)skip=true;
                if(skip || std::any_of(search.places.begin(),search.places.end(),[&](const auto& p){return p.cls==fields[2];}))continue;
                search.places.push_back({fields[2],fields[0]+L" / "+fields[1]+LocalI18n::W("launcher.aboard_a_ship.2930592317")});
            }
}
static wstring ProfileShipLabel(const wstring& cls) {
    static const auto names=[] {
        std::string bytes;
        if(LocalI18n::selection.global.empty()||!LocalI18n::Read(LocalI18n::selection.global,bytes,32*1024*1024))return std::unordered_map<std::string,std::string>{};
        std::istringstream input(bytes);return ReadLocalizedListNames(input);
    }();
    auto key=Narrow(cls);for(char& c:key)c=(char)std::tolower((unsigned char)c);
    auto found=names.find(key);if(found!=names.end())return LocalI18n::Wide(found->second);

    const size_t split=cls.find(L'_');
    const wstring prefix=cls.substr(0,split);
    struct Manufacturer { const wchar_t* code;const wchar_t* name; };
    static const Manufacturer manufacturers[]={
        {L"AEGS",L"Aegis"},{L"ANVL",L"Anvil"},{L"ARGO",L"Argo"},
        {L"BANU",L"Banu"},{L"CNOU",L"Consolidated Outland"},
        {L"CRUS",L"Crusader"},{L"DRAK",L"Drake"},{L"ESPR",L"Esperia"},
        {L"GAMA",L"Gatac"},{L"GRIN",L"Greycat"},{L"KRIG",L"Kruger"},
        {L"MISC",L"MISC"},{L"MRAI",L"Mirai"},{L"ORIG",L"Origin"},
        {L"RSI",L"RSI"},{L"TMBL",L"Tumbril"},{L"VNCL",L"Vanduul"},
        {L"XIAN",L"Aopoa"}};
    wstring label=cls;
    for(const auto& m:manufacturers)if(!_wcsicmp(prefix.c_str(),m.code) && split!=wstring::npos) {
        label=wstring(m.name)+L" "+cls.substr(split+1);break;
    }
    size_t variant=wstring::npos;
    for(auto token:{L"_PU_",L"_AI_",L"_Unmanned_",L"_Template",L"_TEMP_"})
        variant=(std::min)(variant,label.find(token));
    if(variant!=wstring::npos)label.replace(variant,1,L" — ");
    // Keep every variant token: two classes must never silently become one choice.
    std::replace(label.begin(),label.end(),L'_',L' ');
    return label;
}
static const FlightProfileSearch::Ship* ResolveProfileShip(const FlightProfileSearch& search,const wstring& text) {
    for(const auto& ship:search.ships)if(ship.cls==text)return &ship;
    const FlightProfileSearch::Ship* found=nullptr;
    for(const auto& ship:search.ships)if(ship.label==text) {
        if(found)return nullptr; // ambiguous display label must not select the wrong class
        found=&ship;
    }
    return found;
}
static bool ProfileShipMatches(const wstring& name,const wstring& query) {
    wstring foldedName=name,foldedQuery=query;
    for(auto& c:foldedName)c=(wchar_t)towlower(c);
    for(auto& c:foldedQuery)c=(wchar_t)towlower(c);
    return foldedName.find(foldedQuery)!=wstring::npos;
}
static bool SelfTestProfileShipNames() {
    FlightProfileSearch places;
    ParseProfilePlaces("Stanton | Hurston | OOC_Stanton_1_Hurston | 1000000\nPyro | Monox | pyro2 | 0\nNyx | interior | rs_int_test | 0\nStanton | duplicate | OOC_Stanton_1_Hurston | 0\nSolarSystem_9 | stale | unknown | 0\n",places);
    if(places.places.size()!=2 || places.places[0].cls!=L"OOC_Stanton_1_Hurston" || places.places[1].cls!=L"pyro2")return false;
    ParseProfilePlaces("Stanton | Grim HEX | local_station_grimhex | 0\nStanton | Seraphim | local_station_seraphim | 0\nStanton | Everus Harbor | local_station_everus | 0\nStanton | Baijini Point | local_station_baijini | 0\nStanton | Port Tressler | local_station_tressler | 0\n",places);
    if(places.places.size()!=7)return false;
    FlightProfileSearch search;
    for(auto cls:{L"AEGS_Gladius",L"DRAK_Cutlass_Black",L"AEGS_Gladius_PU_AI_CIV"})
        search.ships.push_back({cls,ProfileShipLabel(cls)});
    const auto selected=ResolveProfileShip(search,L"Drake Cutlass Black");
    return !ProfileShipLabel(L"AEGS_Gladius").empty() &&
        selected && selected->cls==L"DRAK_Cutlass_Black" &&
        ResolveProfileShip(search,L"AEGS_Gladius") && !ResolveProfileShip(search,L"gladius") &&
        ProfileShipLabel(L"AEGS_Gladius_PU_AI_CIV")!=ProfileShipLabel(L"AEGS_Gladius") &&
        ProfileShipMatches(search.ships[1].label,L"CUTLASS BLACK");
}
static void FilterProfileShips(HWND wnd,FlightProfileSearch& search) {
    if(search.updating)return;
    search.updating=true;
    const HWND combo=GetDlgItem(wnd,pfShip);
    wchar_t text[256]={};GetWindowTextW(combo,text,256);
    DWORD begin=0,end=0;SendMessageW(combo,CB_GETEDITSEL,(WPARAM)&begin,(LPARAM)&end);
    SendMessageW(combo,WM_SETREDRAW,FALSE,0);
    SendMessageW(combo,CB_RESETCONTENT,0,0);
    size_t matches=0;
    for(const auto& ship:search.ships)if(ProfileShipMatches(ship.label,text)||ProfileShipMatches(ship.cls,text)) {
        SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)ship.label.c_str());++matches;
    }
    SendMessageW(combo,CB_SHOWDROPDOWN,matches?TRUE:FALSE,0);
    SetWindowTextW(combo,text);
    SendMessageW(combo,CB_SETEDITSEL,0,MAKELPARAM(begin,end));
    SendMessageW(combo,WM_SETREDRAW,TRUE,0);InvalidateRect(combo,nullptr,TRUE);
    const wstring count=matches?LocalI18n::W("launcher.ships_found.1372fb3d90")+std::to_wstring(matches):LocalI18n::W("launcher.no_matches_change_or_clear_the_search.af209e7303");
    SetWindowTextW(GetDlgItem(wnd,pfMatches),count.c_str());
    search.updating=false;
}
static void FilterProfileStarts(HWND wnd,FlightProfileSearch& search) {
    if(search.updating)return;
    search.updating=true;
    HWND combo=GetDlgItem(wnd,pfStart);
    wchar_t text[256]={};GetWindowTextW(combo,text,256);
    DWORD begin=0,end=0;SendMessageW(combo,CB_GETEDITSEL,(WPARAM)&begin,(LPARAM)&end);
    SendMessageW(combo,WM_SETREDRAW,FALSE,0);SendMessageW(combo,CB_RESETCONTENT,0,0);
    size_t matches=0;
    for(const auto& label:search.starts)if(ProfileShipMatches(label,text)) {SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)label.c_str());++matches;}
    SendMessageW(combo,CB_SHOWDROPDOWN,matches?TRUE:FALSE,0);SetWindowTextW(combo,text);
    SendMessageW(combo,CB_SETEDITSEL,0,MAKELPARAM(begin,end));SendMessageW(combo,WM_SETREDRAW,TRUE,0);InvalidateRect(combo,nullptr,TRUE);
    search.updating=false;
}
static LRESULT CALLBACK FlightProfileProc(HWND wnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_CREATE) {
        auto search=new FlightProfileSearch;
        SetWindowLongPtrW(wnd,GWLP_USERDATA,(LONG_PTR)search);
        auto control=[&](const wchar_t* cls,const wchar_t* text,DWORD style,int id,int x,int y,int w,int h){
            auto c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,wnd,(HMENU)(INT_PTR)id,nullptr,nullptr);
            SendMessageW(c,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);return c;
        };
        Config cfg;FILE* old=g_log;g_log=nullptr;ReadConfig(g_gui.here+L"\\sc-offline.ini",cfg);g_log=old;
        control(L"STATIC",LocalI18n::W("launcher.flight_start_search_by_part_of_the_name.2f4c8c744c"),0,0,18,18,540,24);
        auto start=control(L"COMBOBOX",L"",CBS_DROPDOWN|WS_TABSTOP|WS_VSCROLL,pfStart,18,46,540,320);
        for(auto s:{LocalI18n::W("launcher.normal_spawn_location.5d038e2fd0"),LocalI18n::W("launcher.above_daymar_aboard_the_selected_ship.b8783dd702"),LocalI18n::W("launcher.continue_saved_session.e1101b310b")})SendMessageW(start,CB_ADDSTRING,0,(LPARAM)s);
        int initialStart=cfg.resume?2:!_wcsicmp(cfg.start.c_str(),L"Daymar")?1:0;
        for (auto file:{L"locations.txt", L"locations_found.txt"}) {
            std::string catalog;
            if (!ReadAll(g_gui.here+L"\\data\\"+file,catalog)) continue;
            ParseProfilePlaces(catalog,*search);
        }
        std::sort(search->places.begin(),search->places.end(),[](const auto& a,const auto& b){return a.label<b.label;});
        for(size_t i=0;i<search->places.size();++i) {
            SendMessageW(start,CB_ADDSTRING,0,(LPARAM)search->places[i].label.c_str());
            if(cfg.start==L"place:"+search->places[i].cls)initialStart=3+(int)i;
        }
        for(int i=0;i<(int)SendMessageW(start,CB_GETCOUNT,0,0);++i) {
            wchar_t label[256]={}; SendMessageW(start,CB_GETLBTEXT,i,(LPARAM)label);search->starts.push_back(label);
        }
        SendMessageW(start,CB_SETCURSEL,initialStart,0);
        control(L"STATIC",LocalI18n::W("launcher.starting_ship_at_the_selected_destination.d8a198ff75"),0,0,18,88,540,24);
        auto ship=control(L"COMBOBOX",L"",CBS_DROPDOWN|WS_TABSTOP|WS_VSCROLL,pfShip,18,116,540,320);
        std::string text;if(ReadAll(g_gui.here+L"\\data\\ships.txt",text)) {
            size_t from=0;while(from<text.size()){auto end=text.find('\n',from);auto row=text.substr(from,end==std::string::npos?std::string::npos:end-from);
                if(!row.empty() && row.back()=='\r')row.pop_back();if(!row.empty()&&row[0]!='#'){std::wstring w(row.begin(),row.end());search->ships.push_back({w,ProfileShipLabel(w)});}
                if(end==std::string::npos)break;from=end+1;
            }
        }
        // Disambiguate collisions before displaying any choices.
        std::vector<wstring> labels;
        for(const auto& entry:search->ships)labels.push_back(entry.label);
        for(auto& entry:search->ships) {
            if(std::count(labels.begin(),labels.end(),entry.label)>1)entry.label+=L" ["+entry.cls+L"]";
            SendMessageW(ship,CB_ADDSTRING,0,(LPARAM)entry.label.c_str());
        }
        const auto initial=ResolveProfileShip(*search,cfg.startShip);
        SetWindowTextW(ship,initial?initial->label.c_str():cfg.startShip.c_str());
        control(L"STATIC",LocalI18n::W("launcher.search_by_part_of_the_name_type_and_select_a_.350f82338c"),0,pfMatches,18,150,540,24);
        auto check=control(L"BUTTON",LocalI18n::W("launcher.autosave_once_a_minute.217f6435ad"),BS_AUTOCHECKBOX|WS_TABSTOP,pfAutosave,18,180,540,26);
        SendMessageW(check,BM_SETCHECK,cfg.autosave?BST_CHECKED:BST_UNCHECKED,0);
        control(L"STATIC",LocalI18n::W("launcher.continue_uses_the_saved_location_and_ship_cla.c564f5e695"),0,0,18,218,540,85);
        control(L"BUTTON",LocalI18n::W("launcher.save_profile.0c8209e72e"),BS_DEFPUSHBUTTON|WS_TABSTOP,pfSave,18,304,240,34);
        control(L"BUTTON",LocalI18n::W("launcher.cancel.19766ed6cc"),BS_PUSHBUTTON|WS_TABSTOP,pfCancel,278,304,180,34);
        wchar_t playerName[49] = {};
        GetPrivateProfileStringW(L"player",L"display_name",L"default_1",playerName,49,(g_gui.here+L"\\data\\player-profile.ini").c_str());
        control(L"STATIC",LocalI18n::W("launcher.mod_menu_name.500cd2c70f"),0,0,18,350,170,24);
        auto nameEdit=control(L"EDIT",playerName,WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,pfPlayerName,188,348,200,26);
        SendMessageW(nameEdit,EM_SETLIMITTEXT,48,0);
        control(L"BUTTON",LocalI18n::W("launcher.save_name.b7297226fd"),BS_PUSHBUTTON|WS_TABSTOP,pfSaveName,398,346,160,30);
        control(L"STATIC",LocalI18n::W("launcher.language.ui"),0,0,18,389,175,24);
        auto ui=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,pfUiLanguage,198,385,360,220);
        auto addUi=[&](const std::string& code,const wstring& label){search->uiCodes.push_back(code);SendMessageW(ui,CB_ADDSTRING,0,(LPARAM)label.c_str());};
        addUi("auto",LocalI18n::W("launcher.language.auto"));addUi("en",L"English");addUi("ru",L"Русский");
        std::error_code ec;
        for(std::filesystem::directory_iterator i(std::filesystem::path(g_gui.here)/"localization/launcher",ec),end;!ec&&i!=end;i.increment(ec)) {
            auto code=i->path().stem().string();if(i->path().extension()==".ini"&&LocalI18n::SafeCode(code)&&std::find(search->uiCodes.begin(),search->uiCodes.end(),code)==search->uiCodes.end())addUi(code,LocalI18n::Wide(code));
        }
        int selectedUi=0;for(size_t i=0;i<search->uiCodes.size();++i)if(search->uiCodes[i]==Narrow(cfg.language))selectedUi=(int)i;
        SendMessageW(ui,CB_SETCURSEL,selectedUi,0);
        control(L"STATIC",LocalI18n::W("launcher.language.game"),0,0,18,431,175,24);
        auto game=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,pfGameLanguage,198,427,360,220);
        search->gameSlots.push_back("auto");SendMessageW(game,CB_ADDSTRING,0,(LPARAM)LocalI18n::W("launcher.language.auto"));
        int selectedGame=0;
        for(const auto& language:LocalI18n::selection.available){search->gameSlots.push_back(language.slot);auto label=LocalI18n::Wide(language.label+" ["+language.slot+"]");SendMessageW(game,CB_ADDSTRING,0,(LPARAM)label.c_str());if(language.slot==Narrow(cfg.gameLanguage))selectedGame=(int)search->gameSlots.size()-1;}
        SendMessageW(game,CB_SETCURSEL,selectedGame,0);
        control(L"BUTTON",LocalI18n::W("launcher.language.save"),BS_PUSHBUTTON|WS_TABSTOP,pfSaveLanguage,198,470,360,30);
        control(L"STATIC",LocalI18n::W("launcher.language.catalog_hint"),0,0,18,510,540,42);

        return 0;
    }

    if(msg==WM_COMMAND && LOWORD(wp)==pfSaveLanguage) {
        auto search=(FlightProfileSearch*)GetWindowLongPtrW(wnd,GWLP_USERDATA);
        auto ui=SendMessageW(GetDlgItem(wnd,pfUiLanguage),CB_GETCURSEL,0,0),game=SendMessageW(GetDlgItem(wnd,pfGameLanguage),CB_GETCURSEL,0,0);
        if(!search||ui<0||game<0||(size_t)ui>=search->uiCodes.size()||(size_t)game>=search->gameSlots.size())return 0;
        auto ini=g_gui.here+L"\\sc-offline.ini";std::string original;bool have=ReadAll(ini,original);
        bool ok=SetIniValue(ini,"language",search->uiCodes[ui].c_str())&&SetIniValue(ini,"game_language",search->gameSlots[game].c_str());
        if(!ok&&have)WriteAll(ini,original);
        MessageBoxW(wnd,LocalI18n::W(ok?"launcher.language.restart":"launcher.language.failed"),LocalI18n::W("launcher.language.ui"),MB_OK|(ok?MB_ICONINFORMATION:MB_ICONWARNING));return 0;
    }
    if(msg==WM_COMMAND && LOWORD(wp)==pfSaveName) {
        wchar_t name[49]={};GetWindowTextW(GetDlgItem(wnd,pfPlayerName),name,49);
        if(!name[0] || wcspbrk(name,L"\r\n=;[]\"")) { MessageBoxW(wnd,LocalI18n::W("launcher.enter_a_name_without_control_characters.12960e0910"),LocalI18n::W("launcher.player_name.8a6f25d4bf"),MB_OK);return 0; }
        if(!WritePrivateProfileStringW(L"player",L"display_name",name,(g_gui.here+L"\\data\\player-profile.ini").c_str())) {
            MessageBoxW(wnd,LocalI18n::W("launcher.could_not_save_the_name.9f9ecb8283"),LocalI18n::W("launcher.player_name.8a6f25d4bf"),MB_OK);return 0;
        }
        AppendOutput(LocalI18n::W("launcher.mod_menu_name_saved_applies_on_the_next_game_.0bd414015b"));return 0;
    }
    if(msg==WM_COMMAND && LOWORD(wp)==pfStart && HIWORD(wp)==CBN_EDITCHANGE) {
        auto search=(FlightProfileSearch*)GetWindowLongPtrW(wnd,GWLP_USERDATA);
        if(search)FilterProfileStarts(wnd,*search);return 0;
    }
    if(msg==WM_COMMAND && LOWORD(wp)==pfShip && HIWORD(wp)==CBN_EDITCHANGE) {
        auto search=(FlightProfileSearch*)GetWindowLongPtrW(wnd,GWLP_USERDATA);
        if(search)FilterProfileShips(wnd,*search);
        return 0;
    }
    if(msg==WM_COMMAND && LOWORD(wp)==pfSave) {
        wchar_t ship[256];GetWindowTextW(GetDlgItem(wnd,pfShip),ship,256);
        wchar_t startText[256]={};GetWindowTextW(GetDlgItem(wnd,pfStart),startText,256);
        int choice=-1;
        auto search=(FlightProfileSearch*)GetWindowLongPtrW(wnd,GWLP_USERDATA);
        const auto selected=search?ResolveProfileShip(*search,ship):nullptr;
        if(search && !search->ships.empty() && !selected) {
            MessageBoxW(wnd,LocalI18n::W("launcher.the_field_still_contains_a_search_query_selec.4e45c27606"),LocalI18n::W("launcher.flight_profile.8afb7c386b"),MB_OK);return 0;
        }
        const std::string cls=Narrow(selected?selected->cls:wstring(ship));
        if(cls.empty() || cls.size()>63 || cls.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos){MessageBoxW(wnd,LocalI18n::W("launcher.select_a_ship_from_the_list.b890354997"),LocalI18n::W("launcher.flight_profile.8afb7c386b"),MB_OK);return 0;}
        if(search)for(size_t i=0;i<search->starts.size();++i)if(search->starts[i]==startText)choice=(int)i;
        if(choice<0) {MessageBoxW(wnd,LocalI18n::W("launcher.select_a_full_destination_from_the_list_searc.721ec47ae9"),LocalI18n::W("launcher.flight_start.44dbed21a1"),MB_OK);return 0;}
        if(choice>=3 && (!search || size_t(choice-3)>=search->places.size()))return 0;
        const std::string startValue=choice>=3?"place:"+Narrow(search->places[choice-3].cls):choice==1?"Daymar":choice==2?"resume":"normal";
        const wstring ini=g_gui.here+L"\\sc-offline.ini";
        // Snapshot first: roll back all settings if any write fails.
        std::string original;const bool have=ReadAll(ini,original);
        const bool ok=SetIniValue(ini,"start_ship",cls.c_str()) && SetIniValue(ini,"start",startValue.c_str()) &&
            (choice<3 || SetIniValue(ini,"boot_map","PU_All")) && SetIniValue(ini,"resume",choice==2?"on":"off") && SetIniValue(ini,"autosave",SendMessageW(GetDlgItem(wnd,pfAutosave),BM_GETCHECK,0,0)==BST_CHECKED?"on":"off");
        if(!ok){if(have)WriteAll(ini,original);MessageBoxW(wnd,LocalI18n::W("launcher.could_not_save_the_profile_check_access_to_sc.70b18ce0bc"),LocalI18n::W("launcher.flight_profile.8afb7c386b"),MB_OK);return 0;}
        AppendOutput(LocalI18n::W("launcher.flight_profile_saved_press_play_to_start.3fe5a27d1d"));DestroyWindow(wnd);return 0;
    }
    if((msg==WM_COMMAND && LOWORD(wp)==pfCancel)||msg==WM_CLOSE){DestroyWindow(wnd);return 0;}
    if(msg==WM_NCDESTROY) {
        delete (FlightProfileSearch*)GetWindowLongPtrW(wnd,GWLP_USERDATA);
        SetWindowLongPtrW(wnd,GWLP_USERDATA,0);
    }
    return DefWindowProcW(wnd,msg,wp,lp);
}
static void ShowFlightProfile(HWND parent) {
    static bool registered=false;if(!registered){WNDCLASSW c{};c.lpfnWndProc=FlightProfileProc;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"sc-offline-flight-profile";c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);registered=RegisterClassW(&c)!=0;}
    if(!registered)return;
    auto wnd=CreateWindowExW(WS_EX_DLGMODALFRAME,L"sc-offline-flight-profile",LocalI18n::W("launcher.free_flight_profile.bf4ec49956"),WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,590,600,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!wnd)return;EnableWindow(parent,FALSE);MSG m;
    while(IsWindow(wnd)&&GetMessageW(&m,nullptr,0,0)>0){if(!IsDialogMessageW(wnd,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}
    EnableWindow(parent,TRUE);SetForegroundWindow(parent);
}
