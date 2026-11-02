"""Portable OCSR startup, diagnostics and Molfile safety regressions."""
from pathlib import Path
import os
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ai_ocsr_bridge as bridge
import ocsr_service as service
from rdkit import Chem

class OcsrRuntime(unittest.TestCase):
    def make_runtime(self, root):
        runtime = root/'portable 带空格/runtime'
        (runtime/'venv/Scripts').mkdir(parents=True)
        (runtime/'venv/Scripts/python.exe').touch()
        base = runtime/'python/cpython-3.10-windows-x86_64-none'
        (base/'Lib/encodings').mkdir(parents=True)
        (base/'python.exe').touch(); (base/'Lib/encodings/__init__.py').touch()
        (runtime/'venv/pyvenv.cfg').write_text('home = C:\\deleted\\old-version\ninclude-system-site-packages = false\n', encoding='utf-8')
        return runtime, base

    def test_moved_venv_home_is_repaired(self):
        with tempfile.TemporaryDirectory() as directory:
            runtime, base = self.make_runtime(Path(directory))
            python = bridge.prepare_runtime(runtime)
            self.assertEqual(python, runtime/'venv/Scripts/python.exe')
            text = (runtime/'venv/pyvenv.cfg').read_text(encoding='utf-8')
            self.assertIn('home = '+str(base.resolve()), text)
            self.assertIn('include-system-site-packages = false', text)
            with patch.object(Path, 'replace', side_effect=AssertionError('unnecessary rewrite')):
                bridge.prepare_runtime(runtime)

    def test_missing_stdlib_has_explicit_error(self):
        with tempfile.TemporaryDirectory() as directory:
            runtime, base = self.make_runtime(Path(directory)); (base/'Lib/encodings/__init__.py').unlink()
            with self.assertRaisesRegex(ValueError, 'encodings'): bridge.prepare_runtime(runtime)

    def test_startup_exit_reports_real_stderr_and_code(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'ocsr_service.py').write_text('import sys\nsys.stderr.write("missing_test_dependency\\n")\nsys.exit(7)\n', encoding='utf-8')
            with patch.object(bridge, 'prepare_runtime', return_value=Path(sys.executable)):
                with self.assertRaises(bridge.OcsrServiceError) as error:
                    with bridge.service_session(root): pass
            self.assertIn('missing_test_dependency', str(error.exception))
            self.assertEqual(error.exception.diagnostics['exit_code'], 7)
            self.assertEqual(error.exception.diagnostics['stage'], 'service_startup')
            self.assertIn('missing_test_dependency', (root/'OCSRCache/last-ai-service-error.log').read_text())

    def test_startup_handshake_and_python_environment_isolation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)/'应用 带空格'; root.mkdir()
            (root/'ocsr_service.py').write_text('import sys,os,xml.etree.ElementTree as E\nassert not os.environ.get("PYTHONHOME")\nassert not os.environ.get("PYTHONPATH")\nfor line in sys.stdin:\n r=E.fromstring(line);print(E.tostring(E.Element("response",{"ok":"1","id":r.get("id",""),"pong":"1"}),encoding="unicode"),flush=True)\n', encoding='utf-8')
            with patch.object(bridge, 'prepare_runtime', return_value=Path(sys.executable)), patch.dict(os.environ, {'PYTHONHOME':'C:\\wrong', 'PYTHONPATH':'C:\\wrong'}):
                with bridge.service_session(root) as request:
                    response = request({'op':'ping','id':'real-after-startup'})
                    self.assertEqual(response.get('id'), 'real-after-startup')

    def test_plain_v3000_conversion_preserves_stereo(self):
        for smiles in ('CCO', 'F/C=C/F', 'F/C=C\\F', 'C[C@H](O)/C=C/C'):
            mol = Chem.MolFromSmiles(smiles)
            parsed = service.parse_molblock(Chem.MolToMolBlock(mol, forceV3000=True))
            self.assertEqual(Chem.MolToSmiles(parsed['rdkit_mol'], isomericSmiles=True), Chem.MolToSmiles(mol, isomericSmiles=True))
            self.assertEqual(parsed['sanitize_error'], '')

    def test_v3000_coordination_is_not_flattened(self):
        mol = Chem.MolFromSmiles('[NH3]->[Cu+2]')
        with self.assertRaisesRegex(ValueError, '配位/查询键'):
            service.parse_molblock(Chem.MolToMolBlock(mol, forceV3000=True))

    def test_invalid_valence_keeps_rejection_without_hydrogen_cache_crash(self):
        rw = Chem.RWMol(); rw.AddAtom(Chem.Atom(6))
        for _ in range(5):
            atom = rw.AddAtom(Chem.Atom(9)); rw.AddBond(0, atom, Chem.BondType.SINGLE)
        mol = rw.GetMol(); mol.UpdatePropertyCache(strict=False)
        parsed = service.parse_molblock(Chem.MolToMolBlock(mol))
        self.assertTrue(parsed['sanitize_error'])
        self.assertEqual(len(parsed['atoms']), 6)

if __name__ == '__main__': unittest.main(verbosity=2)
