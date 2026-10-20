"""Generate the editable VSTGUI layout; no third-party artwork is required."""
from pathlib import Path
import xml.etree.ElementTree as ET
root=ET.Element('vstgui-ui-description',version='1')
fonts=ET.SubElement(root,'fonts')
for name,size in [('Label',12),('Small',10),('Value',13)]:
 ET.SubElement(fonts,'font',{'name':name,'font-name':'Arial','size':str(size),'bold':'true'})
colors=ET.SubElement(root,'colors')
for name,value in [('Cream','#e8e5d4ff'),('Dark','#151719ff'),('Ink','#30332fff'),('Red','#c8322aff'),('Muted','#a8ad9bff'),('Drawer','#252a2bff')]:
 ET.SubElement(colors,'color',{'name':name,'rgba':value})
tags=ET.SubElement(root,'control-tags')
names=['Blend','Level','Feedback','Rate','Depth','Delay','Model','Wave','Bypass','Boost','Squelch','ModEnable']
for i,n in enumerate(names):ET.SubElement(tags,'control-tag',name=n,tag=str(i))
ET.SubElement(tags,'control-tag',name='Overload',tag='100')
ET.SubElement(tags,'control-tag',name='Details',tag='900')
view=ET.SubElement(root,'template',{'name':'view','class':'CViewContainer','origin':'0, 0','size':'1080, 800','background-color':'Dark'})
def add(parent,cls,x,y,w,h,**attrs):
 return ET.SubElement(parent,'view',{'class':cls,'origin':f'{x}, {y}','size':f'{w}, {h}',**{k.replace('_','-'):str(v) for k,v in attrs.items()}})
def label(parent,x,y,w,h,title='',color='Cream',**attrs):
 return add(parent,'CTextLabel',x,y,w,h,title=title,font='Label',font_color=color,transparent='true',mouse_enabled='false',text_alignment='center',**attrs)
add(view,'MemoryPanel',0,0,1080,800,mouse_enabled='false')
for i,(tag,title) in enumerate(zip(names,['BLEND','LEVEL','FEEDBACK','CHORUS / VIBRATO','DEPTH','DELAY'])):
 x=70+i*156
 add(view,'MemoryKnob',x,105,150,150,control_tag=tag,wheel_inc_value='.005',tooltip='Drag to adjust; Shift: fine adjustment; right-click: reset')
 label(view,x-2,260,154,20,title,color='Cream' if i<3 else 'Ink')
 add(view,'CTextLabel',x+10,284,130,22,control_tag=tag,font='Value',font_color='Cream',back_color='Dark',frame_color='Dark',transparent='false',text_alignment='center',mouse_enabled='false')
add(view,'MemoryLed',300,320,30,30,control_tag='Overload',mouse_enabled='false')
label(view,75,554,75,24,'CIRCUIT',color='Muted')
add(view,'COptionMenu',159,550,270,32,control_tag='Model',font='Label',font_color='Cream',back_color='Drawer',frame_color='Muted',tooltip='Choose the modeled circuit; switching fades through dry audio')
add(view,'CTextButton',79,598,180,30,control_tag='Details',title='CIRCUIT DETAILS',font='Small',text_color='Cream',text_color_highlighted='Red',frame_color='Muted',kick_style='false',round_radius='4')
add(view,'MemoryFootswitch',650,525,112,112,control_tag='Bypass',tooltip='Effect bypass')
add(view,'MemoryLed',690,494,30,30,control_tag='Bypass',mouse_enabled='false')
# Local UI drawer: not a sound parameter and intentionally not saved in DAW state.
drawer=add(view,'CViewContainer',77,634,540,85,background_color='Drawer',editor_role='details')
label(drawer,10,7,90,18,'BOOST',color='Muted')
add(drawer,'CTextButton',15,33,80,28,control_tag='Boost',title='BOOST',font='Small',kick_style='false',tooltip='EH7500 input boost')
label(drawer,110,7,100,18,'SQUELCH',color='Muted')
add(drawer,'MemoryKnob',132,25,52,52,control_tag='Squelch',wheel_inc_value='.01')
label(drawer,225,7,130,18,'LFO SHAPE',color='Muted')
add(drawer,'COptionMenu',225,34,125,28,control_tag='Wave',font='Small',font_color='Cream',back_color='Dark',frame_color='Muted')
label(drawer,375,7,145,18,'MODULATION',color='Muted')
add(drawer,'CTextButton',390,34,110,28,control_tag='ModEnable',title='MOD ON',font='Small',kick_style='false')
ET.indent(root)
Path(__file__).resolve().parents[1].joinpath('resource/memory.uidesc').write_text(ET.tostring(root,encoding='unicode')+'\n')
