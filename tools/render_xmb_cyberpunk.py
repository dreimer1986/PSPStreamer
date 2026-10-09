#!/usr/bin/env python3
"""Animate generated production layers, without changing installed EBOOT art."""
import argparse
import math
from pathlib import Path
import subprocess
import numpy as np
from PIL import Image, ImageDraw
from render_xmb_preview import soundtrack, FPS, FRAMES

ART=Path(__file__).resolve().parents[1]/'psp-client/assets/xmb-cyberpunk-concept'
W,H=960,540
def smooth(x):
    x=np.clip(x,0,1)
    return x*x*(3-2*x)

class Scene:
    def __init__(self):
        self.plate=np.array(Image.open(ART/'animation-plate.png').convert('RGB').resize((W,H),Image.Resampling.LANCZOS))
        self.city=np.array(Image.open(ART/'city-panorama.png').convert('RGB'))
        self.logo=Image.open(ART/'icon-master.png').convert('RGB')
        self.y,self.x=np.mgrid[:H,:W]
    def city_view(self,width,height,phase):
        ch,cw,_=self.city.shape
        cropw=int(cw*.58);croph=min(ch,int(cropw*height/width))
        left=round((cw-cropw)*(.5-.5*math.cos(phase)))
        top=max(0,round((ch-croph)*(.45+.10*math.sin(phase))))
        view=Image.fromarray(self.city[top:top+croph,left:left+cropw]).resize((width,height),Image.Resampling.BILINEAR)
        d=ImageDraw.Draw(view,'RGBA')
        for i in range(5):
            x=((phase/(2*math.pi)*(1+i%2)+i*.19)%1)*width;y=height*(.18+i*.075)
            d.line((x-6,y,x+2,y),fill=(60,205,255,190),width=max(1,width//350))
        return view
    def frame(self,index,wallpaper=False):
        t=index/FPS;phase=2*math.pi*index/FRAMES
        if wallpaper:
            im=self.city_view(W,H,phase);d=ImageDraw.Draw(im,'RGBA')
            for i in range(95):
                x=(i*137+t*42)%W;y=(i*73+t*H/2)%H
                d.line((x,y,x-3,y+14),fill=(115,185,230,35),width=1)
            return im
        out=self.plate.copy();sx,sy,sw,sh=552,234,274,158
        city=self.city_view(sw,sh,phase)
        intro=Image.fromarray((np.array(self.logo.resize((sw,sh),Image.Resampling.LANCZOS))*.4).astype('uint8'))
        pen=ImageDraw.Draw(intro,'RGBA');pulse=1+.08*math.sin(phase*4);cx,cy=sw*.5,sh*.5
        pen.ellipse((cx-30*pulse,cy-30*pulse,cx+30*pulse,cy+30*pulse),outline=(60,220,255,255),width=3)
        pen.polygon([(cx-9,cy-17),(cx+18,cy),(cx-9,cy+17)],fill=(165,245,255,255))
        amount=float(smooth((t-1.3)/1.2)*(1-smooth((t-6.6)/1.2)))
        out[sy:sy+sh,sx:sx+sw]=np.array(Image.blend(intro,city,amount))
        # Curved inverse ribbon mapping: frames and perforations travel together.
        # Geometry bends over time; no static film image remains underneath.
        u=np.clip(self.x/630,0,1)
        center=135+180*u+38*np.sin(2*math.pi*u-phase)*np.sin(math.pi*u)
        half=98*(1-u)**1.25+2;v=(self.y-center)/half
        inside=(self.x<630)&(abs(v)<=1)
        travel=-np.log(1-.95*u)*1.7-4*index/FRAMES;tile=travel%1
        texx=(tile*(self.city.shape[1]-1)).astype(int)
        texy=np.clip((v*.5+.5)*(self.city.shape[0]-1),0,self.city.shape[0]-1).astype(int)
        pixels=self.city[texy,texx].astype(float)
        pixels*=(.80+.2*np.cos(v*1.4))[...,None]
        pixels[(abs(v)>.73)|(tile<.035)|(tile>.965)]=(6,7,24)
        pixels[abs(v)>.965]=(30,185,245)
        holes=(abs(v)>.80)&(abs(v)<.92)&((travel*5%1)>.18)&((travel*5%1)<.77)
        pixels[holes]=(100,205,255)
        out[inside]=pixels.clip(0,255).astype('uint8')[inside]
        im=Image.fromarray(out);pen=ImageDraw.Draw(im,'RGBA')
        for i in range(20):
            p=(index/FRAMES*3+i/20)%1;x=600+90*p;y=315+math.sin(i*2.1)*25*(1-p)
            pen.rectangle((x,y,x+2,y+2),fill=(75,200,255,int(160*math.sin(math.pi*p))))
        return im

def encode(scene,output,wav,wallpaper=False):
    cmd=['ffmpeg','-hide_banner','-loglevel','error','-y','-f','rawvideo','-pixel_format','rgb24',
         '-video_size',f'{W}x{H}','-framerate','30000/1001','-i','pipe:0']
    if not wallpaper:cmd+=['-i',str(wav),'-c:a','aac','-b:a','128k']
    cmd+=['-c:v','libx264','-preset','fast','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(output)]
    p=subprocess.Popen(cmd,stdin=subprocess.PIPE)
    try:
        for i in range(FRAMES):p.stdin.write(scene.frame(i,wallpaper).tobytes())
    finally:p.stdin.close()
    if p.wait():raise RuntimeError('ffmpeg failed')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    wav=a.output/'Cyberpunk-Jingle.wav';soundtrack(wav);scene=Scene()
    encode(scene,a.output/'Cyberpunk-Icon-Animation.mp4',wav)
    encode(scene,a.output/'Cyberpunk-Hintergrund-Konzept.mp4',wav,True)
    print(a.output)
if __name__=='__main__':main()
