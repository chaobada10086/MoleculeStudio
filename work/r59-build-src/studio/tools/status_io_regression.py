"""Windows sharing-violation regressions for the advisory progress channel."""
from pathlib import Path
from contextlib import contextmanager
import ctypes
from ctypes import wintypes
import io
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ai_assistant as ai

@contextmanager
def reader(path, sharing):
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    api.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    api.CreateFileW.restype = wintypes.HANDLE
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = api.CreateFileW(str(path), 0x80000000, sharing, None, 3, 0x80, None)
    if handle == ctypes.c_void_p(-1).value: raise ctypes.WinError(ctypes.get_last_error())
    try: yield api, handle
    finally: api.CloseHandle(handle)

@unittest.skipUnless(sys.platform == 'win32', 'real Windows sharing modes required')
class StatusIO(unittest.TestCase):
    def test_legacy_lock_reproduces_error_but_fixed_writer_is_nonfatal(self):
        with tempfile.TemporaryDirectory() as directory:
            status = Path(directory)/'job.status'; status.write_text('previous', encoding='utf-8')
            with reader(status, 1):
                with self.assertRaises(PermissionError): status.write_text('old unsafe writer', encoding='utf-8')
                self.assertFalse(ai.write_progress_status(status, '绘图仍在执行'))
                self.assertEqual(status.read_text(encoding='utf-8'), 'previous')
            self.assertTrue(ai.write_progress_status(status, '绘图仍在执行'))
            self.assertEqual(status.read_text(encoding='utf-8'), '绘图仍在执行')
            self.assertFalse(status.with_name(status.name+'.tmp').exists())

    def test_shared_reader_keeps_complete_snapshot_during_update(self):
        with tempfile.TemporaryDirectory() as directory:
            status = Path(directory)/'job.status'; old = '完整旧进度'; status.write_text(old, encoding='utf-8')
            with reader(status, 1|2|4) as (api, handle):
                published = ai.write_progress_status(status, '新的完整进度')
                # Windows filesystems may still deny replacement while a handle
                # is open. Either publication or a skipped refresh is valid;
                # truncation and cancellation are never valid.
                self.assertEqual(status.read_text(encoding='utf-8'), '新的完整进度' if published else old)
                api.ReadFile.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
                api.ReadFile.restype = wintypes.BOOL
                buffer = ctypes.create_string_buffer(1024); count = wintypes.DWORD()
                self.assertTrue(api.ReadFile(handle, buffer, 1024, ctypes.byref(count), None))
                self.assertEqual(buffer.raw[:count.value].decode('utf-8'), old)
            self.assertTrue(ai.write_progress_status(status, '新的完整进度'))
            self.assertEqual(status.read_text(encoding='utf-8'), '新的完整进度')

    def test_transient_lock_is_retried(self):
        with tempfile.TemporaryDirectory() as directory:
            status = Path(directory)/'job.status'; original = Path.replace; attempts = []
            def replace(path, target):
                attempts.append(target)
                if len(attempts) < 3: raise PermissionError('transient sharing violation')
                return original(path, target)
            with patch.object(Path, 'replace', replace): self.assertTrue(ai.write_progress_status(status, 'ready'))
            self.assertEqual(len(attempts), 3)
            self.assertEqual(status.read_text(), 'ready')

    def test_status_storage_failure_is_nonfatal(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(Path, 'write_text', side_effect=OSError('status storage unavailable')):
                self.assertFalse(ai.write_progress_status(Path(directory)/'job.status', 'drawing'))

    def test_real_tool_execution_finishes_while_status_is_locked(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/'job.xml'; status = output.with_suffix('.status'); status.write_text('old')
            def run(request, progress):
                engine = ai.DrawingTools({'selected':0,'font':'Arial'})
                calls = [{'tool':'add_text','args':{'x':0,'y':0,'text':'kept despite status lock'}}]
                executions = ai._advanced_execute(engine, calls, 120, progress)
                return ai._advanced_result(request, engine, [], 'completed', executions)
            with reader(status, 1), patch.object(sys, 'argv', ['worker',str(output)]), patch.object(sys, 'stdin') as stdin, patch.object(ai, 'run', side_effect=run):
                stdin.buffer = io.BytesIO(b'<request/>'); ai.main()
            result = ET.parse(output).getroot()
            self.assertEqual(result.get('ok'), 'true')
            self.assertEqual(result.findtext('actions/add_text'), 'kept despite status lock')
            self.assertFalse((output.parent/'last-error.log').exists())

    def test_identical_failed_progress_is_retried_after_lock_releases(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/'job.xml'; status = output.with_suffix('.status'); status.write_text('old')
            def run(request, progress):
                with reader(status, 1): progress('same progress')
                progress('same progress'); progress('same progress')
                return ET.Element('result', {'ok':'true'})
            original = ai.write_progress_status
            with patch.object(sys, 'argv', ['worker',str(output)]), patch.object(sys, 'stdin') as stdin, patch.object(ai, 'run', side_effect=run), patch.object(ai, 'write_progress_status', wraps=original) as writer:
                stdin.buffer = io.BytesIO(b'<request/>'); ai.main()
            self.assertEqual(status.read_text(), 'same progress')
            self.assertEqual(writer.call_count, 2)

if __name__ == '__main__': unittest.main(verbosity=2)
