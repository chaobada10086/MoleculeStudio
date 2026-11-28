"""Authorized attachment source, visual inspection and OCSR dispatch regressions (no neural model/API)."""
from pathlib import Path
import os,sys,tempfile,unittest,xml.etree.ElementTree as ET
from unittest.mock import patch
from contextlib import nullcontext
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import ai_assistant as ai
from ai_plugins import draw_plugin
from ai_attachments import Attachments,PDF_TOOL
import ai_local_sources as sources
import ai_ocsr_bridge as bridge
from PIL import Image
class LocalSources(unittest.TestCase):
 @classmethod
 def setUpClass(cls):draw_plugin.install_plugin(vars(ai))
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.image=self.root/'image 空格.png';self.pdf=self.root/'paper.pdf'
  im=Image.new('RGB',(80,60),'white');im.save(self.image);im.save(self.pdf,save_all=True,append_images=[im]);im.close()
  request=ET.Element('request')
  for path in (self.image,self.pdf):ET.SubElement(request,'image',path=str(path))
  self.a=Attachments(request);self.a.workspace=self.root
 def tearDown(self):self.tmp.cleanup()
 def engine(self):
  e=ai.DrawingTools({});e.attachments=self.a;return e
 def test_absolute_relative_and_attachment(self):
  for path in (str(self.image),self.image.name):
   with self.assertRaises(ValueError):sources.resolve_source(self.a,{'path':path})
  r=ET.Element('request');ET.SubElement(r,'image',path=str(self.image));a=Attachments(r)
  self.assertEqual(sources.resolve_source(a,{'attachment':1})[1]['path'],self.image)
 def test_pdf_page_and_real_visual_blocks(self):
  for args in ({'attachment':2,'pages':[2]},{'attachment':1,'pages':[1]}):
   value,blocks=self.a.read_pages(args);self.assertTrue(any(b['type']=='image_url' for b in blocks));self.assertEqual(value['pages'],args['pages'])
 def test_bmp_tiff_source_and_visual_preview(self):
  for suffix in ('.bmp','.tiff'):
   path=self.root/('preview'+suffix)
   with Image.new('RGB',(24,24),'white') as image:image.save(path)
   request=ET.Element('request');ET.SubElement(request,'image',path=str(path));a=Attachments(request)
   value,blocks=a.read_pages({'attachment':1,'pages':[1]})
   self.assertTrue(blocks[1]['image_url']['url'].startswith('data:image/jpeg;base64,'))
 def test_invalid_paths_pages_and_source_choice(self):
  bad=self.root/'bad.txt';bad.write_text('hello')
  for args in ({},{'path':str(self.image),'attachment':1},{'path':'missing.png'},{'path':str(bad)},{'path':'https://example.com/a.png'},{'attachment':2,'page':3},{'attachment':1,'page':2},{'attachment':2,'page':True},{'attachment':False}):
   with self.subTest(args=args),self.assertRaises(ValueError):sources.resolve_source(self.a,args)
 def test_three_tools_dispatch_attachments(self):
  root=ET.fromstring('<CDXML BondLength="14.4"><page id="1"><fragment id="2"><n id="3" p="0 0" Element="6"/><n id="4" p="14.4 0" Element="8"/><b id="5" B="3" E="4" Order="1"/></fragment></page></CDXML>')
  requests=[]
  def request(args):requests.append(args);return ET.Element('response',{'insertable':'1'})
  detect={'method':'test','dpi':300,'page_size_px':[80,60],'count':1,'regions':[{'crop':[0,0,1,1],'score':1,'bbox_pt':[0,0,80,60]}],'preview':'','warnings':[]}
  with patch.object(bridge,'service_session',return_value=nullcontext(request)),patch.object(bridge,'_detect',return_value=detect),patch.object(bridge,'_recognize',return_value=ET.Element('response',{'insertable':'1'})),patch.object(bridge,'_load_cdxml',return_value=(root,list(root.iter('n')))):
   self.assertEqual(self.engine().call('detect_structure_regions',{'attachment':1})['count'],1)
   e=self.engine();r=e.call('recognize_regions',{'attachment':2,'page':2,'stage':False,'regions':[[0,0,1,1]]});self.assertEqual(r['recognized'],1);self.assertFalse(e.actions)
   self.assertTrue(self.engine().call('recognize_and_draw_structure',{'attachment':1})['staged'])
   self.assertTrue(any(r.get('file')==str(self.pdf) and r.get('page')=='2' for r in requests))
 def test_quality_gate_still_rejects(self):
  e=self.engine()
  with patch.object(bridge,'service_session',return_value=nullcontext(lambda a:ET.Element('response'))),patch.object(bridge,'_recognize',return_value=ET.Element('response',{'insertable':'0'})),self.assertRaisesRegex(ValueError,'质量门'):
   e.call('recognize_and_draw_structure',{'attachment':1})
  self.assertFalse(e.actions)
 def test_advanced_returns_actual_preview_files(self):
  r=ai._advanced_execute(self.engine(),[{'tool':'read_pdf_pages','args':{'attachment':2,'pages':[2]}}],3,None,self.a)
  self.assertTrue(r[0]['ok']);import json
  result=json.JSONDecoder().raw_decode(r[0]['result'])[0];self.assertTrue(Path(result['preview_images'][0]).is_file())
 def test_schemas_require_attachment_numbers(self):
  for t in ai.TOOLS:
   if t['function']['name'] in ('detect_structure_regions','recognize_regions','recognize_and_draw_structure'):
    self.assertNotIn('path',t['function']['parameters']['properties']);self.assertIn('attachment',t['function']['parameters']['required'])
  self.assertNotIn('path',PDF_TOOL['function']['parameters']['properties'])
  text=Path(ai.__file__).read_text(encoding='utf-8');self.assertNotIn('if attachments.pdfs else',text)
if __name__=='__main__':unittest.main(verbosity=2)
