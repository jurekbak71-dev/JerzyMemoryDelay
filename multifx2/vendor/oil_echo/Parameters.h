#pragma once
#include <array>
#include <string>
#include <vector>
namespace jerzy {
struct Param { const char* id; const char* name; float min,max,def; int tab; const char* choices=""; };
inline const std::vector<Param>& parameters() {
 static const std::vector<Param> p = {
 {"master","Master Only",0,1,0,0}, {"time","Rotation / Time ms",40,8000,480,0},
 {"sync","BPM Sync",0,1,0,0}, {"division","Sync division",0,7,3,0,"1/16|1/8 triplet|1/8|1/4|1/4 dotted|1/2|1 bar|2 bars"},
 {"extended","Time range",0,1,1,0,"Vintage|Extended"}, {"heads","Head count",0,1,1,0,"2 HEADS|4 HEADS"},
 {"feedback","Feedback",0,1.12f,.48f,0}, {"dry","Dry Level",0,1.5f,1,0}, {"wet","Wet Level",0,1.5f,.6f,0},
 {"focus","Echo Focus",0,1,.35f,0}, {"duck","Dynamic Ducking",0,1,.1f,0},
 {"stereo","Stereo",0,2,1,0,"Classic|Wide|Ping Pong"}, {"mono","Mono compatibility",0,1,.3f,0},
 {"h1on","Head 1 On",0,1,1,1},{"h1level","Head 1 Level",0,1,.7f,1},{"h1pan","Head 1 Pan",-1,1,-.6f,1},{"h1send","Head 1 Feedback Send",0,1,.5f,1},
 {"h2on","Head 2 On",0,1,1,1},{"h2level","Head 2 Level",0,1,.6f,1},{"h2pan","Head 2 Pan",-1,1,.6f,1},{"h2send","Head 2 Feedback Send",0,1,.6f,1},
 {"h3on","Head 3 On",0,1,1,1},{"h3level","Head 3 Level",0,1,.45f,1},{"h3pan","Head 3 Pan",-1,1,-.3f,1},{"h3send","Head 3 Feedback Send",0,1,.7f,1},
 {"h4on","Head 4 On",0,1,1,1},{"h4level","Head 4 Level",0,1,.5f,1},{"h4pan","Head 4 Pan",-1,1,.3f,1},{"h4send","Head 4 Feedback Send",0,1,.8f,1},
 {"age","Age",0,1,.3f,2},{"oil","Oil",0,1,.45f,2},{"motor","Motor",0,1,.2f,2},{"contact","Contact",0,1,.2f,2},
 {"decay","Charge Decay",0,1,.25f,2},{"noise","Noise",0,1,0,2},{"drift","Drift",0,1,.15f,2},{"wow","Wow",0,1,.15f,2},{"flutter","Flutter",0,1,.08f,2},
 {"mod","Modulation",0,3,0,3,"Off|Chorus|Ensemble|Vibrato"},{"route","Routing",0,1,0,3,"Wet Only|Dry Only"},
 {"speed","Speed Hz",.05f,6,.35f,3},{"depth","Depth",0,1,.35f,3},{"voices","Voices",2,6,4,3},
 {"width","Width",0,1,.8f,3},{"modtone","Tone",0,1,.6f,3},{"modmix","Mix",0,1,.4f,3},
 {"inbypass","Preamp bypass",0,1,0,4},{"drive","Input Drive dB",0,24,0,4},{"insat","Input Saturation",0,1,.1f,4},{"intone","Input Tone",0,1,.6f,4},
 {"icmode","Input Compressor",0,2,0,4,"Off|Vintage Smooth|Studio Punch"},
 {"icthreshold","Input Threshold dB",-48,0,-18,4},{"icratio","Input Ratio",1,12,3,4},{"icattack","Input Attack ms",.2f,100,15,4},{"icrelease","Input Release ms",20,1200,180,4},{"icknee","Input Knee dB",0,18,6,4},{"icmakeup","Input Makeup dB",0,24,0,4},
 {"outbypass","Power amp bypass",0,1,0,4},{"power","Power Drive dB",0,24,0,4},{"distortion","Distortion",0,1,0,4},{"outsat","Output Saturation",0,1,.1f,4},{"outtone","Output Tone",0,1,.6f,4},{"body","Body",0,1,.5f,4},{"sag","Sag",0,1,0,4},{"output","Output Level dB",-36,12,0,4},
 {"ocmode","Output Compressor",0,2,0,4,"Off|Vintage Smooth|Studio Punch"},
 {"octhreshold","Output Threshold dB",-48,0,-12,4},{"ocratio","Output Ratio",1,12,2,4},{"ocattack","Output Attack ms",.2f,100,25,4},{"ocrelease","Output Release ms",20,1200,250,4},{"ocknee","Output Knee dB",0,18,6,4},{"ocmakeup","Output Makeup dB",0,24,0,4},
 {"mechanicson","Mechanics",0,1,1,2},{"mechmix","Mechanics Amount",0,1,.7f,2},
 {"mechroute","Mechanics Routing",0,2,0,2,"Echo Only|Dry / Master|Both"},
 {"space","Echo Space",0,1,.45f,0},
 {"springon","Spring Tank",0,1,0,5},{"springmix","Spring Mix",0,1,.25f,5},
 {"springdecay","Spring Decay s",.4f,6,2.2f,5},{"springtone","Spring Tone",0,1,.55f,5}
 }; return p;
}
inline int index(const std::string& id) { for(size_t i=0;i<parameters().size();++i) if(id==parameters()[i].id) return int(i); return -1; }
namespace key { enum Id { master,time,sync,division,extended,heads,feedback,dry,wet,focus,duck,stereo,mono,h1on,h1level,h1pan,h1send,h2on,h2level,h2pan,h2send,h3on,h3level,h3pan,h3send,h4on,h4level,h4pan,h4send,age,oil,motor,contact,decay,noise,drift,wow,flutter,mod,route,speed,depth,voices,width,modtone,modmix,inbypass,drive,insat,intone,icmode,icthreshold,icratio,icattack,icrelease,icknee,icmakeup,outbypass,power,distortion,outsat,outtone,body,sag,output,ocmode,octhreshold,ocratio,ocattack,ocrelease,ocknee,ocmakeup,mechanicson,mechmix,mechroute,space,springon,springmix,springdecay,springtone, count }; }
struct Settings {
 std::array<float,96> v{};
 Settings() { for(size_t i=0;i<parameters().size();++i) v[i]=parameters()[i].def; }
 float get(const char* id) const { return v[size_t(index(id))]; }
 void set(const char* id,float value) { auto i=index(id); if(i>=0) {v[size_t(i)]=value;
 if(i==key::heads){v[key::h3on]=v[key::h4on]=value>.5f?1.f:0.f;}
 if((i==key::h3on||i==key::h4on)&&value>.5f)v[key::heads]=1;
 } }
};
struct Preset { const char* name; Settings values; };
inline const std::vector<Preset>& presets() {
 static const std::vector<Preset> all=[] {
 std::vector<Preset> p;
 auto add=[&](const char* name,std::initializer_list<std::pair<const char*,float>> edits){ Settings s; for(auto& e:edits)s.set(e.first,e.second); p.push_back({name,s}); };
 add("Vintage / Warm Can",{{"time",380},{"age",.5f},{"oil",.65f},{"heads",1},{"wet",.75f},{"space",.55f}});
 add("Vintage / Short Room",{{"time",125},{"feedback",.2f},{"wet",.35f},{"age",.65f}});
 add("Dub / Oil Trails",{{"time",680},{"feedback",.82f},{"focus",.75f},{"duck",.3f},{"stereo",2}});
 add("Dub / Dotted Charge",{{"sync",1},{"division",4},{"feedback",.7f},{"h1on",0},{"h3on",0},{"oil",.7f}});
 add("Guitar / Slap",{{"time",110},{"heads",0},{"h1on",0},{"feedback",.12f},{"wet",.45f},{"drive",3}});
 add("Guitar / Singing Lead",{{"time",510},{"duck",.55f},{"feedback",.6f},{"focus",.6f},{"wet",.7f}});
 add("Rhythmic / Four Heads",{{"sync",1},{"division",5},{"feedback",.4f},{"wow",.05f},{"flutter",.02f}});
 add("Rhythmic / Stereo Steps",{{"sync",1},{"division",3},{"stereo",2},{"feedback",.6f},{"h2on",0}});
 add("Ambient / Long Oil",{{"time",5800},{"feedback",.86f},{"wet",.9f},{"decay",.08f},{"mod",2},{"modmix",.5f},{"duck",.4f}});
 add("Ambient / Slow Orbit",{{"time",3200},{"feedback",.8f},{"wow",.4f},{"drift",.4f},{"oil",.7f},{"wet",.85f}});
 add("Experimental / Charged Edge",{{"time",920},{"feedback",1.05f},{"contact",.65f},{"motor",.7f},{"wet",.5f}});
 add("Experimental / Unsteady Motor",{{"time",1400},{"wow",.8f},{"flutter",.3f},{"drift",.8f},{"motor",.8f},{"feedback",.65f}});
 add("Chorus / Warm Guitar",{{"master",1},{"mechmix",0},{"mod",1},{"route",1},{"depth",.4f},{"modmix",.45f},{"speed",.7f}});
 add("Ensemble / Wide Strings",{{"master",1},{"mechmix",0},{"mod",2},{"route",1},{"voices",6},{"modmix",.65f},{"depth",.55f}});
 add("Vibrato / Slow Soul",{{"master",1},{"mechmix",0},{"mod",3},{"route",1},{"speed",.8f},{"depth",.3f},{"modmix",1}});
 add("Master Color / Gentle Glue",{{"master",1},{"drive",2},{"insat",.25f},{"icmode",1},{"icthreshold",-20},{"icratio",2},{"icmakeup",2},{"power",1},{"outsat",.2f}});
 add("Master Color / Drum Punch",{{"master",1},{"ocmode",2},{"octhreshold",-16},{"ocratio",4},{"ocattack",30},{"ocrelease",100},{"ocmakeup",3},{"body",.65f}});
 add("Mechanics / Worn Rotor",{{"master",1},{"mechmix",.85f},{"wow",.65f},{"drift",.6f},{"contact",.45f},{"oil",.6f}});
 add("Mechanics / Broken Contact",{{"master",1},{"mechmix",1},{"wow",.45f},{"flutter",.55f},{"contact",.9f},{"age",.7f},{"motor",.65f}});
 add("Spring / Surf Tank",{{"master",1},{"mechmix",0},{"springon",1},{"springmix",.45f},{"springdecay",2.8f},{"springtone",.7f}});
 add("Echo / Oil And Springs",{{"time",620},{"feedback",.65f},{"wet",.8f},{"space",.65f},{"springon",1},{"springmix",.28f},{"springdecay",3.5f}});
 return p; }(); return all;
}
}
