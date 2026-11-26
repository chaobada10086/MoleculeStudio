"""Offline regressions for the native AI bridge; no API calls or real credentials."""
from pathlib import Path
import copy
import io
import json
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ai_assistant as ai
from ai_attachments import Attachments
from ai_ocsr_bridge import _resolve_source


class ReferenceRouting(unittest.TestCase):
    def test_imine_donors_point_into_scandium(self):
        root = ET.fromstring('<fragment><n id="m" Element="21"/><n id="n" Element="7"/>'
            '<n id="c1"/><n id="c2"/><n id="o" Element="8"/><n id="n2" Element="7"/>'
            '<b B="m" E="n"/><b B="n" E="c1" Order="2"/><b B="c2" E="n"/>'
            '<b B="m" E="o" Order="dative"/><b B="m" E="n2" Order="1"/></fragment>')
        ai.orient_coordination_bonds(root)
        self.assertEqual([(b.get("B"), b.get("E")) for b in root.findall('b[@Order="dative"]')],
                         [("n","m"), ("o","m")])
        self.assertEqual(root.findall("b")[-1].get("Order"), "1")

    def test_layout_draft_requires_same_indexed_graph(self):
        from ocsr_service import map_molfile_to_model
        atoms = [{"atom_symbol": symbol, "x": 0.1*i, "y": 0.5} for i, symbol in enumerate(("C","N"))]
        parsed = {"atoms": [{"element": n, "x": 20, "y": 30} for n in (6,7)],
                  "bonds": [{"begin": 0, "end": 1, "order": 1}]}
        bonds = [{"endpoint_atoms": [0,1], "bond_type": "single"}]
        args = (parsed, atoms, bonds, (100,100))
        with self.assertRaises(ValueError):
            map_molfile_to_model(*args)
        self.assertTrue(map_molfile_to_model(*args, preview=True)["preview_topology_only"])
        bonds[0]["bond_type"] = "double"
        with self.assertRaises(ValueError):
            map_molfile_to_model(*args, preview=True)

    def test_conversational_image_requests(self):
        request = ET.fromstring('<request><image kind="canvas"/><image path="sc.png"/></request>')
        for prompt in ("把这个也画出来", "这张图也画一下吧", "请帮我画一下", "画出这个结构", "绘制这个结构"):
            with self.subTest(prompt=prompt):
                self.assertEqual(ai.single_structure_upload(request, prompt), 2)
        for prompt in ("为什么没有生成绘图", "别把这个画出来", "画出反应式", "把这个也画出来并标出机理"):
            with self.subTest(prompt=prompt):
                self.assertIsNone(ai.single_structure_upload(request, prompt))
        ET.SubElement(request, "image", path="second.png")
        self.assertIsNone(ai.single_structure_upload(request, "把这个也画出来"))


