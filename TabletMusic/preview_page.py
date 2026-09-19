"""Render the built MUSIC page canvas to SVG so the layout can be judged offline.

Reads the real canvas the patch ships, so rects, colours, font sizes, hidden
flags and text anchoring are exactly what Echo will load. Only the typeface is
approximated. Dynamic labels are filled with sample runtime values.
"""
import argparse,re,struct,sys
from pathlib import Path
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'EchoVr-Tablet-Probing'))
from canvas_edit import Canvas,u64

TEXT,PANEL=1,2
# Element indices come from the header the builder generates, the same one the
# native runtime compiles against, so this can never drift from the real page.
HEADER=HERE.parent/'PersonalDiscTrainer/native/generated/music_tab.h'
K=dict((m.group(1),int(m.group(2))) for m in
       re.finditer(r'MUSIC_([A-Z_]+)=(\d+)\b',HEADER.read_text()))
# What the native runtime writes into each dynamic slot.
SAMPLES={'SOURCE_TEXT':'SPOTIFY','TITLE_TEXT':'Everything In Its Right Place',
         'ARTIST_TEXT':'Radiohead','ELAPSED_TEXT':'1:42','TOTAL_TEXT':'4:11',
         'PLAY_TEXT':'PAUSE'}
EMPTY={'SOURCE_TEXT':'NO PLAYER','TITLE_TEXT':'NOTHING PLAYING','ARTIST_TEXT':'',
       'ELAPSED_TEXT':'','TOTAL_TEXT':'','PLAY_TEXT':'PLAY'}

def strings(canvas):
    """Element index -> its literal text, from the canvas string table."""
    out={}
    for i,row in enumerate(canvas.elements):
        if struct.unpack_from('<I',row,8)[0]!=TEXT: continue
        slot,capacity=struct.unpack_from('<II',row,0x90)
        start=struct.unpack_from('<I',canvas.string_offsets,slot*4)[0]
        blob=bytes(canvas.text[start:start+capacity])
        out[i]=(blob.split(b'\0')[0].decode('utf-8','replace'),capacity)
    return out

def render(canvas,fraction,volume,named,label):
    w,h=struct.unpack_from('<2I',canvas.header,0x14)
    literals=strings(canvas)
    values={K[k]:v for k,v in named.items() if k in K}
    meters={}  # element index -> lit or not, mirroring the runtime's show/hide
    for first,count,filled in ((K['BAR_FIRST'],K['BAR_COUNT'],round(fraction*K['BAR_COUNT'])),
                               (K['VOL_FIRST'],K['VOL_COUNT'],round(volume*K['VOL_COUNT']))):
        for i in range(count): meters[first+i]=i<filled
    parts=[f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}">',
           f'<title>Echo VR tablet MUSIC page - {label}</title>',
           '<rect width="100%" height="100%" fill="#000"/>']
    for i,row in enumerate(canvas.elements):
        kind=struct.unpack_from('<I',row,8)[0]
        hidden=struct.unpack_from('<I',row,0x10)[0]
        x0,y0,x1,y1=struct.unpack_from('<4f',row,0x34)
        r,g,b,a=struct.unpack_from('<4f',row,0x78)
        if i in meters: hidden=0 if meters[i] else 1
        if hidden: continue
        color='#%02x%02x%02x'%tuple(min(255,max(0,round(v*255))) for v in (r,g,b))
        if kind==PANEL:
            parts.append(f'<rect x="{x0:.1f}" y="{y0:.1f}" width="{x1-x0:.1f}" height="{y1-y0:.1f}" '
                         f'fill="{color}" fill-opacity="{a:.2f}"/>')
        elif kind==TEXT:
            text=values.get(i,literals.get(i,('',0))[0])
            if not text: continue
            size=struct.unpack_from('<H',row,0xa4)[0]
            # Anchor fractions, X then Y: 0 left/top, 0.5 centre, 1 right/bottom.
            ax,ay=struct.unpack_from('<2f',row,0x9c)
            anchor='start' if ax<.25 else 'end' if ax>.75 else 'middle'
            baseline='hanging' if ay<.25 else 'auto' if ay>.75 else 'central'
            escaped=text.replace('&','&amp;').replace('<','&lt;').replace('>','&gt;')
            parts.append(f'<text x="{x0+(x1-x0)*ax:.1f}" y="{y0+(y1-y0)*ay:.1f}" fill="{color}" '
                         f'fill-opacity="{a:.2f}" font-family="Segoe UI, DejaVu Sans, sans-serif" '
                         f'font-size="{size}" text-anchor="{anchor}" dominant-baseline="{baseline}">{escaped}</text>')
    parts.append('</svg>')
    return '\n'.join(parts)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--canvas',default=str(HERE/'build/music.canvas'))
    ap.add_argument('--out',default=str(HERE/'build'))
    args=ap.parse_args()
    out=Path(args.out);out.mkdir(parents=True,exist_ok=True)
    # One sheet per built theme, so they can be compared side by side.
    themed=sorted(out.glob('music-*.canvas'))
    sheets=[]
    if themed:
        for path in themed:
            name=path.stem.replace('music-','')
            svg=render(Canvas(path.read_bytes()),102/251,.45,SAMPLES,name)
            (out/f'music-page-{name}.svg').write_text(svg,newline='\n')
            sheets.append((name,svg))
            print('Wrote',out/f'music-page-{name}.svg')
    canvas=Canvas(Path(args.canvas).read_bytes())
    for name,fraction,volume,values in [('idle',0,0,EMPTY)]:
        path=out/f'music-page-{name}.svg'
        svg=render(canvas,fraction,volume,values,name)
        path.write_text(svg,newline='\n')
        sheets.append((name,svg))
        print('Wrote',path)
    # One sheet showing both states, scaled to whatever window it is opened in.
    body=''.join(f'<figure><figcaption>{name}</figcaption>{svg}</figure>' for name,svg in sheets)
    page=('<!doctype html><meta charset="utf-8"><title>MUSIC page preview</title>'
          '<style>body{background:#15181c;color:#c8d2dc;font:14px system-ui,sans-serif;margin:24px}'
          'h1{font-size:16px;font-weight:600}'
          'figure{margin:0 0 28px;max-width:942px}'
          'figcaption{text-transform:uppercase;letter-spacing:.08em;font-size:12px;color:#7f8c99;margin-bottom:6px}'
          'figure svg{width:100%;height:auto;border:1px solid #2a3138;display:block}</style>'
          f'<h1>Echo VR tablet - MUSIC page (942 x 528)</h1>{body}')
    sheet=out/'music-page.html'
    sheet.write_text(page,newline='\n')
    print('Wrote',sheet)
    # Report the widest string each slot can hold before the runtime trims it.
    for index,(literal,capacity) in sorted(strings(canvas).items()):
        print(f'  element {index:2}: capacity {capacity:3}  literal {literal!r}')

if __name__=='__main__':main()
