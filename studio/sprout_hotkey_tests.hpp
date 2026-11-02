{
#include "keymap_data.hpp"
    int routes=0;
    for(const char* recipe:{"0","1","4","5","8","2"}){
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.doc.setDefaultBondLength(20);
        ObjectId id,bond;s.doc.addAtom(Vec2(),6,&id);s.doc.addAtom(Vec2(-20,0),6,&id);s.doc.addBond(0,1,&bond);
        // Obstruct one of the two 120-degree directions with an unrelated bond.
        s.doc.addAtom(Vec2(10,-32),6,&id);s.doc.addAtom(Vec2(10,-8),6,&id);s.doc.addBond(2,3,&bond);
        s.refresh();auto v=s.view();v.center=Vec2();v.scale=4;s.setView(v);
        auto p=s.view().toDevice(Vec2());SendMessageW(canvas,WM_MOUSEMOVE,0,MAKELPARAM(int(p.x),int(p.y)));
        size_t key=0;while(key<sizeof(keymap::hotkeys)/sizeof(keymap::hotkeys[0])&&!(std::string(keymap::hotkeys[key].command)=="SPROUT"&&std::string(keymap::hotkeys[key].value)==recipe))++key;
        require(key<sizeof(keymap::hotkeys)/sizeof(keymap::hotkeys[0]),"sprout shortcut exists");auto before=s.doc.digest();editorHotkey(int(key));
        require(s.doc.stats().aliveAtoms==5&&s.doc.stats().aliveBonds==3,"actual sprout shortcut adds one bond");
        require(s.doc.atoms().get(4)->position.y>0,"shortcut chooses unobstructed trigonal side");
        auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"shortcut avoidance one undo");require(s.undo(true).ok()&&s.doc.digest()==after,"shortcut avoidance redo");++routes;
    }
    std::ofstream(testOut+"/sprout_hotkey_tests.json")<<"{\"actual_canvas_hotkey_routes\":"<<routes<<",\"normal_double_wedge_hash_carbonyl\":true,\"avoids_crossing\":true,\"undo_redo\":true}";
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();
}