class BaseTools(unittest.TestCase):
    def setUp(self):
        self.engine = ai.DrawingTools({'selected': 0, 'bond_length': 14.4, 'font': 'Arial'})

    def unchanged_on_error(self, name, args):
        before = copy.deepcopy((self.engine.actions, self.engine.scene, self.engine.context))
        serialized = lambda value: ([ET.tostring(n) for n in value[0]], value[1:])
        with self.assertRaises(ValueError): self.engine.call(name, args)
        self.assertEqual(serialized(before), serialized((self.engine.actions, self.engine.scene, self.engine.context)))

    def test_single_coordination_review_and_editable_fallback(self):
        self.engine.coordination_candidate = (
            '<CDXML><page><fragment><n id="10" p="0 0" Element="7"/>'
            '<n id="12" p="30 0" Element="50"/><b id="20" B="10" E="12" Order="1"/>'
            '</fragment></page></CDXML>')
        def response(content):
            return {"choices":[{"message":{"content":content}}]}
        for replies, expected, count, order in (
            ([response('{"atom_edits":[],"bond_edits":["1 2 dative"]}')],True,1,"dative"),
            ([response("")],False,1,"1"),
            ([response('{"uncertain":"键型"}')],False,1,"1"),
            ([response('{"atom_edits":[],"bond_edits":["1 99 1"]}')],False,1,"1"),
            ([ValueError("timeout")],False,1,"1")):
            with self.subTest(expected=expected,count=count), patch.object(ai,"request_json",side_effect=replies) as api:
                progress=[]
                before=len(self.engine.actions)
                reviewed,_,message=ai.review_coordination(self.engine,{"messages":[]},"endpoint","key",progress.append)
                self.assertEqual((reviewed,api.call_count,len(self.engine.actions)-before),(expected,count,1))
                self.assertEqual(len(progress),1)
                self.assertFalse(api.call_args.kwargs["retry"])
                if not reviewed:
                    self.assertIn("需要人工复核",message)
                self.assertEqual(ET.fromstring(self.engine.actions[-1].text).find(".//b").get("Order"),order)

    def test_coordination_edits_preserve_atom_ids_and_bond_direction(self):
        self.engine.coordination_candidate = (
            '<CDXML><page><fragment><n id="10" p="10 0" Element="50"/>'
            '<n id="15" p="40 0" Element="7" Charge="1" NumHydrogens="0"/>'
            '<b id="20" B="10" E="15" Order="1"/></fragment></page></CDXML>')
        self.unchanged_on_error("correct_coordination_candidate", {"atom_edits":[], "bond_edits":["3 1 dative"]})
        self.engine.call("correct_coordination_candidate",
                         {"atom_edits":["2 N 0 0","1 Sn 0 1"], "bond_edits":["2 1 dative"]})
        root = ET.fromstring(self.engine.actions[0].text)
        self.assertEqual(root.find(".//b").attrib, {"id":"20","B":"15","E":"10","Order":"dative"})
        self.assertEqual(root.find('.//n[@id="15"]').get("NumHydrogens"), "0")
        self.assertEqual(root.find('.//n[@id="10"]').get("MoleculeStudioLonePairs"), "1")

    def test_page_updates_keep_previous_values(self):
        self.engine.call('set_page_layout', {'width_mm': 300, 'height_mm': 400, 'across': 2})
        result = self.engine.call('set_page_layout', {'down': 3})['page']
        self.assertEqual(result, dict(width_mm=300, height_mm=400, across=2, down=3))
        self.assertEqual(self.engine.call('inspect_document', {})['page'], result)
        self.unchanged_on_error('set_page_layout', {'orientation': 'sideways'})

    def test_empty_selector_cannot_match_document(self):
        self.engine.call('add_text', {'x': 0, 'y': 0, 'text': 'keep'})
        self.unchanged_on_error('delete_objects', {'target': ''})

    def test_equal_numeric_endpoints(self):
        self.unchanged_on_error('draw_arrow', {'x': 0, 'y': 0, 'x2': 0.0, 'y2': 0.0, 'style': 'forward'})

    def test_xml_transport(self):
        for text in ('bad\0text', '\ud800', 'bad\x01'):
            self.unchanged_on_error('add_text', {'x': 0, 'y': 0, 'text': text})
        self.engine.call('add_text', {'x': 0, 'y': 0, 'text': '中文 < & > 😀\nOK'})
        ET.fromstring(ET.tostring(self.engine.actions[0]))

    def test_failed_edit_is_atomic(self):
        self.engine.call('add_text', {'x': 0, 'y': 0, 'text': 'keep'})
        # edit_text sets the implicit kind filter; invalid later options must roll back.
        self.unchanged_on_error('edit_text', {'target': 'created', 'text': 'changed', 'bold': 'yes'})

    def test_font_byte_limit(self):
        self.engine.call('add_text', {'x': 0, 'y': 0, 'text': 'keep'})
        self.unchanged_on_error('edit_text', {'target': 'created', 'font': '字'*70})

    def test_selection_shortcuts_update_scene(self):
        scene = ET.fromstring('<scene total="1"><object ref="text:1" kind="text" x="0" y="0" x2="20" y2="10" text="hello" selected="true"/></scene>')
        self.engine.context['selected'] = 1
        self.engine.load_scene(scene)
        self.engine.call('move_selection', {'dx': 30, 'dy': 20})
        item = self.engine.call('inspect_document', {'target': 'selection'})['objects'][0]
        self.assertEqual((item['x'], item['y']), (30, 20))
        self.engine.call('style_selection', {'color': '#FF0000'})
        self.assertEqual(self.engine.scene[0]['color'], '#FF0000')


class PluginTools(BaseTools):
    @classmethod
    def setUpClass(cls):
        from ai_plugins import draw_plugin
        draw_plugin.install_plugin(vars(ai))

    def test_plugin_rollback_restores_page_context(self):
        from ai_plugins import draw_plugin as plugin
        state = plugin._snapshot(self.engine)
        self.engine.call('set_page_layout', {'width_mm': 500})
        plugin._restore(self.engine, state)
        self.assertNotIn('page_width_mm', self.engine.context)


def blank_pdf(path):
    objects = [b'<< /Type /Catalog /Pages 2 0 R >>',
               b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
               b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Resources << >> /Contents 4 0 R >>',
               b'<< /Length 0 >>\nstream\n\nendstream']
    data = bytearray(b'%PDF-1.4\n'); offsets = [0]
    for number, obj in enumerate(objects, 1):
        offsets.append(len(data)); data.extend(f'{number} 0 obj\n'.encode()+obj+b'\nendobj\n')
    start = len(data)
    data.extend(b'xref\n0 5\n0000000000 65535 f \n')
    for offset in offsets[1:]: data.extend(f'{offset:010} 00000 n \n'.encode())
    data.extend(f'trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n{start}\n%%EOF\n'.encode())
    path.write_bytes(data)


