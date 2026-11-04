# Builds Android/app/src/main/assets/intro/logo.jpg (the logo before the opening video) from Rafael's picture:
# the plate's text is replaced by the edition's name in the title font. Run as:
#   python3 intro/make_logo.py 392 412 1278 548 68 80 "DEFINITIVE EDITION" <picture.png> out.png
# then save out.png as logo.jpg (quality 93).
from PIL import Image, ImageDraw, ImageFilter, ImageFont
import numpy as np, sys
SRC=sys.argv[8]  # Rafael's logo picture
im=Image.open(SRC).convert('RGB'); a=np.asarray(im).astype(float)
X0,Y0,X1,Y1=int(sys.argv[1]),int(sys.argv[2]),int(sys.argv[3]),int(sys.argv[4])  # interior box
R=int(sys.argv[5])
mask=Image.new('L',im.size,0); ImageDraw.Draw(mask).rounded_rectangle([X0,Y0,X1,Y1],radius=R,fill=255)
mask=mask.filter(ImageFilter.GaussianBlur(4)); m=np.asarray(mask)/255.0
# fill: interpolate between rows just above/below text region per column
top=a[Y0+6]; bot=a[Y1-6]
fill=np.zeros_like(a)
for y in range(a.shape[0]):
    t=np.clip((y-(Y0+6))/((Y1-6)-(Y0+6)),0,1); fill[y]=top*(1-t)+bot*t
fill=np.asarray(Image.fromarray(fill.clip(0,255).astype('uint8')).filter(ImageFilter.GaussianBlur(10))).astype(float)
fill=fill*0.55
out=a*(1-m[...,None])+fill*m[...,None]
base=Image.fromarray(out.clip(0,255).astype('uint8'))
# text
font=ImageFont.truetype(__import__('os').path.join(__import__('os').path.dirname(__file__),'..','mm','assets','custom','fonts','Alegreya-Bold.ttf'),int(sys.argv[6]))
text=sys.argv[7]
d=ImageDraw.Draw(Image.new('L',(1,1))); bb=d.textbbox((0,0),text,font=font)
tw,th=bb[2]-bb[0],bb[3]-bb[1]; cx,cy=(X0+X1)/2,(Y0+Y1)/2
pos=(cx-tw/2-bb[0],cy-th/2-bb[1])
tm=Image.new('L',im.size,0); ImageDraw.Draw(tm).text(pos,text,font=font,fill=255)
glow=tm.filter(ImageFilter.GaussianBlur(14)); glow2=tm.filter(ImageFilter.GaussianBlur(4))
b=np.asarray(base).astype(float)
g=np.asarray(glow)/255.0; g2=np.asarray(glow2)/255.0
purple=np.array([220,120,255.]); 
b=b+(purple-b)*np.clip(g*1.1,0,1)[...,None]*0.75
b=b+(np.array([255,200,255.])-b)*np.clip(g2,0,1)[...,None]*0.35
# cream gradient fill
H=np.arange(im.size[1])[:,None].repeat(im.size[0],1)
t=np.clip((H-(cy-th/2))/th,0,1)
cream=np.stack([255-20*t,248-50*t,232-80*t],-1)
tmask=np.asarray(tm)/255.0
b=b*(1-tmask[...,None])+cream*tmask[...,None]
Image.fromarray(b.clip(0,255).astype('uint8')).save(sys.argv[9])
