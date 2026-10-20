#include "parameters.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include <vector>
#include <cstdio>
namespace Jerzy {
using namespace VSTGUI;
namespace {
constexpr double pi=3.14159265358979323846;
void ellipse(CDrawContext* c,double x,double y,double r,CColor fill,CColor border,double width=1.){
 c->setFillColor(fill);c->setFrameColor(border);c->setLineWidth(width);
 c->drawEllipse({x-r,y-r,x+r,y+r},kDrawFilledAndStroked);
}
void gradient(CDrawContext* c,const CRect& r,CColor a,CColor b,double radius=0.){
 auto* path=c->createGraphicsPath();if(!path)return;
 if(radius>0)path->addRoundRect(r,radius);else path->addRect(r);
 auto* g=CGradient::create(0,1,a,b);
 if(g){c->fillLinearGradient(path,*g,{r.left,r.top},{r.right,r.bottom});g->forget();}path->forget();
}
void text(CDrawContext* c,const char* label,const CRect& r,double size,CColor color,CHoriTxtAlign align=kCenterText){
 c->setFont(kNormalFontVeryBig,size,kBoldFace);c->setFontColor(color);c->drawString(label,r,align);
}
void screw(CDrawContext* c,double x,double y){
 ellipse(c,x+1,y+2,8,{0,0,0,80},{0,0,0,0});ellipse(c,x,y,7,{162,161,154},{89,89,84});
 ellipse(c,x-1,y-1,5,{203,203,195},{213,212,204});c->setFrameColor({85,85,81});c->setLineWidth(2);c->drawLine({x-3,y+2},{x+3,y-2});
}
class MemoryKnob final:public CKnob {
public:
 MemoryKnob():CKnob({0,0,150,150},nullptr,-1,nullptr,nullptr){}
 void draw(CDrawContext* c) override {
  const auto center=getViewSize().getCenter();const double x=center.x,y=center.y;
  const double radius=std::min(getViewSize().getWidth(),getViewSize().getHeight())*.365;
  c->setDrawMode(kAntiAliasing|kNonIntegralMode);
  for(int i=0;i<=10;++i){
   double a=(135.+27.*i)*pi/180.,inner=radius+8.,outer=inner+(i%5==0?8.:4.);
   c->setFrameColor({135,132,120,140});c->setLineWidth(i%5==0?2.:1.);
   c->drawLine({x+std::cos(a)*inner,y+std::sin(a)*inner},{x+std::cos(a)*outer,y+std::sin(a)*outer});
  }
  for(int i=8;i>0;--i)ellipse(c,x+4,y+8,radius+i,{0,0,0,static_cast<uint8_t>(4+(8-i)*2)},{0,0,0,0},0);
  ellipse(c,x,y,radius+2,{18,18,18},{134,131,121},1.5);
  gradient(c,{x-radius,y-radius,x+radius,y+radius},{88,87,81},{9,10,11},radius);
  ellipse(c,x,y,radius-3,{30,31,31},{9,9,10},2.);
  gradient(c,{x-radius+5,y-radius+5,x+radius-5,y+radius-5},{52,53,52},{19,20,21},radius-5);
  for(int i=0;i<64;++i){double a=2*pi*i/64.;c->setFrameColor(i%2?CColor(87,85,77,100):CColor(6,7,8,160));c->setLineWidth(1);
   c->drawLine({x+(radius-3)*std::cos(a),y+(radius-3)*std::sin(a)},{x+(radius-1)*std::cos(a),y+(radius-1)*std::sin(a)});}
  double a=(135.+270.*getValueNormalized())*pi/180.;c->setFrameColor(getMouseEnabled()?CColor(247,239,217):CColor(110,108,101));c->setLineWidth(3.2);
  c->drawLine({x+radius*.5*std::cos(a),y+radius*.5*std::sin(a)},{x+radius*.86*std::cos(a),y+radius*.86*std::sin(a)});setDirty(false);
 }
 CMouseEventResult onMouseDown(CPoint& p,const CButtonState& b) override {
  if(b.isRightButton()){
   beginEdit();setValueNormalized(float(defaults[std::clamp(getTag(),0,int(Count)-1)]));valueChanged();endEdit();invalid();
   return kMouseDownEventHandledButDontNeedMovedOrUpEvents;
  }return CKnob::onMouseDown(p,b);
 }
};
class Footswitch final:public COnOffButton {
public:
 Footswitch():COnOffButton({0,0,112,112}){}
 void draw(CDrawContext* c) override {
  auto center=getViewSize().getCenter();double x=center.x,y=center.y;c->setDrawMode(kAntiAliasing|kNonIntegralMode);
  ellipse(c,x+3,y+7,46,{0,0,0,100},{0,0,0,0});
  CDrawContext::PointList hex;for(int i=0;i<6;++i){double a=(30.+60*i)*pi/180.;hex.push_back({x+45*std::cos(a),y+45*std::sin(a)});}
  c->setFillColor({174,174,164});c->setFrameColor({225,222,208});c->setLineWidth(2);c->drawPolygon(hex,kDrawFilledAndStroked);
  ellipse(c,x,y,35,{90,91,87},{36,36,35},2);gradient(c,{x-30,y-30,x+30,y+30},{242,242,226},{113,115,110},30);
  ellipse(c,x,y,24,{186,188,179},{223,225,212});gradient(c,{x-22,y-22,x+22,y+22},{213,216,204},{153,156,149},22);setDirty(false);
 }
};
class Led final:public CControl {
public:
 Led():CControl({0,0,30,30},nullptr,-1) {setMouseEnabled(false);}
 CBaseObject* newCopy() const override {return new Led;}
 void draw(CDrawContext* c) override {
  auto p=getViewSize().getCenter();bool on=getTag()==Bypass?getValue()<.5f:getValue()>.8f,red=getTag()==int(OverloadMeter);
  c->setDrawMode(kAntiAliasing|kNonIntegralMode);
  if(on)for(int i=6;i>0;--i)ellipse(c,p.x,p.y,8+i,red?CColor(255,32,12,10):CColor(255,140,25,10),{0,0,0,0},0);
  ellipse(c,p.x,p.y,10,{17,17,17},{87,83,74});
  ellipse(c,p.x,p.y,7.5,on?(red?CColor(243,45,25):CColor(255,133,32)):CColor(72,30,22),{56,21,17});
  ellipse(c,p.x-2,p.y-2,3,on?CColor(255,215,164):CColor(118,61,46),{0,0,0,0},0);setDirty(false);
 }
};
class MemoryPanel final:public CView {
public:
 MemoryPanel():CView({0,0,1080,800}){setMouseEnabled(false);}
 void draw(CDrawContext* c) override {
  c->setDrawMode(kAntiAliasing|kNonIntegralMode);gradient(c,getViewSize(),{24,42,44},{9,17,20});
  for(int i=12;i>0;--i)gradient(c,{27.+i*.6,23.+i,1053.+i*.4,774.+i},{0,0,0,18},{0,0,0,35},40);
  gradient(c,{25,19,1055,779},{118,120,112},{49,52,50},38);
  gradient(c,{25,15,1055,763},{253,250,233},{131,136,129},38);
  gradient(c,{30,18,1050,756},{219,220,207},{247,244,224},34);
  gradient(c,{36,24,1044,750},{237,234,215},{187,189,179},29);
  for(int y=32;y<747;y+=2){c->setFrameColor({255,255,240,static_cast<uint8_t>(y%7+3)});c->setLineWidth(.5);c->drawLine({49.,double(y)},{1031.,double(y)});}
  CDrawContext::PointList face{{45,115},{70,82},{1025,463},{1034,490},{1034,720},{1017,738},{63,738},{45,720}};
  c->setFillColor({19,21,23});c->setFrameColor({60,62,59});c->setLineWidth(1);c->drawPolygon(face,kDrawFilledAndStroked);
  for(int y=480;y<733;y+=3){c->setFrameColor({255,255,255,3});c->setLineWidth(.5);c->drawLine({51.,double(y)},{1027.,double(y)});}
  for(auto x:{65.,1015.})for(auto y:{51.,710.})screw(c,x,y);
  text(c,"JERZY AUDIO",{100,42,370,76},20,{39,42,40},kLeftText);
  text(c,"ANALOG MEMORY COLLECTION",{660,46,995,70},11,{88,91,83},kRightText);
  text(c,"DELUXE",{82,319,385,365},32,{211,42,37},kLeftText);
  text(c,"MEMORY",{76,370,725,461},80,{232,229,212},kLeftText);
  text(c,"DELAY",{78,455,530,541},80,{232,229,212},kLeftText);
  text(c,"Echo / Chorus / Vibrato",{655,432,1000,466},19,{209,48,39},kRightText);
  text(c,"JERZY AUDIO",{755,651,992,688},23,{223,220,201},kRightText);
  text(c,"BUCKET BRIGADE / VST3",{729,690,992,713},10,{136,139,128},kRightText);
  text(c,"OVERLOAD",{342,322,460,346},10,{219,215,196},kLeftText);
  text(c,"EFFECT",{655,634,755,660},10,{208,206,187});setDirty(false);
 }
};
template<class T> class Creator final:public ViewCreatorAdapter {
 const char* name;const char* base;
public:
 Creator(const char* n,const char* b):name(n),base(b){UIViewFactory::registerViewCreator(*this);}
 IdStringPtr getViewName()const override{return name;}IdStringPtr getBaseViewName()const override{return base;}
 CView* create(const UIAttributes&,const IUIDescription*)const override{return new T;}
};
void registerViews(){
 static Creator<MemoryKnob> k("MemoryKnob","CKnob");static Creator<MemoryPanel> p("MemoryPanel","CView");
 static Creator<Footswitch> s("MemoryFootswitch","COnOffButton");static Creator<Led> l("MemoryLed","CControl");
}
class Editor final:public VST3Editor {
 std::vector<CControl*> controls;CView* details=nullptr;SharedPointer<CVSTGUITimer> timer;
 void refresh(){
  int model=std::clamp(int(std::lround(getController()->getParamNormalized(Model)*5)),0,5);
  const bool sync=getController()->getParamNormalized(TempoSync)>=.5;
  for(auto* c:controls){auto tag=c->getTag();bool enabled=true;
   if(auto* label=dynamic_cast<CTextLabel*>(c)){
    if(tag==Delay){
     char text[32]{};
     if(sync)std::snprintf(text,sizeof(text),"%s",divisions[divisionIndex(getController()->getParamNormalized(Division))].name);
     else std::snprintf(text,sizeof(text),"%.0f ms",delayMs(getController()->getParamNormalized(Delay)));
     label->setText(text);
    }
    continue;
   }
   if(tag==Delay)enabled=!sync;
   if(tag==Division)enabled=sync;
   if(tag==ModRate||tag==Depth||tag==Wave||tag==ModEnable)enabled=model>=3;
   if(tag==Boost)enabled=model==0;if(tag==Squelch)enabled=model==1||model==2;
   bool led=dynamic_cast<Led*>(c)!=nullptr;if(led)enabled=false;
   if(c->getMouseEnabled()!=enabled){c->setMouseEnabled(enabled);c->setAlphaValue(enabled||led?1.f:.3f);c->invalid();}
  }
 }
public:
 explicit Editor(Steinberg::Vst::EditController* controller):VST3Editor(controller,"view","memory.uidesc"){setAllowedZoomFactors({.65,.75,1.,1.25,1.5});}
 CView* verifyView(CView* view,const UIAttributes& attr,const IUIDescription* desc) override {
  view=VST3Editor::verifyView(view,attr,desc);
  if(const auto* role=attr.getAttributeValue("editor-role");role&&*role=="details"){details=view;details->setVisible(false);}return view;
 }
 void onViewAdded(CFrame* f,CView* v) override {VST3Editor::onViewAdded(f,v);if(auto* c=dynamic_cast<CControl*>(v))controls.push_back(c);}
 void onViewRemoved(CFrame* f,CView* v) override {
  controls.erase(std::remove(controls.begin(),controls.end(),v),controls.end());if(v==details)details=nullptr;VST3Editor::onViewRemoved(f,v);
 }
 void valueChanged(CControl* c) override {
  if(c->getTag()==900){if(details){details->setVisible(c->getValue()>.5f);details->invalid();}return;}VST3Editor::valueChanged(c);refresh();
 }
 bool PLUGIN_API open(void* parent,const PlatformType& type) override {
  if(!VST3Editor::open(parent,type))return false;timer=owned(new CVSTGUITimer([this](CVSTGUITimer*){refresh();},50));refresh();return true;
 }
 void PLUGIN_API close() override {timer=nullptr;VST3Editor::close();controls.clear();details=nullptr;}
};
}
Steinberg::IPlugView* createEditor(Steinberg::Vst::EditController* controller){registerViews();return new Editor(controller);}
}
