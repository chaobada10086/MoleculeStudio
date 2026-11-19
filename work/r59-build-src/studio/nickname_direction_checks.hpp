#pragma once
// Connection-order regression: an abbreviation drawn at the end of a bond must
// touch that bond with the atom that actually connects it to the skeleton.
// Every spelling in nicknameform::pairs is drawn on both sides of a bond and the
// glyph order that reaches the canvas is compared with the expected spelling.
void App::groupDirectionCheck(const std::string& dir){
    using namespace r62;using namespace core;using namespace integration;
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};
    std::vector<std::string> problems;
    int cases=0,left=0,right=0;
    std::string samples[]={"OAc","AcO","CO2Me","OMe","NO2","B(OH)2","CF3","SO2Me"};
    for(const auto& pair:nicknameform::pairs){
        std::string forms[2]={pair.left,pair.right};
        bool hydride=false;for(auto form:forms){bool first=false;size_t chars=0;if(r62::integration::molecular::hydrideLabel(6,form,&first,&chars))hydride=true;}
        if(hydride)continue;
        for(int typed=0;typed<2;++typed){
            std::string label=forms[typed];
            for(int side=0;side<2;++side){
                Session s;s.doc.setDefaultFont("Arial");s.doc.setDefaultBondLength(30);ObjectId c,a,b;
                require(s.doc.addAtom(Vec2(0,0),6,&c).ok()&&s.doc.addAtom(Vec2(side?-30.0:30.0,0),6,&a).ok()&&s.doc.addBond(c,a,&b).ok(),"fixture");
                require(s.submit(nicknames::labelCommand(a,label),"label").ok(),"apply label");
                // The inline editor also mirrors the typed text into the core label
                // side table, which is what the renderer reads; do the same so a
                // label that is not in the abbreviation catalogue is drawn.
                if(s.doc.atomLabel(a).empty())s.doc.setAtomLabel(a,label);
                RingGeometry rings(s.doc);
                auto labels=molecular::labels(s.doc,s.glyphs,rings);
                if(labels.count(a)==0){
                    std::ostringstream why;why<<"drawn "<<label<<" side "<<side<<" stored="<<s.doc.atomLabel(a)<<" runs="<<studioatom::runs(s.doc,a).size()<<" labels="<<labels.size();
                    problems.push_back(why.str());continue;
                }
                // The renderer reorders glyph POSITIONS, not the glyph array, so
                // read the visual order by sorting the placed glyphs by x.
                std::vector<std::pair<double,uint32_t> > placed;
                for(const auto& glyph:labels.at(a).layout.glyphs){
                    double x=glyph.x;for(const auto& contour:glyph.contours)for(auto point:contour)x=std::min(x,point.x);
                    placed.push_back(std::make_pair(x,glyph.codepoint));
                }
                std::sort(placed.begin(),placed.end(),[](const std::pair<double,uint32_t>& l,const std::pair<double,uint32_t>& r){return l.first<r.first;});
                std::string drawn;for(const auto& glyph:placed)drawn+=char(glyph.second);
                auto expected=nicknameform::oriented(label,side!=0);
                if(drawn!=expected){std::ostringstream why;why<<"order typed "<<label<<" side "<<side<<" drew "<<drawn<<" expected "<<expected;problems.push_back(why.str());continue;}
                // The glyph that touches the bond must be the connecting atom:
                // the ink nearest the neighbouring atom has to be the expected
                // first (bond on the left) or last (bond on the right) character.
                const auto& glyphs=labels.at(a).layout.glyphs;
                if(glyphs.size()>1){
                    size_t nearest=0;double best=1e100;
                    for(size_t i=0;i<glyphs.size();++i){
                        double edge=1e100;
                        for(const auto& contour:glyphs[i].contours)for(auto point:contour){
                            double x=point.x+labels.at(a).layout.anchor.x;
                            edge=std::min(edge,side?-(x):x);
                        }
                        if(edge<best){best=edge;nearest=i;}
                    }
                    char wanted=side?expected.back():expected.front();
                    if(char(glyphs[nearest].codepoint)!=wanted){std::ostringstream why;why<<"contact "<<label<<" side "<<side<<" nearest "<<char(glyphs[nearest].codepoint)<<" wanted "<<wanted;problems.push_back(why.str());}
                }
                (void)cases;++cases;if(side)++right;else ++left;
                bool sample=false;for(auto name:samples)if(label==name)sample=true;
                if(sample&&typed==0){s.refresh();dragpreview::writeSvg(s.scene,dir+"/"+label+(side?"-left.svg":"-right.svg"));}
            }
        }
    }
    {std::ofstream report(dir+"/problems.txt");for(size_t i=0;i<problems.size()&&i<40;++i)report<<problems[i]<<"\n";report<<"total="<<problems.size()<<"\n";}
    if(!problems.empty())throw std::runtime_error("connection order: "+std::to_string(problems.size())+" cases, see problems.txt");
    for(const auto& pair:std::vector<std::pair<std::string,std::string>>{{"OH","HO"},{"CN","NC"}}){
        for(bool blocked:{false,true}){
            Document doc;ObjectId center,neighbour,bondId;
            doc.addAtom(Vec2(0,0),6,&center);
            doc.addAtom(Vec2(2,-30),6,&neighbour);
            doc.addBond(center,neighbour,&bondId);
            doc.setAtomLabel(center,pair.second);
            if(blocked){ObjectId blocker;doc.addAtom(Vec2(11,0),8,&blocker);}
            RingGeometry rings(doc);
            auto labels=molecular::labels(doc,s.glyphs,rings);
            const auto& glyphs=labels.at(center).layout.glyphs;
            require(glyphs.size()==2,"two-letter group");
            const auto& leftmost=glyphs[glyphs[0].x<glyphs[1].x?0:1];
            const auto& wanted=blocked?pair.second:pair.first;
            require(leftmost.codepoint==uint32_t(wanted[0]),"prefer the right side of a near-vertical bond unless another label blocks it");
        }
    }
    for(int element:{6,8}){
        Document doc;ObjectId oxygen,center,left,right,edge;
        doc.addAtom(Vec2(0,0),element,&oxygen);
        doc.addAtom(Vec2(0,30),6,&center);
        doc.addAtom(Vec2(-26,45),6,&left);
        doc.addAtom(Vec2(26,45),6,&right);
        doc.addBond(oxygen,center,&edge);doc.addBond(center,left,&edge);doc.addBond(center,right,&edge);
        // Old files can store either spelling with a right-justified native label.
        auto& labelAttributes=doc.extensions(ObjectRef(ObjectKind::Atom,oxygen));
        labelAttributes.set("studio.label.offset","3 4");
        labelAttributes.set("studio.label.text","HO");
        labelAttributes.set("studio.text.LabelJustification","Right");
        doc.setAtomLabel(oxygen,"HO");
        RingGeometry rings(doc);
        auto labels=molecular::labels(doc,s.glyphs,rings);
        const auto& layout=labels.at(oxygen).layout;
        const auto& glyphs=layout.glyphs;
        require(glyphs.size()==2&&glyphs[0].codepoint=='H'&&glyphs[1].codepoint=='O'&&glyphs[0].x>glyphs[1].x,
                "H is to the right of O above a vertical bond, including old label layout");
        require(std::abs(layout.anchor.x+glyphs[1].x+glyphs[1].advance*.5)<1.5,
                "the vertical bond is aligned to O");
    }
    // Formula typography of the completed label.
    struct Case{const char* typed;int segments;int chargeSegment;const char* expectTail;};
    require(chemcompletion::merged(chemcompletion::segments("SO42-"))=="SO42-","merged text round-trips");
    {auto pieces=chemcompletion::segments("SO42-");require(pieces.size()==3,"sulfate has three pieces");
     require(pieces[0].text=="SO"&&pieces[0].script==0,"sulfate base");
     require(pieces[1].text=="4"&&pieces[1].script==-1,"sulfate count is a subscript");
     require(pieces[2].text=="2-"&&pieces[2].script==1,"sulfate charge is a superscript");}
    {auto pieces=chemcompletion::segments("Fe3+");require(pieces.size()==2&&pieces[0].text=="Fe"&&pieces[1].text=="3+"&&pieces[1].script==1,"iron(III) charge");}
    {auto pieces=chemcompletion::segments("NH4+");require(pieces.size()==3&&pieces[1].text=="4"&&pieces[1].script==-1&&pieces[2].text=="+"&&pieces[2].script==1,"ammonium");}
    {auto pieces=chemcompletion::segments("Ph+*");require(pieces.size()==2&&pieces[1].script==1&&pieces[1].text.find("+")!=std::string::npos&&pieces[1].text.size()>2,"radical cation");}
    {auto pieces=chemcompletion::segments("M-*");require(pieces.size()==2&&pieces[1].text.size()>2,"radical anion");}
    {auto pieces=chemcompletion::segments("1-Naph");require(pieces.size()==2&&pieces[0].script==0&&pieces[1].script==0,"locant stays on the baseline");}
    {auto pieces=chemcompletion::segments("13CH4");require(pieces.size()==3&&pieces[0].text=="13"&&pieces[0].script==1&&pieces[1].text=="CH"&&pieces[2].text=="4"&&pieces[2].script==-1,"isotope prefix goes up");}
    {auto pieces=chemcompletion::segments("CuSO4*5H2O");require(pieces.size()>=2&&pieces[0].script==0,"hydrate dot is not a charge");}
    s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);
    std::ofstream(dir+"/group-direction-checks.json")<<"{\"pairs\":"<<(sizeof(nicknameform::pairs)/sizeof(nicknameform::pairs[0]))<<",\"cases\":"<<cases<<",\"left_side\":"<<left<<",\"right_side\":"<<right<<",\"connection_at_bond\":true,\"formula_typography\":true}";
}
