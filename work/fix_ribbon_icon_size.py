from pathlib import Path
p=Path('studio/studio.cpp');s=p.read_text(encoding='utf-8')
old='                    RectF glyph(b.r.X+(b.r.Width-24)/2,b.r.Y+(b.r.Height-24)/2,24,24);'
assert s.count(old)==1
s=s.replace(old,'''                    // Hiding captions changes layout, not the tool's drawing scale.
                    const float side=b.iconOnly?24.f:(ribbonTab==0?17.f:18.f);
                    RectF glyph(b.r.X+(b.r.Width-side)/2,b.r.Y+(b.r.Height-side)/2,side,side);''')
old='                else icon(g,b.icon,RectF(b.r.X+(b.r.Width-17)/2,b.r.Y,17,17),c);'
assert s.count(old)==1
s=s.replace(old,'                else {const float side=ribbonTab==0?17.f:18.f;icon(g,b.icon,RectF(b.r.X+(b.r.Width-side)/2,b.r.Y,side,side),c);}')
p.write_text(s,encoding='utf-8')
