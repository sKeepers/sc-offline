// Independent Codex tests of the integrated pure parser/XML; no game and no received test scripts.
#include "../src/gear_attachments.h"
#include "../src/list_label.h"
#include <fstream>
#include <iostream>
#include <memory>
static int checked=0,failed=0;
static void Check(bool ok,const char* what){++checked;if(!ok){++failed;std::cout<<"FAIL "<<what<<'\n';}}
static void Line(gear::Catalog& c,const std::string& text){std::string writable=text;gear::ParseLine(c,writable.data(),[](const char* name){return std::string(name).find("missing_")!=0;});}
static void File(gear::Catalog& c,const char* file){gear::Begin(c);std::ifstream input(file);std::string s;while(std::getline(input,s))Line(c,s);}
static bool Has(const std::string& xml,const std::string& value){return xml.find(value)!=xml.npos;}
int main(int argc,char** argv){
 if(argc!=3)return 2;
 auto old=std::make_unique<gear::Catalog>(),full=std::make_unique<gear::Catalog>();
 File(*old,argv[1]);File(*full,argv[2]);
 for(int i=0;i<Gear_SlotCount;++i){
  Check(old->gearCount[i]==full->gearCount[i],"old gear count preserved");
  for(int j=0;j<old->gearCount[i];++j)Check(std::string(gear::GearName(*old,i,j))==gear::GearName(*full,i,j),"old class and order preserved");
 }
 const int counts[]={53,35,10,5,2,1};for(int i=0;i<gear::Extra_Count;++i)Check(full->extraCount[i]==counts[i],"106 extra class counts");
 auto c=std::make_unique<gear::Catalog>();gear::Begin(*c);
 Line(*c,"[optic]");Line(*c,std::string(64,'x'));Line(*c,std::string(63,'y'));Line(*c,"bad\"class");Line(*c,"missing_optic");
 Check(c->extraCount[gear::Extra_Optic]==1&&c->rejected==2&&c->unknown==1,"long/malformed/unavailable extra IDs rejected");
 Check(std::string(gear::ExtraName(*c,gear::Extra_Optic,0))==std::string(63,'y'),"63-byte exact ID retained");
 gear::Begin(*c);
 for(const auto* s:{"[primary]","rifle_01","[sidearm]","pistol_01","grin_multitool_01","grin_multitool_01_default_salvage_repair","[ammo]","rifle_01_mag","pistol_01_mag","[optic]","optic_demo","[barrel]","barrel_demo","[underbarrel]","under_demo","[toolmodule]","grin_multitool_01_mining","grin_multitool_01_salvage_repair","[toolcanister]","grin_multitool_resource_salvage_repair_01_filled","[toolbattery]","grin_multitool_energy_01_mag"})Line(*c,s);
 int picks[Gear_SlotCount];for(auto& i:picks)i=-1;MenuWeaponAttachments a;
 const std::string emptyExpected="<Loadout><Items><Item portName=\"Body_ItemPort\" itemName=\"body_01\"><Items><Item portName=\"Armor_Undersuit\" itemName=\"rsi_odyssey_undersuit_01_01_01\"><Items><Item portName=\"medPen_attach_1\" itemName=\"crlf_consumable_healing_01\"/></Items></Item><head/></Items></Item></Items></Loadout>\n";
 Check(gear::GearXml(*c,picks,a,"<head/>")==emptyExpected,"legacy empty XML matches literal golden document");
 picks[Gear_Primary]=0;picks[Gear_Sidearm]=0;
 const auto plain=gear::GearXml(*c,picks,a,"<head/>");
 Check(Has(plain,"rifle_01_mag")&&Has(plain,"pistol_01_mag")&&!Has(plain,"WeaponAttachment"),"default magazines unchanged without extra choices");
 a.primary[0]=0;a.primary[1]=0;a.primary[2]=0;a.sidearm[0]=0;
 auto xml=gear::GearXml(*c,picks,a,"<head/>");
 Check(Has(xml,"portName=\"optics_attach\" itemName=\"optic_demo\" tag=\"WeaponAttachment(IronSight)\""),"optic port/tag retained");
 Check(Has(xml,"portName=\"barrel_attach\" itemName=\"barrel_demo\" tag=\"WeaponAttachment(Barrel)\""),"barrel port/tag retained");
 Check(Has(xml,"portName=\"underbarrel_attach\" itemName=\"under_demo\" tag=\"WeaponAttachment(BottomAttachment)\""),"underbarrel port/tag retained");
 a={};Check(gear::GearXml(*c,picks,a,"<head/>")==plain,"clear returns to exact legacy XML");
 picks[Gear_Primary]=-1;picks[Gear_Sidearm]=2;
 const auto defaultSrt=gear::GearXml(*c,picks,a,"");
 Check(Has(defaultSrt,"wep_sidearm")&&Has(defaultSrt,"default_salvage_repair")&&!Has(defaultSrt,"utility_attach_1"),"default SRT unchanged");
 a.sidearmToolModule=1;Check(gear::GearXml(*c,picks,a,"")==defaultSrt,"explicit default SRT module normalizes to legacy");
 a.sidearmToolCanister=0;xml=gear::GearXml(*c,picks,a,"");
 Check(Has(xml,"utility_attach_1")&&Has(xml,"canister_attach")&&Has(xml,"WeaponAttachment(Magazine)")&&!Has(xml,"default_salvage_repair"),"explicit SRT cartridge uses base tool and cartridge tag");
 Check(Has(xml,"grin_multitool_energy_01_mag")&&Has(xml,"WeaponAttachment(Utility)"),"module and battery retained");
 a.sidearmToolModule=0;xml=gear::GearXml(*c,picks,a,"");Check(Has(xml,"grin_multitool_01_mining")&&!Has(xml,"canister_attach"),"non-SRT module discards cartridge");
 a={};a.primary[0]=999;a.sidearm[0]=0;Check(gear::GearXml(*c,picks,a,"")==defaultSrt,"stale/missing/incompatible hand choices ignored");
 std::string last="pistol_01";a.primary[0]=0;a.sidearm[0]=0;
 Check(gear::ResetOnWeaponChange(last,"rifle_01",a,true)&&a.primary[0]==-1&&a.sidearm[0]==0,"weapon change resets only its hand");
 Check(!gear::ResetOnWeaponChange(last,"rifle_01",a,true),"same weapon retains choices");
 Check(gear::IsLoadoutXmlPayload(xml.data(),xml.size()),"new preset payload accepted");
 Check(gear::IsLoadoutXmlPayload(defaultSrt.data(),defaultSrt.size()),"old preset payload accepted");
 Check(!gear::IsLoadoutXmlPayload(nullptr,0)&&!gear::IsLoadoutXmlPayload(xml.data(),gear::kMaxLoadoutXml+1),"invalid/oversized preset payload rejected");
 Check(gear::IsLoadoutXmlPayload(xml.data(),xml.size())&&std::string(xml.data(),xml.size())==xml,"preset byte roundtrip preserves attachment XML");
 Check(std::string(gear::EnglishAlias("grin_multitool_01_mining"))=="OreBit Mining Attachment","module search alias available");
 std::cout<<checked<<" independent parser/XML/catalog checks, "<<failed<<" failures\n";return failed?1:0;
}
