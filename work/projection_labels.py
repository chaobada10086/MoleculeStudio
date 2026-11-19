from pathlib import Path
p=Path('studio/engine/molecular_render.hpp');s=p.read_text(encoding='utf-8');s='#include "engine/projection_molecule.hpp"\n'+s;needle='        Vec2 top=baseline-Vec2(0,measured.metrics.firstBaseline);';assert needle in s
s=s.replace(needle,'''        // Projection labels sit outside their spokes, with the same geometry as
        // the original captions, but remain native atom labels for all editing.
        if(doc.incidentBonds(a.id).size()==1){auto edge=doc.bonds().alive(doc.incidentBonds(a.id).front());auto other=edge->beginAtom==a.id?edge->endAtom:edge->beginAtom;
            if(projection_molecule::kind(doc,other)>=0&&projection_molecule::kind(doc,other)<3){auto ray=a.position-doc.atoms().alive(other)->position;double span=length(ray);if(span>1e-8){ray=ray*(1/span);baseline=a.position+ray*(doc.defaultBondLength()*.2);baseline.y+=ray.y<-.65?0:ray.y>.65?size*.72:size*.32;baseline.x-=ray.x>.3?0:ray.x<-.3?measured.metrics.width:measured.metrics.width*.5;native=false;}}
        }
'''+needle);p.write_text(s,encoding='utf-8')
