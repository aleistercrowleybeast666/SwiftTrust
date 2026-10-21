"""Build only the UI's used CJK glyphs from OFL Noto Sans SC; no full font in firmware."""
import argparse,hashlib,json
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('font',type=Path);args=parser.parse_args()
texts=''.join((ROOT/p).read_text(encoding='utf-8') for p in ['Controller/src/st_ui2.c','Controller/src/st_cal_ui.c'])
characters=sorted({c for c in texts if ord(c)>127})
font=ImageFont.truetype(str(args.font),14);font.set_variation_by_axes([500])
lines=['/* Generated Noto Sans SC 12x14/weight500 subset; OFL in Controller/fonts/OFL.txt. */',
       'static const struct { uint32_t code; uint8_t columns[24]; } cjk_glyphs[] = {']
for character in characters:
 image=Image.new('L',(14,14));draw=ImageDraw.Draw(image)
 left,top,right,bottom=font.getbbox(character)
 draw.text(((14-(right-left))//2-left,(14-(bottom-top))//2-top),character,font=font,fill=255)
 image=image.resize((12,14),Image.Resampling.LANCZOS)
 columns=[]
 for x in range(12):
  bits=sum((1<<y) for y in range(14) if image.getpixel((x,y))>=128)
  columns.extend([bits&255,bits>>8])
 lines.append('    {0x%04Xu,{%s}}, /* %s */'%(ord(character),','.join('0x%02x'%b for b in columns),character))
lines+=['};','']
(ROOT/'Controller/src/st_font_subset.inc').write_text('\n'.join(lines),encoding='utf-8')
manifest={'font':'Noto Sans SC','license':'SIL OFL 1.1','source':'https://github.com/google/fonts/tree/main/ofl/notosanssc',
 'source_sha256':hashlib.sha256(args.font.read_bytes()).hexdigest(),'width':12,'height':14,'weight':500,
 'glyph_count':len(characters),'glyph_bitmap_bytes':len(characters)*24,'codepoints':[ord(c) for c in characters]}
(ROOT/'Controller/fonts/SOURCE.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Subset:',len(characters),'glyphs,',len(characters)*24,'bitmap bytes')
