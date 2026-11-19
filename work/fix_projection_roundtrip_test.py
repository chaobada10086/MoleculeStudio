from pathlib import Path
p=Path('studio/projection_checks.hpp');s=p.read_text(encoding='utf-8')
s=s.replace('require(app.s.save(file,true).ok(),"save edited projection");Session reopened;require(reopened.open(file).ok(),"reopen edited projection");','''Session reopened;
            if(binary){std::string xml,bytes,decoded,why;require(studioio::exportXml(app.s.doc,&xml).ok()&&cdxbinary::fromXml(xml,bytes,why)&&cdxbinary::toXml(bytes,decoded,why),"edited CDX roundtrip");std::ofstream(file,std::ios::binary).write(bytes.data(),bytes.size());require(persistence::readCdxml(decoded,reopened.doc).ok(),"read edited CDX");}
            else{require(app.s.save(file,true).ok(),"save edited projection");require(reopened.open(file).ok(),"reopen edited projection");}''')
s=s.replace('            click(tip,true);require(app.textEditor.active&&fullText', '''            // CDXML export translates drawing coordinates onto its page.
            for(const auto& path:app.s.doc.paths().records())if(path.isAlive()&&!path.closed&&path.points.size()==2)tip=path.points.back();app.s.fit();
            click(tip,true);require(app.textEditor.active&&fullText''')
p.write_text(s,encoding='utf-8')
