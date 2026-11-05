"""Build and test the same C renderer used by ESP32 and SDL2; export real pixels.

Run in Linux/macOS (or WSL) with a C99 compiler and Pillow:
  python3 tools/test_sakura_ui.py --cc cc
Outputs are ignored under firmware/build/sakura-preview/.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser();p.add_argument('--cc',default=os.environ.get('CC','cc'));p.add_argument('--sanitize',action='store_true');args=p.parse_args()
    output=ROOT/'firmware/build/sakura-preview';output.mkdir(parents=True,exist_ok=True)
    cc=shlex.split(args.cc)
    flags=['-Wall','-Wextra','-Werror','-std=c99','-g','-I'+str(ROOT/'firmware/src/include')]
    if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    renderer=str(ROOT/'firmware/src/ui/ui_sakura.c')
    for source,name in [('firmware/tests/test_sakura_ui.c','test_sakura_ui'),('firmware/sim/sakura_export.c','sakura_export')]:
        subprocess.run(cc+flags+[str(ROOT/source),renderer,'-lm','-o',str(output/name)],check=True)
    subprocess.run([str(output/'test_sakura_ui')],check=True)
    states=['idle','focus','meeting','night','done','alert','stale','empty','blink','invalid-utf8']
    for state in states:subprocess.run([str(output/'sakura_export'),state,str(output/f'{state}.ppm')],check=True)
    try:
        from PIL import Image,ImageDraw,ImageFont
    except ImportError:
        print(f'C tests passed; PPMs exported to {output}. Install Pillow for PNG previews.');return
    for state in states:
        with Image.open(output/f'{state}.ppm') as img:
            img.save(output/f'{state}.png')
            img.resize((720,720),Image.Resampling.NEAREST).save(output/f'{state}-720.png')
    gallery=Image.new('RGB',(768,560),'#f7ebf2');draw=ImageDraw.Draw(gallery)
    for i,state in enumerate(['idle','focus','night','done','alert','empty']):
        x=12+(i%3)*252;y=12+(i//3)*276
        gallery.paste(Image.open(output/f'{state}.png'),(x,y));draw.text((x,y+245),state,fill='#63384f')
    gallery.save(output/'states.png')
    frames=[]
    for state in ['idle']*6+['blink']+['idle']*2:
        frames.append(Image.open(output/f'{state}-720.png').convert('RGB'))
    frames[0].save(output/'idle.gif',save_all=True,append_images=frames[1:],duration=300,loop=0)
    print(f'Passed; previews: {output}')
if __name__=='__main__':main()
