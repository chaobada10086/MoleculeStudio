from pathlib import Path
import sys
import unittest
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from chemistry_open import run


class HelmConversion(unittest.TestCase):
    mime = "chemical/x-helm"
    notation = "PEPTIDE1{A.G.C}$$$$"

    def test_cdxml_round_trip(self):
        document = run(self.notation, self.mime, "text/xml", "convert")
        self.assertEqual(run(document, "text/xml", self.mime, "convert"), self.notation)

    def test_reject_stale_polymer_after_edit(self):
        edited = run("PEPTIDE1{V.G.C}$$$$", self.mime, "text/xml", "convert")
        document = ET.fromstring(edited)
        document.find(".//n").set("MoleculeStudioHELM", self.notation)
        with self.assertRaisesRegex(ValueError, "不一致"):
            run(ET.tostring(document, encoding="unicode"), "text/xml", self.mime, "convert")


if __name__ == "__main__":
    unittest.main()
