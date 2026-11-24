#include "jfx.h"
#include <new>
namespace {
struct State{float y[2]{};};
void *create(double,uint32_t,const char*){return new(std::nothrow)State;}
void destroy(void*p){delete static_cast<State*>(p);}
void process(void*p,const JfxBlock*b){auto&s=*static_cast<State*>(p);for(uint32_t i=0;i<b->frames;i++)for(unsigned c=0;c<2;c++){s.y[c]=b->inputs[c][i]+s.y[c]*.99f;b->outputs[c][i]=s.y[c];}}
JfxDescriptor d{JFX_MAGIC,sizeof(JfxDescriptor),3,0,"test_tail","1","Deterministic tail",2,2,0,0,nullptr,0,0,create,destroy,process,nullptr,nullptr,nullptr,nullptr};
}
extern "C" JFX_EXPORT const JfxDescriptor*jfx_entry_v3(){return &d;}
