from pathlib import Path
p=Path('studio/catalytic_dialog.hpp');s=p.read_text();s=s.replace('    HFONT font=', '''    std::vector<CatalyticMetal> nodeMetals;std::vector<ObjectId> previewNodes;std::set<int> editedNodes;int selectedNode=0;bool loading=false;
    void loadMetal(){loading=true;auto set=[&](int id,std::string value){SetDlgItemTextW(window,id,W(value).c_str());};set(730,metal.element);set(731,metal.oxidation);set(732,metal.ligands);SendDlgItemMessageW(window,740,CB_SETCURSEL,metal.numerals,0);SendDlgItemMessageW(window,741,CB_SETCURSEL,metal.format,0);CheckDlgButton(window,742,metal.excited?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(window,743,metal.collapsed?BST_CHECKED:BST_UNCHECKED);loading=false;}
    HFONT font=''')
s=s.replace('if(nodes.empty())for(int i=0;i<int(count);++i)nodes.push_back(metal.apply(demo->doc,kInvalidObjectId,Vec2(i*60,0)));std::vector<ObjectRef> made;', '''if(nodeMetals.empty())nodeMetals.resize(nodes.empty()?int(count):nodes.size(),metal);
                if(nodes.empty()){nodeMetals.resize(int(count),metal);for(int i=0;i<int(count);++i)nodes.push_back(nodeMetals[i].apply(demo->doc,kInvalidObjectId,Vec2(i*60,0)));}
                else for(int i:editedNodes)if(i<int(nodes.size()))nodeMetals[i].apply(demo->doc,nodes[i],Vec2());
                previewNodes=nodes;std::vector<ObjectRef> made;''')
s=s.replace('if(event==EN_CHANGE||event==CBN_SELCHANGE||id==742||id==743){SetTimer', 'if(!d->loading&&(event==EN_CHANGE||event==CBN_SELCHANGE||id==742||id==743)){SetTimer')
s=s.replace('if(m==WM_TIMER&&w==32)', '''if(m==WM_LBUTTONDOWN&&d->cycle&&d->cyclePreview){
            KillTimer(h,32);d->read();if(d->selectedNode<int(d->nodeMetals.size())){d->nodeMetals[d->selectedNode]=d->metal;d->editedNodes.insert(d->selectedNode);}
            auto preview=d->cyclePreview;preview->prepare(d->px(410),d->px(225));auto point=preview->view.transform.toDocument(Vec2(GET_X_LPARAM(l)-d->px(450),GET_Y_LPARAM(l)-d->px(55)));
            double best=1e100;int selected=-1;for(size_t i=0;i<d->previewNodes.size();++i){double distance=length(point-studio::catalytic::center(preview->source->doc,d->previewNodes[i]));if(distance<best){best=distance;selected=int(i);}}
            if(selected>=0&&best<studio::catalytic::extent(preview->source->doc,d->previewNodes[selected])+10){d->selectedNode=selected;d->metal=d->nodeMetals[selected];d->loadMetal();d->rebuild();InvalidateRect(h,nullptr,FALSE);}return TRUE;
        }
        if(m==WM_TIMER&&w==32)''')
s=s.replace('d->read();d->rebuild();InvalidateRect', 'd->read();if(d->cycle&&d->selectedNode<int(d->nodeMetals.size())){d->nodeMetals[d->selectedNode]=d->metal;d->editedNodes.insert(d->selectedNode);}d->rebuild();InvalidateRect')
s=s.replace('if(id==IDOK){d->read();','if(id==IDOK){KillTimer(h,32);d->read();if(d->cycle&&d->selectedNode<int(d->nodeMetals.size())){d->nodeMetals[d->selectedNode]=d->metal;d->editedNodes.insert(d->selectedNode);}')
s=s.replace('if(d->cyclePreview)d->cyclePreview->paint(g,rect(450,55,410,225));', '''if(d->cyclePreview){d->cyclePreview->paint(g,rect(450,55,410,225));if(d->selectedNode<int(d->previewNodes.size())){auto p=d->cyclePreview->view.transform.toDevice(studio::catalytic::center(d->cyclePreview->source->doc,d->previewNodes[d->selectedNode]));Pen pen(Color(255,40,125,225),float(d->px(1)));g.DrawEllipse(&pen,float(d->px(450)+p.x-d->px(23)),float(d->px(55)+p.y-d->px(23)),float(d->px(46)),float(d->px(46)));}}txt(g,"单击配合物分别编辑 · 当前第 "+std::to_string(d->selectedNode+1)+" 个",rect(445,293,425,22),float(d->px(12)),muted);''')
p.write_text(s)
p=Path('studio/catalytic_ui.hpp');s=p.read_text();s=s.replace('f.selectedSource=std::make_shared<Session>();for(auto id:nodes)', 'for(auto id:nodes)f.nodeMetals.push_back(CatalyticMetal::read(s.doc,id));f.selectedSource=std::make_shared<Session>();for(auto id:nodes)')
# Apply configs before sorting nodes, preserving preview's component index.
s=s.replace('        Vec2 origin=s.view().center;', '        auto configuredNodes=nodes;\n        Vec2 origin=s.view().center;',1)
s=s.replace('auto metal=f.metal;auto sourceShape=', 'auto metal=f.metal;auto configs=f.nodeMetals;auto edited=f.editedNodes;auto sourceShape=')
s=s.replace('metal,inherited,sourceShape,rotation](CommandContext', 'metal,configs,edited,configuredNodes,inherited,sourceShape,rotation](CommandContext')
s=s.replace('nodes.push_back(metal.apply(c.document,kInvalidObjectId,origin+Vec2(i*40,0)));', 'nodes.push_back((i<int(configs.size())?configs[i]:metal).apply(c.document,kInvalidObjectId,origin+Vec2(i*40,0)));\n            else for(int i:edited)if(i<int(configuredNodes.size()))configs[i].apply(c.document,configuredNodes[i],origin);')
p.write_text(s)
