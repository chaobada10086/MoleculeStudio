"""Mapping failures must stop recognition instead of allowing an unsafe preview."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('ocsr_service', Path(__file__).resolve().parents[1] / 'studio/ocsr_service.py')
service = importlib.util.module_from_spec(spec)
spec.loader.exec_module(service)

class MappingRejectionTest(unittest.TestCase):
    def test_excessive_distance_stops_recognition(self):
        parsed = {'atoms': [{'x': 25.248, 'y': 1.0}], 'bonds': []}
        with patch.object(service, '_atom_compatible', return_value=True):
            with self.assertRaisesRegex(ValueError, '偏差过大'):
                service.map_molfile_to_model(parsed, [{'x': 0.0, 'y': 0.0}], [], (100,100))

    def test_inconsistent_topology_stops_recognition(self):
        parsed = {'atoms': [{'x': 0, 'y': 1}, {'x': 8, 'y': 1}], 'bonds': []}
        with patch.object(service, '_atom_compatible', return_value=True):
            with self.assertRaisesRegex(ValueError, '键连接关系'):
                service.map_molfile_to_model(parsed, [{'x': 0, 'y': 0}, {'x': .8, 'y': 0}], [{'endpoint_atoms': [0,1]}], (100,100))

if __name__ == '__main__':
    unittest.main()