class ReferenceInk(unittest.TestCase):
    def test_local_ink_colors_do_not_spread_across_bonds(self):
        from PIL import Image, ImageDraw
        from ocsr_service import capture_structure_colors
        image = Image.new("RGB",(100,40),"white")
        draw = ImageDraw.Draw(image)
        draw.line((10,20,90,20),fill="black",width=2)
        draw.rectangle((8,17,13,23),fill="#008000")
        draw.rectangle((88,17,92,23),fill="#0000FF")
        graph = {"atoms":[{"x_px":x,"y_px":20} for x in (10,50,90)],
                 "bonds":[{"begin":i,"end":i+1,"length_px":40} for i in (0,1)]}
        capture_structure_colors(graph,image)
        self.assertEqual([a.get("ink_rgb") for a in graph["atoms"]],[(0,128,0),None,(0,0,255)])
        self.assertTrue(all("ink_rgb" not in b for b in graph["bonds"]))


class AttachmentNumbers(unittest.TestCase):
    def test_mixed_image_pdf_numbers_and_page_bounds(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory); image = directory/'test.png'; pdf = directory/'test.pdf'
            Image.new('RGB', (10, 10), 'white').save(image); blank_pdf(pdf)
            request = ET.Element('request')
            for path in (image, pdf): ET.SubElement(request, 'image', {'path': str(path)})
            attachments = Attachments(request)
            result, blocks = attachments.read_pages({'attachment': 2, 'pages': [1]})
            self.assertEqual(result['attachment'], 2); self.assertTrue(blocks)
            image_page, blocks = attachments.read_pages({'attachment': 1, 'pages': [1]})
            self.assertEqual(image_page['attachment'], 1)
            self.assertEqual(blocks[-1]['type'], 'image_url')
            with self.assertRaises(ValueError): attachments.read_pages({'attachment': 1, 'pages': [2]})
            self.assertEqual(_resolve_source(attachments, {'attachment': 2, 'page': 1})[0], 2)
            with self.assertRaises(ValueError): _resolve_source(attachments, {'attachment': 2, 'page': 2})


