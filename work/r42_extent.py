from pathlib import Path
p=Path('studio/engine/catalytic.hpp');s=p.read_text();a=s.index('inline double extent(');b=s.index('inline Vec2 point(',a)
s=s[:a]+'''inline double extent(const Document& d,ObjectId id){
    auto refs=component(d,id);auto box=selection::selectionBounds(d,refs);
    // Commands may change geometry before the document revision advances.
    // Measure current labels rather than using the revision-keyed anchor cache.
    static std::unique_ptr<text::GlyphProvider> provider=text::createGdiGlyphProvider("Arial","Microsoft YaHei");static text::GlyphCache glyphs(provider.get());
    integration::RingGeometry rings(d);auto labels=integration::molecular::labels(d,glyphs,rings);
    for(auto ref:refs)if(ref.kind==ObjectKind::Atom){auto i=labels.find(ref.id);if(i!=labels.end()){auto ink=i->second.layout.metrics.inkBox;if(!ink.empty()){box.add(ink.min);box.add(ink.max);}}}
    auto c=center(d,id);return std::hypot(std::max(std::abs(box.min.x-c.x),std::abs(box.max.x-c.x)),std::max(std::abs(box.min.y-c.y),std::abs(box.max.y-c.y)))+8;
}
''' + s[b:];p.write_text(s)
