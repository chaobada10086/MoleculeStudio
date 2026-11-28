"""Offline boundary checks for the advanced drawing worker."""
import http.client
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ai_dsh
import dsh_proxy


class DrawingSessions(unittest.TestCase):
    def test_concurrent_and_abandoned_workspaces(self):
        with tempfile.TemporaryDirectory() as root, patch.dict(os.environ, LOCALAPPDATA=root):
            with ai_dsh.task_workspace() as first:
                with ai_dsh.task_workspace() as second:
                    self.assertNotEqual(first, second)
                    self.assertTrue(first.is_dir())
                self.assertFalse(second.exists())
                code = ("import os,sys;sys.path.insert(0,sys.argv[1]);import ai_dsh;"
                        "cm=ai_dsh.task_workspace();print(cm.__enter__(),flush=True);os._exit(0)")
                path = subprocess.check_output([sys.executable, "-c", code, str(Path(ai_dsh.__file__).parent)], text=True)
                abandoned = Path(path.strip())
                self.assertTrue(abandoned.exists())
                with ai_dsh.task_workspace():
                    self.assertFalse(abandoned.exists())
                    self.assertTrue(first.exists())
            self.assertFalse(first.exists())

    def test_proxy_requires_its_own_session_token(self):
        first = dsh_proxy.start_adapter("https://offline.invalid", "test-upstream", "test")
        second = dsh_proxy.start_adapter("https://offline.invalid", "test-upstream", "test")
        self.addCleanup(first.close)
        self.addCleanup(second.close)
        for headers, expected in [
            ({}, 401), ({"x-api-key": second.session_token}, 401),
            ({"x-api-key": first.session_token}, 200),
            ({"Authorization": "Bearer " + first.session_token}, 200),
        ]:
            conn = http.client.HTTPConnection(*first.server.server_address, timeout=3)
            try:
                conn.request("POST", "/v1/messages/count_tokens", "{}", headers)
                response = conn.getresponse()
                self.assertEqual(response.status, expected)
                response.read()
            finally:
                conn.close()
        self.assertEqual(first.upstream_calls, 0)

    def test_planner_environment(self):
        with tempfile.TemporaryDirectory() as root, patch.dict(os.environ, LOCALAPPDATA=root, OTHER_API_KEY="private"):
            with ai_dsh.task_workspace() as workspace, patch.object(ai_dsh, "available", return_value=True):
                with patch.object(subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "{}", "")) as launch:
                    ai_dsh.run_task("test", "session-token", workspace=workspace, base_url="http://127.0.0.1:12345")
                kwargs = launch.call_args.kwargs
                self.assertEqual(kwargs["cwd"], str(workspace))
                self.assertEqual(kwargs["env"]["DEEPSEEK_API_KEY"], "session-token")
                self.assertNotIn("OTHER_API_KEY", kwargs["env"])
                self.assertTrue(Path(kwargs["env"]["DSH_HOME"]).is_relative_to(workspace.parent))


if __name__ == "__main__":
    unittest.main(verbosity=2)
