"""Offline self-test for the Molecule Studio drawing plugin.

Run with the bundled interpreter from the application folder:

    python\\python.exe ai_plugins\\selftest.py

It does not call an AI service or PubChem.  It exercises every plugin tool,
checks the generated action tags, validates every curated SMILES with the
bundled RDKit/CDXML path, and writes a sample action XML next to this file.
"""
from __future__ import annotations

import copy
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

import ai_assistant  # noqa: E402
from ai_plugins.draw_plugin import PLUGIN_NAMES, TEMPLATES  # noqa: E402
from chemistry_open import Chem, identity, one_molecule, to_cdxml  # noqa: E402
from rdkit import rdBase  # noqa: E402

ALLOWED_ACTION_TAGS = {
    "molecule", "draw_shape", "draw_arrow", "add_text",
    "style_selection", "move_selection", "style_objects", "move_objects",
    "transform_objects", "duplicate_objects", "delete_objects", "edit_text",
    "arrange_objects", "draw_path",
}


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def overlap_area(a, b):
    ix = min(a[2], b[2]) - max(a[0], b[0])
    iy = min(a[3], b[3]) - max(a[1], b[1])
    return max(0.0, ix) * max(0.0, iy)


def main():
    # The native exe normally supplies this declaration in the AI request.
    request = ET.Element("request")
    plugins = ET.SubElement(request, "plugins")
    ET.SubElement(plugins, "plugin", {
        "name": "绘图增强插件", "version": "1.2.0", "module": "ai_plugins",
        "entry": "install_plugin", "path": "ai_plugins",
    })
    loaded = ai_assistant.load_plugins(request)
    check(len(loaded) == 1 and loaded[0].get("ok") == "true", "plugin loader failed")
    check(loaded[0].get("tools") == str(len(PLUGIN_NAMES)), "plugin loader did not add all tools")
    tools = {t["function"]["name"] for t in ai_assistant.TOOLS}
    check(PLUGIN_NAMES <= tools, "plugin tools not registered")
    check("current" not in tools, "unexpected tool")

    # Validate every curated template through the real RDKit -> CDXML path.
    for name, smiles in TEMPLATES.items():
        mol = Chem.MolFromSmiles(smiles)
        check(mol is not None, "invalid template SMILES: " + name)
        xml = to_cdxml(mol)
        check(identity(one_molecule(xml, allow_disconnected=True)) == identity(mol),
              "template round-trip failed: " + name)

    engine = ai_assistant.DrawingTools({
        "selected": 0,
        "font": "Times New Roman",
        "bond_length": 14.4,
        "color": "#000000",
    })
    engine.load_scene(None)

    benzene = engine.call("draw_template", {"template": "benzene", "x": 0, "y": 0})
    check(benzene.get("ref"), "draw_template returned no ref")

    ethanol = engine.call("draw_smiles", {"smiles": "CCO"})
    check(ethanol.get("ref"), "draw_smiles returned no ref")
    check(ethanol["bounds"][1] >= benzene["bounds"][3] - 1,
          "auto placement overlaps previous structure")

    reaction = engine.call("draw_reaction", {
        "reactants": ["smiles:CC(=O)Cl", "smiles:c1ccccc1"],
        "products": ["smiles:CC(=O)c1ccccc1"],
        "conditions": "AlCl3, CH2Cl2, 0 °C",
        "reactant_labels": ["1", "2"],
        "product_labels": ["1a"],
        "x": 0,
        "y": 260,
    })
    check(len(reaction["reactants"]) == 2, "reaction reactants missing")
    check(len(reaction["products"]) == 1, "reaction products missing")
    check(reaction["reactants"][0]["label"] == "1", "custom reactant label missing")
    check(reaction["products"][0]["label"] == "1a", "custom product label missing")
    check(len(reaction["label_refs"]) == 3, "reaction label refs missing")
    check(len(reaction["plus_refs"]) == 1, "reaction plus ref missing")
    check(reaction["arrow"], "reaction arrow missing")
    check(reaction["conditions_ref"], "reaction conditions missing")
    for i in range(len(reaction["reactants"])):
        for j in range(i + 1, len(reaction["reactants"])):
            check(overlap_area(reaction["reactants"][i]["bounds"],
                               reaction["reactants"][j]["bounds"]) < 1,
                  "reactants overlap")
    for item in reaction["reactants"] + reaction["products"]:
        check(overlap_area(item["bounds"], reaction["products"][0]["bounds"]) < 1
              or item in reaction["products"], "products overlap reactants")

    curved = engine.call("draw_curved_arrow", {
        "x": 0, "y": 340, "x2": 70, "y2": 300,
        "bend": 0.45, "label": "e-", "color": "#0046DC",
    })
    check(curved["ref"] and curved["head_ref"], "curved arrow incomplete")

    caption = engine.call("add_caption", {
        "target": reaction["products"][0]["ref"],
        "text": "98%",
        "position": "below",
        "size": 10,
    })
    check(caption.get("ref"), "caption missing")

    third = engine.call("draw_template", {"template": "pyridine", "x": 380, "y": 260})
    aligned = engine.call("align_objects", {
        "refs": [benzene["ref"], ethanol["ref"]],
        "edge": "top",
    })
    check(aligned["moved"] >= 1, "align moved nothing")

    grid_source = engine.call("draw_template", {"template": "cyclopentane", "x": 0, "y": 430})
    repeated = engine.call("repeat_objects", {
        "target": grid_source["ref"], "rows": 2, "cols": 2,
        "gap_x": 20, "gap_y": 20,
    })
    check(len(repeated["refs"]) == 4, "repeat_objects count")
    check(len(set(repeated["refs"])) == 4, "repeat_objects duplicate refs")

    gridded = engine.call("layout_grid", {
        "refs": repeated["refs"], "columns": 2, "gap": 25,
    })
    check(gridded["moved"] >= 3, "layout_grid moved too little")

    distributed = engine.call("distribute_objects", {
        "refs": repeated["refs"], "direction": "row", "gap": 25,
    })
    check(distributed["gap"] >= 0, "distribute gap")

    report = engine.call("layout_report", {"include_ascii": True})
    check(report["group_count"] >= 5, "layout report groups")
    check(isinstance(report.get("ascii"), str) and "|" in report["ascii"],
          "layout report ascii")

    # A failed multi-object call must restore the staged actions and the local
    # scene, including coordinates already shifted for earlier items.
    before_actions = len(engine.actions)
    before_scene = copy.deepcopy(engine.scene)
    before_created = engine.created_count
    before_molecules = engine.molecules
    rdBase.DisableLog("rdApp.error")
    try:
        try:
            engine.call("draw_reaction", {
                "reactants": ["smiles:c1ccccc1", "smiles:NOT_A_SMILES"],
                "products": ["smiles:CCO"],
            })
        except ValueError:
            pass
        else:
            check(False, "invalid reaction did not fail")
    finally:
        rdBase.EnableLog("rdApp.error")
    check(len(engine.actions) == before_actions, "rollback actions mismatch")
    check(engine.scene == before_scene, "rollback scene mismatch")
    check(engine.created_count == before_created, "rollback handle counter mismatch")
    check(engine.molecules == before_molecules, "rollback molecule counter mismatch")

    # The plugin must only emit action tags the native host already accepts.
    for action in engine.actions:
        check(action.tag in ALLOWED_ACTION_TAGS,
              "plugin emitted an unsupported native action: " + action.tag)

    # Write a compact sample of the generated native action XML.
    result = ET.Element("result")
    actions = ET.SubElement(result, "actions")
    actions.extend(engine.actions)
    sample = Path(__file__).resolve().parent / "selftest-actions.xml"
    sample.write_text(ET.tostring(result, encoding="unicode"), encoding="utf-8")

    print("tools:", len(tools))
    print("plugin tools:", len(PLUGIN_NAMES))
    print("templates:", len(TEMPLATES))
    print("actions:", len(engine.actions), "tags:", sorted({a.tag for a in engine.actions}))
    print("layout groups:", report["group_count"])
    print("sample:", sample)
    print("OK")


if __name__ == "__main__":
    main()
