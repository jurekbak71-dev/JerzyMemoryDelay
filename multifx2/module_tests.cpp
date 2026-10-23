#include "api.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <string>
#include <vector>
int main(int argc,char** argv){
 if(argc!=2)return 2;
 const std::string folder=argv[1];
 for(const std::string& key:{"compressor","overdrive","tremolo","memory_delay","small_clone","big_muff","attack_decay","tape_drive","mx_analog_delay","atomic_cluster","reel_echo","oil_drum_echo"}){
  const std::string file=folder+"/"+key+".so";
  void* library=dlopen(file.c_str(),RTLD_NOW|RTLD_LOCAL);
  if(!library){std::cerr<<"Unable to load "<<file<<": "<<dlerror()<<"\n";return 1;}
  auto entry=reinterpret_cast<JfxEntryPoint>(dlsym(library,"jfx_plugin"));
  if(!entry){std::cerr<<"Module missing jfx_plugin\n";return 1;}
  const JfxDescriptor* d=entry();
  if(!d||d->abi_version!=JFX_ABI_VERSION||!d->name||!d->key||key!=d->key||
    d->parameter_count>JFX_MAX_PARAMS||!d->create||!d->destroy||!d->process){
    std::cerr<<"Module descriptor invalid: "<<key<<"\n";return 1;
  }
  if(key=="memory_delay"&&d->parameter_count!=14){
    std::cerr<<"Original Memory Delay controls lost\n";return 1;
  }
  void* instance=d->create(44100.);
  if(!instance){std::cerr<<"Cannot create module\n";return 1;}
  std::array<float,JFX_MAX_PARAMS> p{};
  for(unsigned i=0;i<d->parameter_count;++i) {
    p[i]=d->parameters[i].default_value;
    if(!std::isfinite(p[i])||p[i]<0||p[i]>1){std::cerr<<"Invalid defaults\n";return 1;}
  }
  for(int n=0;n<44100;++n){
    float input[2]{(n<1000)?float(.2*std::sin(6.283185307179586*440*n/44100)):0,
                 (n<1000)?float(.15*std::sin(6.283185307179586*330*n/44100)):0};
    float output[2]{0,0};
    d->process(instance,input,output,p.data(),120);
    for(float v:output)if(!std::isfinite(v)||std::abs(v)>8.f){
      std::cerr<<"Invalid audio from "<<key<<"\n";return 1;
    }
  }
  d->destroy(instance);
  dlclose(library);
 }
 std::cout<<"All 12 separate .so modules loaded, all audio finite, Memory Delay has 14 controls\n";
}
