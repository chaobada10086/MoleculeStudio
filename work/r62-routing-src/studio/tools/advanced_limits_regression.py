"""Offline checks for the shared 30-minute advanced-drawing budget."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import time
import unittest
import xml.etree.ElementTree as ET
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ai_assistant as ai
from ai_plugins import draw_plugin

class AdvancedLimits(unittest.TestCase):
    @classmethod
    def setUpClass(cls): draw_plugin.install_plugin(vars(ai))

    def test_both_phases_share_budget_and_accept_larger_plans(self):
        request = ET.fromstring('<request><key>offline</key><model>offline</model><prompt>test</prompt></request>')
        clock = [100.0]; calls = []
        action = {'tool': 'add_text', 'args': {'x': 0, 'y': 0, 'text': 'test'}}
        def dsh(task, *args, **kwargs):
            calls.append((task, kwargs['timeout']))
            if len(calls) == 1:
                clock[0] += 600
                return json.dumps({'plan': [action]*60})
            return json.dumps({'fixes': [action]*20, 'done': True})
        with patch('ai_dsh.available', return_value=True), patch('ai_dsh.run_task', side_effect=dsh), patch.object(ai, '_stage_attachments', return_value=[]), patch.object(ai.time, 'monotonic', side_effect=lambda: clock[0]):
            result = ai.run_advanced(request)
        self.assertEqual([timeout for _, timeout in calls], [1800, 1200])
        self.assertEqual(len(result.find('actions')), 80)
        self.assertIn('120 个调用', calls[0][0]); self.assertIn('48 个修正', calls[1][0])

    def test_deadline_stops_further_tool_calls(self):
        engine = ai.DrawingTools({'selected': 0})
        with patch.object(ai.time, 'monotonic', return_value=1801):
            runs = ai._advanced_execute(engine, [{'tool': 'add_text', 'args': {'x': 0, 'y': 0, 'text': 'late'}}], 120, None, deadline=1800)
        self.assertEqual(engine.actions, []); self.assertFalse(runs[0]['ok'])
        self.assertIn('时间上限', runs[0]['error'])

    def test_watchdog_exits_blocked_worker_and_writes_result(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/'result.xml'
            code = "import sys,time;from pathlib import Path;sys.path.insert(0,sys.argv[1]);import ai_assistant as a;a.advanced_watchdog(Path(sys.argv[2]),0.2);time.sleep(10)"
            start = time.monotonic()
            run = subprocess.run([sys.executable, '-c', code, str(Path(ai.__file__).parent), str(output)], timeout=5)
            self.assertEqual(run.returncode, 124); self.assertLess(time.monotonic()-start, 5)
            result = ET.parse(output).getroot(); self.assertEqual(result.get('ok'), 'false')
            self.assertIsNone(result.find('actions'))

    def test_larger_schemes_and_compound_batches(self):
        engine = ai.DrawingTools({'selected': 0, 'bond_length': 14.4})
        step = {'reactants': ['smiles:CCO'], 'products': ['smiles:CC=O']}
        result = engine.call('draw_scheme', {'steps': [step]*36})
        self.assertEqual(len(result['steps']), 36)
        with self.assertRaises(ValueError): engine.call('draw_scheme', {'steps': [step]*37})
        engine = ai.DrawingTools({'selected': 0, 'bond_length': 14.4})
        result = engine.call('draw_compounds', {'compounds': [{'spec': 'smiles:CCO'}]*72})
        self.assertTrue(result['staged'])
        with self.assertRaises(ValueError): engine.call('draw_compounds', {'compounds': [{'spec': 'smiles:CCO'}]*73})

if __name__ == '__main__': unittest.main(verbosity=2)
