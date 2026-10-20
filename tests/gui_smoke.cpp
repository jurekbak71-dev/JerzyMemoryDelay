#include "parameters.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <poll.h>
#include <dlfcn.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <filesystem>
using namespace Steinberg;using namespace Steinberg::Vst;
using Clock=std::chrono::steady_clock;
void check(bool value,const char* msg){if(!value)throw std::runtime_error(msg);}
struct TestHost final:HostApplication,IPlugFrame,Linux::IRunLoop,IComponentHandler {
 std::atomic<uint32> refs{1};Display* display=nullptr;Window window=0;int edits=0;ParamID edited=999;
 struct Event{IPtr<Linux::IEventHandler> handler;int fd;};std::vector<Event> events;
 struct Timer{IPtr<Linux::ITimerHandler> handler;Linux::TimerInterval ms;Clock::time_point when;};std::vector<Timer> timers;
 uint32 PLUGIN_API addRef()override{return ++refs;}
 uint32 PLUGIN_API release()override{auto r=--refs;if(!r)delete this;return r;}
 tresult PLUGIN_API queryInterface(const TUID id,void** obj)override{
  *obj=nullptr;
  if(FUnknownPrivate::iidEqual(id,IPlugFrame::iid))*obj=static_cast<IPlugFrame*>(this);
  else if(FUnknownPrivate::iidEqual(id,Linux::IRunLoop::iid))*obj=static_cast<Linux::IRunLoop*>(this);
  else if(FUnknownPrivate::iidEqual(id,IComponentHandler::iid))*obj=static_cast<IComponentHandler*>(this);
  else return HostApplication::queryInterface(id,obj);
  addRef();return kResultOk;
 }
 tresult PLUGIN_API resizeView(IPlugView* view,ViewRect* size)override{
  XResizeWindow(display,window,size->getWidth(),size->getHeight());return view->onSize(size);
 }
 tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* h,int fd)override{events.push_back({h,fd});return kResultOk;}
 tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler* h)override{events.erase(std::remove_if(events.begin(),events.end(),[&](auto& e){return e.handler==h;}),events.end());return kResultOk;}
 tresult PLUGIN_API registerTimer(Linux::ITimerHandler* h,Linux::TimerInterval ms)override{timers.push_back({h,ms,Clock::now()});return kResultOk;}
 tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* h)override{timers.erase(std::remove_if(timers.begin(),timers.end(),[&](auto& t){return t.handler==h;}),timers.end());return kResultOk;}
 tresult PLUGIN_API beginEdit(ParamID)override{return kResultOk;}
 tresult PLUGIN_API performEdit(ParamID id,ParamValue)override{++edits;edited=id;return kResultOk;}
 tresult PLUGIN_API endEdit(ParamID)override{return kResultOk;}
 tresult PLUGIN_API restartComponent(int32)override{return kResultOk;}
 void pump(int ms){
  auto end=Clock::now()+std::chrono::milliseconds(ms);
  while(Clock::now()<end){
   auto snapshot=events;std::vector<pollfd> fds;for(auto& e:snapshot)fds.push_back({e.fd,POLLIN,0});poll(fds.data(),fds.size(),5);
   for(size_t i=0;i<fds.size();++i)if(fds[i].revents&POLLIN)snapshot[i].handler->onFDIsSet(fds[i].fd);
   std::vector<IPtr<Linux::ITimerHandler>> due;
   for(auto& t:timers)if(Clock::now()-t.when>=std::chrono::milliseconds(t.ms)){t.when=Clock::now();due.push_back(t.handler);}
   for(auto& t:due)t->onTimer();
   while(XPending(display)){XEvent event;XNextEvent(display,&event);}
  }
 }
 void capture(const char* name,int width,int height){
  XSync(display,False);auto* image=XGetImage(display,window,0,0,width,height,AllPlanes,ZPixmap);check(image,"cannot capture GUI");
  std::ofstream f(name,std::ios::binary);check(bool(f),"cannot write GUI screenshot");f<<"P6\n"<<width<<' '<<height<<"\n255\n";
  int bright=0,dark=0;
  for(int y=0;y<height;++y)for(int x=0;x<width;++x){auto pixel=XGetPixel(image,x,y);unsigned char rgb[3]{static_cast<unsigned char>((pixel>>16)&255),static_cast<unsigned char>((pixel>>8)&255),static_cast<unsigned char>(pixel&255)};
   f.write(reinterpret_cast<char*>(rgb),3);if(rgb[0]>180&&rgb[1]>180)++bright;if(rgb[0]<60&&rgb[1]<70)++dark;}
  XDestroyImage(image);check(bright>width*height/10&&dark>width*height/5,"GUI has not rendered its panel");
 }
};
int main(int argc,char** argv){try{
 check(argc>=3,"usage: MemoryDelayGUI plugin-bundle screenshot.ppm");std::string error;
 auto module=VST3::Hosting::Module::create(std::filesystem::canonical(argv[1]).string(),error);check(bool(module),error.c_str());
 auto host=owned(new TestHost);auto* context=static_cast<IHostApplication*>(host.get());module->getFactory().setHostContext(context);
 IPtr<IEditController> controller;
 for(const auto& info:module->getFactory().classInfos())if(info.category()==kVstComponentControllerClass)controller=module->getFactory().createInstance<IEditController>(info.ID());
 check(controller&&controller->initialize(context)==kResultOk,"controller initialize");controller->setComponentHandler(host);
 auto* display=XOpenDisplay(nullptr);check(display,"DISPLAY unavailable");host->display=display;
 auto view=owned(controller->createView(ViewType::kEditor));check(bool(view),"editor missing");ViewRect size{};view->getSize(&size);
 host->window=XCreateSimpleWindow(display,DefaultRootWindow(display),0,0,size.getWidth(),size.getHeight(),0,0,0);
 XMapWindow(display,host->window);XFlush(display);view->setFrame(host);
 check(view->attached(reinterpret_cast<void*>(host->window),kPlatformTypeX11EmbedWindowID)==kResultOk,"editor attachment failed");
 // A DAW embeds the child using the XEmbed handshake, including activation.
 Window rootWindow=0,parentWindow=0;Window* children=nullptr;unsigned childCount=0;
 XQueryTree(display,host->window,&rootWindow,&parentWindow,&children,&childCount);
 for(unsigned i=0;i<childCount;++i){
  XEvent event{};event.xclient.type=ClientMessage;event.xclient.window=children[i];
  event.xclient.message_type=XInternAtom(display,"_XEMBED",False);event.xclient.format=32;
  event.xclient.data.l[0]=CurrentTime;event.xclient.data.l[1]=0;event.xclient.data.l[3]=host->window;
  XSendEvent(display,children[i],False,NoEventMask,&event);event.xclient.data.l[1]=1;
  XSendEvent(display,children[i],False,NoEventMask,&event);XMapWindow(display,children[i]);
 }
 if(children)XFree(children);XFlush(display);
 host->pump(450);
 std::cerr<<"GUI runloop: "<<host->events.size()<<" descriptors, "<<host->timers.size()<<" timers\n";
 host->capture(argv[2],size.getWidth(),size.getHeight());
 auto* xtst=dlopen("libXtst.so.6",RTLD_NOW);check(xtst,"XTest extension library unavailable");
 auto motion=reinterpret_cast<int(*)(Display*,int,int,int,unsigned long)>(dlsym(xtst,"XTestFakeMotionEvent"));
 auto button=reinterpret_cast<int(*)(Display*,unsigned int,Bool,unsigned long)>(dlsym(xtst,"XTestFakeButtonEvent"));check(motion&&button,"XTest functions unavailable");
 auto click=[&](int x,int y){motion(display,-1,x,y,0);button(display,1,True,0);button(display,1,False,0);XFlush(display);host->pump(150);};
 click(705,580);check(controller->getParamNormalized(Jerzy::Bypass)>.5&&host->edited==Jerzy::Bypass,"footswitch did not automate bypass");
 click(705,580);check(controller->getParamNormalized(Jerzy::Bypass)<.5,"footswitch did not re-enable effect");
 click(170,613);host->capture((std::string(argv[2])+"-details.ppm").c_str(),size.getWidth(),size.getHeight());
 controller->setParamNormalized(Jerzy::Model,0);host->pump(150);
 int edits=host->edits;click(110,681);check(host->edits>edits&&host->edited==Jerzy::Boost,"EH7500 boost unavailable");
 controller->setParamNormalized(Jerzy::Model,.6);host->pump(150);edits=host->edits;click(110,681);check(host->edits==edits,"boost remained enabled on Deluxe");
 view->removed();view->setFrame(nullptr);view=nullptr;
 check(host->events.empty()&&host->timers.empty(),"editor left event handlers or timers behind");
 controller->setComponentHandler(nullptr);controller->terminate();controller=nullptr;XDestroyWindow(display,host->window);XCloseDisplay(display);dlclose(xtst);
 std::cout<<"GUI passed: render, footswitch automation, details drawer, circuit-specific boost, clean close\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
