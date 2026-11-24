#include "jfx.h"
#include "dual_cab.hpp"
namespace {
void* create(double sr,uint32_t max,const char* asset){try{return new nextfx::DualCab(sr,max,asset?asset:"");}catch(...){return nullptr;}}
void destroy(void* p){delete static_cast<nextfx::DualCab*>(p);}
void process(void* p,const JfxBlock* b){static_cast<nextfx::DualCab*>(p)->process(b->frames,b->inputs,b->outputs,b->parameters);}
uint64_t bytes(void* p){return static_cast<nextfx::DualCab*>(p)->resource_bytes();}
const JfxParameter params[]{{"mix","Dry/Wet",0,1,1},{"blend","IR A/B",0,1,0},{"level","Level",0,1,.5},{"lowcut","Low cut",0,1,.12},{"highcut","High cut",0,1,.85},{"enabled","Enabled",0,1,1}};
const JfxDescriptor d{JFX_MAGIC,sizeof(JfxDescriptor),3,0,"cab_dual","1.0.0","Dual guitar CAB",2,2,0,6,params,64,0,create,destroy,process,nullptr,nullptr,nullptr,bytes};
}
extern "C" JFX_EXPORT const JfxDescriptor* jfx_entry_v3(){return &d;}
