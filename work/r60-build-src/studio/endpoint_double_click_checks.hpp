#pragma once
// A double-click on a bond endpoint is a label edit. With a bond tool active the
// first click of that double-click would otherwise sprout a bond/atom; the second
// click then edits the wrong object and leaves a stray atom behind. The shell
// retracts that one-click sprout before opening the editor.
static void endpointDoubleClickCheck(App& app, const std::string& dir) {
    auto require = [](bool ok, const char* why) { if (!ok) throw std::runtime_error(why); };
    auto& s = app.s;
    s.cancel(); s.doc = Document(); s.history.clear(); s.selected.clear(); s.input.bindDocument(s.doc.id());
    ObjectId ring[6], carbonyl, oxygen, methyl;
    const double R = 30.0;
    const double xs[6] = { R, R*.5, -R*.5, -R, -R*.5, R*.5 };
    const double ys[6] = { 0, R*.866, R*.866, 0, -R*.866, -R*.866 };
    for (int i = 0; i < 6; ++i) require(s.doc.addAtom(Vec2(xs[i], ys[i]), 6, &ring[i]).ok(), "ring atom");
    for (int i = 0; i < 6; ++i) { ObjectId b; require(s.doc.addBond(ring[i], ring[(i+1)%6], &b).ok(), "ring bond"); }
    require(s.doc.addAtom(Vec2(60, 0), 6, &carbonyl).ok(), "carbonyl atom");
    { ObjectId b; require(s.doc.addBond(ring[0], carbonyl, &b).ok(), "ring-carbonyl bond"); }
    require(s.doc.addAtom(Vec2(60, -30), 8, &oxygen).ok(), "oxygen atom");
    { ObjectId b; require(s.doc.addBond(carbonyl, oxygen, &b).ok(), "carbonyl bond"); require(s.doc.setBondOrder(b, BondOrder::Double).ok(), "carbonyl double"); }
    require(s.doc.addAtom(Vec2(60, 30), 6, &methyl).ok(), "methyl atom");
    { ObjectId b; require(s.doc.addBond(carbonyl, methyl, &b).ok(), "methyl bond"); }
    s.refresh(); s.fit();
    const size_t atomsBefore = s.doc.stats().aliveAtoms;
    const size_t bondsBefore = s.doc.stats().aliveBonds;
    require(s.activateCommand("chooseBondTool_Solid"), "bond tool active");
    const Vec2 pixel = s.view().toDevice(s.doc.atoms().get(methyl)->position);
    const LPARAM where = MAKELPARAM(int(pixel.x), int(pixel.y));
    SendMessageW(app.canvas, WM_LBUTTONDOWN, MK_LBUTTON, where);
    SendMessageW(app.canvas, WM_LBUTTONUP, 0, where);
    SendMessageW(app.canvas, WM_LBUTTONDBLCLK, MK_LBUTTON, where);
    require(app.textEditor.active, "double-click opens the endpoint label editor");
    require(s.doc.stats().aliveAtoms == atomsBefore, "double-click retracts the one-click sprout atom");
    require(s.doc.stats().aliveBonds == bondsBefore, "double-click retracts the one-click sprout bond");
    SetWindowTextW(app.edit, L"Ac");
    require(app.finishText(true), "label commit succeeds");
    require(s.doc.atomLabel(methyl) == "Ac", "label lands on the endpoint under the cursor");
    s.refresh();
    r62::integration::RingGeometry rings(s.doc);
    auto labels = r62::integration::molecular::labels(s.doc, s.glyphs, rings);
    require(labels.count(methyl) != 0, "label is rendered");
    const std::string path = dir + "/endpoint-double-click.cdxml";
    require(s.save(path, true).ok(), "save label");
    Session reload; require(reload.open(path).ok(), "reopen label");
    bool found = false;
    for (const auto& atom : reload.doc.atoms().records())
        if (atom.isAlive() && reload.doc.atomLabel(atom.id) == "Ac") found = true;
    require(found, "label survives save/reload");
    std::ofstream(dir + "/endpoint-double-click-check.json")
        << "{\"editor_opened\":true,\"sprout_retracted\":true,\"label_on_endpoint\":true,"
        << "\"rendered\":true,\"save_reload\":true}";
}