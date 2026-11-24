// Real ABI2 catalog vs ABI3 legacy adapter. No generated/fake module descriptors.
#include "plugin.hpp"
#include "json.hpp"
#define JfxParameter LegacyParameter
#define JfxDescriptor LegacyDescriptor
#define JfxEntryPoint LegacyEntryPoint
#include "../../multifx2/api.h"
#undef JfxParameter
#undef JfxDescriptor
#undef JfxEntryPoint
#include <algorithm>
#include <array>
#include <cmath>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr unsigned Frames=64, Blocks=384;
using Buffer=std::array<float,Frames>;
struct Guard {
    using Toggle=void(*)(int);
    using Reset=void(*)();
    using Count=unsigned long long(*)();
    using Observe=void(*)(void*);
    Toggle toggle=reinterpret_cast<Toggle>(dlsym(RTLD_DEFAULT,"jfx_test_rt"));
    Reset reset=reinterpret_cast<Reset>(dlsym(RTLD_DEFAULT,"jfx_test_reset"));
    Count allocations=reinterpret_cast<Count>(dlsym(RTLD_DEFAULT,"jfx_test_allocations"));
    Count frees=reinterpret_cast<Count>(dlsym(RTLD_DEFAULT,"jfx_test_frees"));
    Observe observe=reinterpret_cast<Observe>(dlsym(RTLD_DEFAULT,"jfx_test_observe"));
    bool available()const{return toggle&&reset&&allocations&&frees&&observe;}
    void start(){if(available())toggle(1);}
    void stop(){if(available())toggle(0);}
    void clear(){if(available())reset();}
    void calibrate(){
        if(!available())return;
        clear();start();
        auto* p=new int(7);observe(p);delete p;
        stop();
        if(!allocations()||!frees())throw std::runtime_error("allocation guard calibration failed");
        clear();
    }
};
void require(bool yes,const std::string& message){if(!yes)throw std::runtime_error(message);}
struct Legacy {
    void* library=nullptr;
    const LegacyDescriptor* descriptor=nullptr;
    explicit Legacy(const std::filesystem::path& path){
        library=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
        require(library!=nullptr,"cannot load actual ABI2 library: "+path.string());
        auto entry=reinterpret_cast<LegacyEntryPoint>(dlsym(library,"jfx_plugin"));
        require(entry!=nullptr,"missing ABI2 entry point");
        descriptor=entry();
        require(descriptor&&descriptor->abi_version==2&&descriptor->parameter_count<=48&&
                descriptor->create&&descriptor->destroy&&descriptor->process&&
                (!descriptor->parameter_count||descriptor->parameters),"invalid ABI2 descriptor");
    }
    ~Legacy(){if(library)dlclose(library);}
};
struct State {
    const LegacyDescriptor* d;
    void* pointer;
    explicit State(const LegacyDescriptor* descriptor):d(descriptor),pointer(d->create(44100)){
        require(pointer!=nullptr,"ABI2 instance creation failed");
    }
    ~State(){d->destroy(pointer);}
};
struct Result {
    std::vector<float> samples;
    double maximum_error=0,dsp_difference=0,parameter_difference=0;
    unsigned long long allocations=0,frees=0;
};
std::array<float,48> defaults(const LegacyDescriptor& d,const std::filesystem::path& manifest){
    std::ifstream file(manifest);
    require(bool(file),"missing actual manifest: "+manifest.string());
    nlohmann::json j;file>>j;
    require(j.at("key")==d.key&&j.at("params").is_array()&&j.at("params").size()==d.parameter_count,
            "manifest identity/count differs from ABI2 descriptor");
    std::array<float,48> p;p.fill(.5f);
    for(unsigned i=0;i<d.parameter_count;++i){
        const auto& m=j.at("params").at(i);
        const float v=m.at("def").get<float>();
        require(m.at("key")==d.parameters[i].key,"manifest parameter order differs from ABI2");
        require(std::isfinite(v)&&v>=0&&v<=1,"invalid manifest normalized default");
        require(std::abs(v-d.parameters[i].default_value)<=2e-5f,"manifest default differs from ABI2");
        p[i]=v;
    }
    return p;
}
void input(unsigned b,Buffer& l,Buffer& r){
    for(unsigned n=0;n<Frames;++n){
        const unsigned f=b*Frames+n;
        // Continuous asymmetric stereo plus transients/silent gaps exercises envelopes and delay history.
        const bool gap=(f%8192)>7000;
        l[n]=gap?0.f:float(.23*std::sin(f*.031)+.09*std::sin(f*.087)+(f%4093==0?.5:0));
        r[n]=gap?0.f:float(-.17*std::cos(f*.023)+.11*std::sin(f*.059));
    }
}
Result render(const std::filesystem::path& module,const std::filesystem::path& bridge_path,
              const std::array<float,48>& original,Guard& guard){
    Legacy old(module);
    State direct(old.descriptor),unchanged(old.descriptor);
    auto bridge=nextfx::Instance::create(nextfx::Library::open(bridge_path.string()),44100,Frames,module.string());
    require(bridge->descriptor().parameter_count==48,"legacy adapter must expose 48 normalized parameters");
    Result result;
    result.samples.resize(Blocks*Frames*2);
    Buffer il{},ir{},ol{},orr{};
    guard.clear();
    for(unsigned b=0;b<Blocks;++b){
        auto params=original;
        const unsigned phase=b/128;
        if(phase==1)
            for(unsigned p=0;p<old.descriptor->parameter_count;++p)
                params[p]=(p%2)? .83f:.17f;
        const double bpm=phase==1?173.:97.;
        input(b,il,ir);
        ol.fill(std::numeric_limits<float>::quiet_NaN());orr=ol;
        const float* ins[]{il.data(),ir.data()};float* outs[]{ol.data(),orr.data()};
        JfxBlock block{Frames,ins,outs,params.data(),nullptr,0,{bpm,0,1,1}};
        guard.start();bridge->process(block);guard.stop();
        for(unsigned n=0;n<Frames;++n){
            const float x[]{il[n],ir[n]};float expected[2]{},baseline[2]{};
            guard.start();
            old.descriptor->process(direct.pointer,x,expected,params.data(),bpm);
            old.descriptor->process(unchanged.pointer,x,baseline,original.data(),bpm);
            guard.stop();
            const float actual[]{ol[n],orr[n]};
            for(unsigned c=0;c<2;++c){
                require(std::isfinite(actual[c])&&std::isfinite(expected[c])&&std::isfinite(baseline[c]),
                        "non-finite/unwritten sample");
                const double error=std::abs(double(actual[c])-expected[c]);
                result.maximum_error=std::max(result.maximum_error,error);
                require(error<=2e-6+2e-6*std::abs(expected[c]),
                        "adapter/direct mismatch at block "+std::to_string(b)+" frame "+std::to_string(n)+
                        " channel "+std::to_string(c)+" error="+std::to_string(error));
                result.dsp_difference=std::max(result.dsp_difference,std::abs(double(expected[c])-x[c]));
                if(phase==1)
                    result.parameter_difference=std::max(result.parameter_difference,
                        std::abs(double(expected[c])-baseline[c]));
                result.samples[(b*Frames+n)*2+c]=actual[c];
            }
        }
    }
    if(guard.available()){
        result.allocations=guard.allocations();result.frees=guard.frees();
    }
    require(result.dsp_difference>1e-5,"fixture does not distinguish DSP from dry passthrough");
    require(result.parameter_difference>1e-5,"fixture does not distinguish changed/restored parameters");
    return result;
}
} // namespace
int main(int argc,char** argv){
    try{
        require(argc==3,"usage: legacy_catalog_test ABI2_MODULES_DIR NEXT_PLUGINS_DIR");
        const std::filesystem::path modules=std::filesystem::canonical(argv[1]);
        const auto bridge=std::filesystem::canonical(std::filesystem::path(argv[2])/"legacy/plugin.so");
        // Fixed catalog prevents an accidentally empty or incomplete directory from passing.
        const std::array<const char*,13> names{{"atomic_cluster","attack_decay","big_muff","compressor",
            "lofi_crush","memory_delay","mx_analog_delay","oil_drum_echo","overdrive","reel_echo",
            "small_clone","tape_drive","tremolo"}};
        Guard guard;guard.calibrate();
        std::cout<<"allocation_guard="<<(guard.available()?"calibrated":"UNAVAILABLE")<<'\n';
        unsigned failures=0;unsigned long long allocations=0,frees=0;
        for(const auto* name:names){
            try{
                const auto module=modules/(std::string(name)+".so");
                std::array<float,48> p;
                {Legacy old(module);p=defaults(*old.descriptor,modules/(std::string(name)+".json"));}
                const auto first=render(module,bridge,p,guard);
                const auto second=render(module,bridge,p,guard);
                double repeat_error=0;
                for(size_t i=0;i<first.samples.size();++i)
                    repeat_error=std::max(repeat_error,std::abs(double(first.samples[i])-second.samples[i]));
                require(repeat_error<=2e-6,"cold reload is not deterministic; investigate stochastic state");
                allocations+=first.allocations+second.allocations;frees+=first.frees+second.frees;
                std::cout<<"PASS "<<name<<" frames="<<Blocks*Frames*2
                         <<" max_error="<<std::max(first.maximum_error,second.maximum_error)
                         <<" reload_error="<<repeat_error<<" dsp_delta="<<first.dsp_difference
                         <<" param_delta="<<first.parameter_difference
                         <<" allocations="<<first.allocations+second.allocations
                         <<" frees="<<first.frees+second.frees<<'\n';
            }catch(const std::exception& error){
                ++failures;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';
            }
        }
        std::cout<<"catalog=13 failures="<<failures<<" guarded_allocations="<<allocations
                 <<" guarded_frees="<<frees<<'\n';
        if(guard.available()&&(allocations||frees)){
            std::cerr<<"FAIL process-time allocation/free observed\n";++failures;
        }
        return failures?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