class AdvancedExecution(unittest.TestCase):
    def engine(self): return ai.DrawingTools({'selected': 0, 'bond_length': 14.4})

    def test_advanced_can_delegate_original_task_before_drawing(self):
        request = ET.fromstring('<request advanced="1"><key>offline</key><model>offline</model><prompt>在左边写字</prompt></request>')
        response = {"choices":[{"message":{"role":"assistant","tool_calls":[{"id":"a","function":{"name":"add_text","arguments":json.dumps({"x":0,"y":0,"text":"左"})}}]}}]}
        with patch('ai_dsh.available',return_value=True), patch('ai_dsh.run_task',return_value='{"plan":[],"workflow":"basic"}') as planner, patch.object(ai,'request_json',side_effect=[response,{"choices":[{"message":{"content":"完成"}}]}]):
            result = ai.run(request)
        self.assertEqual(planner.call_count,1)
        self.assertEqual([action.tag for action in result.find("actions")],["add_text"])

    def test_image_only_advanced_uses_reference_workflow(self):
        request = ET.fromstring('<request advanced="1"><key>offline</key><image kind="upload" path="sample.png"/><image kind="canvas" path="canvas.png"/></request>')
        with patch.object(ai,"run_basic",return_value="shared") as basic, patch("ai_dsh.run_task",side_effect=AssertionError):
            self.assertEqual(ai.run(request),"shared")
        self.assertIs(basic.call_args.args[0],request)
        self.assertEqual(request.findtext("prompt"),"绘制这个结构")

    def test_failures_cannot_report_success_without_actions(self):
        engine = self.engine()
        runs = ai._advanced_execute(engine, [{'tool': 'invented', 'args': {}}], 40, None)
        result = ai._advanced_result(None, engine, [], '已完成', runs)
        self.assertEqual(result.get('ok'), 'false'); self.assertIn('失败', result.findtext('message'))

    def test_partial_actions_and_limit_are_visible(self):
        engine = self.engine()
        runs = ai._advanced_execute(engine, [{'tool': 'add_text', 'args': {'x': 0, 'y': 0, 'text': 'kept'}}]*2, 1, None)
        result = ai._advanced_result(None, engine, [], '已完成', runs)
        self.assertEqual(len(result.find('actions')), 1)
        self.assertEqual(result.get('partial'), 'true'); self.assertIn('上限', result.findtext('message'))

    def test_sources_and_ocsr_notice_are_retained(self):
        engine = self.engine(); engine.sources = [{'cid': 123, 'smiles': 'CC', 'source': 'example'}]; engine._ocsr_staged = True
        result = ai._advanced_result(None, engine, [], 'test')
        self.assertEqual(result.find('source').get('cid'), '123')
        self.assertNotIn('smiles', result.find('source').attrib)
        self.assertIn('核对', result.findtext('message'))

    def test_advanced_planning_keeps_chat_preferences(self):
        request = ET.fromstring('<request><key>fake-offline-key</key><model>offline</model><prompt>继续绘图</prompt><preset>use blue</preset><history><message role="user">earlier step</message></history></request>')
        tasks = []
        def dsh(task, *args, **kwargs):
            tasks.append(task)
            return json.dumps({'plan': [{'tool': 'add_text', 'args': {'x': 0, 'y': 0, 'text': 'ok'}}]}) if len(tasks) == 1 else '{"fixes":[],"done":false}'
        with patch('ai_dsh.available', return_value=True), patch('ai_dsh.run_task', side_effect=dsh), patch('ai_assistant._stage_attachments', return_value=[]):
            result = ai.run_advanced(request)
        self.assertIn('use blue', tasks[0]); self.assertIn('earlier step', tasks[0])
        self.assertEqual(result.get('partial'), 'true'); self.assertIn('尚未确认', result.findtext('message'))

    def test_secret_redaction(self):
        with patch.object(ai, '_ACTIVE_API_KEY', 'offline-secret'):
            self.assertEqual(ai.redact_secret('failed offline-secret'), 'failed [redacted]')

    def test_cli_failure_diagnostics_redact_key(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/'result.xml'
            with patch.object(sys, 'argv', ['ai_assistant.py', str(output)]), patch.object(sys, 'stdin') as stdin, patch.object(ai, '_ACTIVE_API_KEY', 'offline-secret'), patch.object(ai, 'run', side_effect=ValueError('error offline-secret')):
                stdin.buffer = io.BytesIO(b'<request/>'); ai.main()
            self.assertNotIn('offline-secret', output.read_text())
            self.assertNotIn('offline-secret', (output.parent/'last-error.log').read_text())


class ProxyProtocol(unittest.TestCase):
    def adapter(self):
        from dsh_proxy import _Adapter
        return _Adapter('https://offline.invalid/v1', 'fake-key', 'offline', '')

    def test_tool_result_images_reach_model(self):
        request = {'messages': [{'role': 'user', 'content': [{'type': 'tool_result', 'tool_use_id': 'one', 'content': [
            {'type': 'text', 'text': 'page'}, {'type': 'image', 'source': {'type': 'base64', 'media_type': 'image/png', 'data': 'AAAA'}}]}]}]}
        messages = self.adapter()._to_openai(request)['messages']
        self.assertEqual([m['role'] for m in messages], ['tool', 'user'])
        self.assertEqual(messages[1]['content'][0]['image_url']['url'], 'data:image/png;base64,AAAA')

    def test_tool_choice_none(self):
        payload = self.adapter()._to_openai({'tools': [{'name': 'test'}], 'tool_choice': {'type': 'none'}})
        self.assertEqual(payload['tool_choice'], 'none')

    def test_parallel_streams_have_sequential_blocks(self):
        adapter = self.adapter()
        chunks = [
            {'choices': [{'delta': {'tool_calls': [
                {'index': 0, 'id': 'a', 'function': {'name': 'first', 'arguments': '{"x":'}},
                {'index': 1, 'id': 'b', 'function': {'name': 'second', 'arguments': '{"y":'}}]}}]},
            {'choices': [{'delta': {'tool_calls': [
                {'index': 0, 'function': {'arguments': '1}'}},
                {'index': 1, 'function': {'arguments': '2}'}}]}, 'finish_reason': 'tool_calls'}]},
        ]
        response = io.BytesIO(b''.join(b'data: '+json.dumps(chunk).encode()+b'\n\n' for chunk in chunks)+b'data: [DONE]\n\n')
        events = []
        with patch.object(adapter, '_post', return_value=response):
            adapter.stream({'stream': True}, lambda name, item: events.append((name, item)))
        active = None; inputs = {}; names = []
        for name, item in events:
            if name == 'content_block_start':
                self.assertIsNone(active); active = item['index']; inputs[active] = ''; names.append(item['content_block']['name'])
            elif name == 'content_block_delta':
                self.assertEqual(item['index'], active); inputs[active] += item['delta'].get('partial_json', '')
            elif name == 'content_block_stop':
                self.assertEqual(item['index'], active); active = None
        self.assertEqual(names, ['first', 'second'])
        self.assertEqual([json.loads(value) for value in inputs.values()], [{'x': 1}, {'y': 2}])


if __name__ == '__main__': unittest.main(verbosity=2)
