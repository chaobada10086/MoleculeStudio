"""Local Anthropic Messages -> OpenAI Chat Completions adapter for the embedded DSH.

The bundled DeepSeek Harness speaks the Anthropic Messages protocol and points
at https://api.deepseek.com/anthropic by default.  When the user configures a
different OpenAI-compatible endpoint in MoleculeStudio (opencode go, or any
other gateway), the bridge starts this adapter on 127.0.0.1, points DSH at it
via DEEPSEEK_BASE_URL, and the adapter forwards to the real endpoint with full
protocol translation, including streaming and tool calls.

Security: the server binds to loopback only, holds the API key in memory, never
logs request bodies, and dies with the bridge process.
"""
import json
import threading
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# DSH's bundled deepseek model catalog ids -> the model actually configured in
# MoleculeStudio.  Unknown ids pass through unchanged.
_CATALOG_MODELS = {"deepseek-flash", "deepseek-chat", "deepseek-reasoner",
                   "deepseek-v4-flash", "deepseek-v4-pro", "deepseek-coder"}


class _Adapter:
    def __init__(self, endpoint, key, model, session_header):
        self.endpoint = endpoint.rstrip("/")
        self.key = key
        self.model = model
        self.session_header = session_header
        self.server = None
        self.url = None
        self.thread = None
        self.upstream_calls = 0
        self.last_error = ""

    # -- upstream transport -------------------------------------------------

    def _headers(self, accept):
        headers = {"Authorization": "Bearer " + self.key,
                   "Content-Type": "application/json", "Accept": accept,
                   "User-Agent": "MoleculeStudio/0.9.3.1"}
        if self.session_header:
            headers["x-opencode-session"] = self.session_header
        return headers

    def _post(self, payload, accept, timeout=1800):
        """POST to the configured endpoint.  Retries once without
        stream_options (some gateways reject it) and once with the model
        configured in MoleculeStudio when the upstream rejects the model id."""
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        for attempt in range(3):
            request = urllib.request.Request(self.endpoint + "/chat/completions",
                                             data=data, headers=self._headers(accept))
            self.upstream_calls += 1
            try:
                return urllib.request.build_opener().open(request, timeout=timeout)
            except urllib.error.HTTPError as exc:
                detail = exc.read().decode("utf-8", "replace")[:400]
                self.last_error = "HTTP %d: %s" % (exc.code, detail)
                lowered = detail.lower()
                if (attempt == 0 and exc.code == 400 and "stream_options" in payload):
                    payload = {k: v for k, v in payload.items() if k != "stream_options"}
                    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
                    continue
                if (exc.code in (400, 404) and payload.get("model") != self.model
                        and any(w in lowered for w in ("model", "not found", "unavailable"))):
                    payload = {**payload, "model": self.model}
                    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
                    continue
                raise
        raise RuntimeError("unreachable")

    # -- request translation (Anthropic -> OpenAI) ---------------------------

    def _to_openai(self, request):
        messages = []
        system = request.get("system")
        if isinstance(system, str) and system:
            messages.append({"role": "system", "content": system})
        elif isinstance(system, list):
            text = "\n".join(b.get("text", "") for b in system if isinstance(b, dict))
            if text:
                messages.append({"role": "system", "content": text})
        for message in request.get("messages", []):
            role = message.get("role") or "user"
            content = message.get("content")
            if isinstance(content, str):
                messages.append({"role": role, "content": content})
                continue
            tool_calls, text_parts, images = [], [], []
            for block in content or []:
                if not isinstance(block, dict):
                    continue
                kind = block.get("type")
                if kind == "text":
                    text_parts.append(block.get("text", ""))
                elif kind == "thinking":
                    continue
                elif kind == "image":
                    source = block.get("source") or {}
                    if source.get("type") == "base64":
                        images.append({"type": "image_url", "image_url": {
                            "url": "data:%s;base64,%s" % (source.get("media_type", "image/png"),
                                                          source.get("data", ""))}})
                elif kind == "tool_use":
                    tool_calls.append({"id": block.get("id"), "type": "function", "function": {
                        "name": block.get("name"),
                        "arguments": json.dumps(block.get("input") or {}, ensure_ascii=False)}})
                elif kind == "tool_result":
                    inner = block.get("content")
                    if isinstance(inner, list):
                        for part in inner:
                            source = part.get('source', {}) if isinstance(part, dict) and part.get('type') == 'image' else {}
                            if source.get('type') == 'base64':
                                images.append({'type': 'image_url', 'image_url': {'url': 'data:%s;base64,%s' % (source.get('media_type', 'image/png'), source.get('data', ''))}})
                        inner = "\n".join(b.get("text", "") for b in inner
                                          if isinstance(b, dict) and b.get("type") == "text")
                    messages.append({"role": "tool", "tool_call_id": block.get("tool_use_id"),
                                     "content": ("[工具执行出错] " if block.get("is_error") else "")
                                     + str(inner or "")})
            body = "\n".join(p for p in text_parts if p)
            if tool_calls:
                messages.append({"role": "assistant", "content": body or None,
                                 "tool_calls": tool_calls})
            elif images:
                user_content = ([{"type": "text", "text": body}] if body else []) + images
                messages.append({"role": role, "content": user_content})
            elif body:
                messages.append({"role": role, "content": body})
        payload = {"model": request.get("model") or self.model,
                   "messages": messages,
                   "max_tokens": request.get("max_tokens") or 16384}
        if request.get("temperature") is not None:
            payload["temperature"] = request["temperature"]
        if request.get("stop_sequences"):
            payload["stop"] = request["stop_sequences"][:4]
        if request.get("tools"):
            payload["tools"] = [{"type": "function", "function": {
                "name": t.get("name"), "description": t.get("description") or "",
                "parameters": t.get("input_schema") or {"type": "object", "properties": {}}}}
                for t in request["tools"] if isinstance(t, dict)]
            choice = request.get("tool_choice") or {}
            if choice.get("type") == "any":
                payload["tool_choice"] = "required"
            elif choice.get("type") == "tool":
                payload["tool_choice"] = {"type": "function",
                                          "function": {"name": choice.get("name")}}
            elif choice.get("type") == "none":
                payload["tool_choice"] = "none"
            else:
                payload["tool_choice"] = "auto"
        if request.get("stream"):
            payload["stream"] = True
            payload["stream_options"] = {"include_usage": True}
        if payload.get("model") in _CATALOG_MODELS:
            payload["model"] = self.model
        return payload

    # -- response translation (OpenAI -> Anthropic) ---------------------------

    @staticmethod
    def _blocks(message):
        blocks = []
        text = message.get("content")
        if isinstance(text, str) and text:
            blocks.append({"type": "text", "text": text})
        elif isinstance(text, list):
            joined = "\n".join(p.get("text", "") for p in text if isinstance(p, dict))
            if joined:
                blocks.append({"type": "text", "text": joined})
        for call in message.get("tool_calls") or []:
            function = call.get("function") or {}
            raw = function.get("arguments") or "{}"
            try:
                arguments = json.loads(raw)
            except ValueError:
                arguments = {"_raw": raw}
            blocks.append({"type": "tool_use", "id": call.get("id") or "toolu_0",
                           "name": function.get("name") or "unknown", "input": arguments})
        return blocks

    @staticmethod
    def _stop_reason(finish):
        return {"tool_calls": "tool_use", "length": "max_tokens",
                "content_filter": "refusal"}.get(finish, "end_turn")

    def complete(self, request):
        """Non-streaming request; returns the Anthropic message dict."""
        payload = self._to_openai(request)
        response = self._post(payload, "application/json")
        try:
            data = json.loads(response.read())
        finally:
            response.close()
        choice = (data.get("choices") or [{}])[0]
        message = choice.get("message") or {}
        usage = data.get("usage") or {}
        return {"id": data.get("id") or "msg_adapter", "type": "message",
                "role": "assistant", "model": request.get("model") or self.model,
                "content": self._blocks(message),
                "stop_reason": self._stop_reason(choice.get("finish_reason")),
                "stop_sequence": None,
                "usage": {"input_tokens": usage.get("prompt_tokens") or 0,
                          "output_tokens": usage.get("completion_tokens") or 0}}

    def stream(self, request, emit):
        """Streaming request; emits Anthropic SSE events through emit().

        The Anthropic stream is strictly linear: a content block starts,
        receives deltas, stops, and only then may the next block start.  The
        translator therefore keeps one open block at a time and maps OpenAI
        tool_call indices onto consecutive Anthropic block indices.
        """
        payload = self._to_openai(request)
        response = self._post(payload, "text/event-stream")
        state = {"index": -1, "kind": None, "meta": None}
        tool_slots = {}  # OpenAI tool_call index -> Anthropic block index
        input_tokens = output_tokens = 0
        stop_reason = "end_turn"

        def close_open():
            if state["kind"] is not None:
                emit("content_block_stop", {"type": "content_block_stop", "index": state["index"]})
                state["kind"] = None

        def switch(kind, meta=None):
            if state["kind"] == kind and (kind != "tool_use" or state["meta"] is meta):
                return state["index"]
            close_open()
            state["index"] += 1
            state["kind"] = kind
            state["meta"] = meta
            if kind == "text":
                starter = {"type": "text", "text": ""}
            elif kind == "thinking":
                starter = {"type": "thinking", "thinking": ""}
            else:
                starter = {"type": "tool_use", "id": meta["id"], "name": meta["name"], "input": {}}
            emit("content_block_start", {"type": "content_block_start",
                                         "index": state["index"], "content_block": starter})
            return state["index"]

        try:
            emit("ping", {"type": "ping"})
            for raw_line in response:
                line = raw_line.decode("utf-8", "replace").strip()
                if not line.startswith("data:"):
                    continue
                data = line[5:].strip()
                if data == "[DONE]":
                    break
                try:
                    chunk = json.loads(data)
                except ValueError:
                    continue
                if isinstance(chunk.get("usage"), dict):
                    input_tokens = chunk["usage"].get("prompt_tokens") or input_tokens
                    output_tokens = chunk["usage"].get("completion_tokens") or output_tokens
                choice = (chunk.get("choices") or [{}])[0]
                if choice.get("finish_reason"):
                    stop_reason = self._stop_reason(choice["finish_reason"])
                delta = choice.get("delta") or {}
                text = delta.get("content")
                if isinstance(text, str) and text:
                    index = switch("text")
                    emit("content_block_delta", {"type": "content_block_delta", "index": index,
                                                 "delta": {"type": "text_delta", "text": text}})
                reasoning = delta.get("reasoning_content")
                if isinstance(reasoning, str) and reasoning:
                    index = switch("thinking")
                    emit("content_block_delta", {"type": "content_block_delta", "index": index,
                                                 "delta": {"type": "thinking_delta", "thinking": reasoning}})
                for call in delta.get("tool_calls") or []:
                    tool_index = call.get("index", 0)
                    function = call.get("function") or {}
                    meta = tool_slots.get(tool_index)
                    if meta is None:
                        meta = {"id": call.get("id") or ("toolu_" + str(tool_index)),
                                "name": function.get("name") or "unknown", "parts": []}
                        tool_slots[tool_index] = meta
                    elif function.get('name'):
                        meta['name'] = function['name']
                    if call.get('id'): meta['id'] = call['id']
                    piece = function.get("arguments")
                    if isinstance(piece, str): meta['parts'].append(piece)
            # OpenAI can interleave deltas for parallel calls. Anthropic content
            # blocks must be sequential; emit each complete call once at the end.
            for tool_index in sorted(tool_slots):
                meta = tool_slots[tool_index]
                index = switch('tool_use', meta)
                emit('content_block_delta', {'type': 'content_block_delta', 'index': index,
                     'delta': {'type': 'input_json_delta', 'partial_json': ''.join(meta['parts']) or '{}'}})
            close_open()
            emit("message_delta", {"type": "message_delta",
                                   "delta": {"stop_reason": stop_reason, "stop_sequence": None},
                                   "usage": {"output_tokens": output_tokens}})
            emit("message_stop", {"type": "message_stop"})
        finally:
            try:
                response.close()
            except Exception:
                pass
        return {"input_tokens": input_tokens, "output_tokens": output_tokens}


_ADAPTER = None
_ADAPTER_LOCK = threading.Lock()


class _Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def handle_one_request(self):
        try:
            super().handle_one_request()
        except (ConnectionResetError, ConnectionAbortedError, BrokenPipeError, TimeoutError):
            # DSH drops keep-alive sockets when a task ends or is killed; that
            # is routine, not an error worth a traceback on the bridge's stderr.
            self.close_connection = True

    def _json(self, status, body):
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _error(self, status, message):
        self._json(status, {"type": "error", "error": {"type": "api_error", "message": message}})

    def do_GET(self):
        self._error(404, "this adapter only serves the Anthropic Messages API")

    def do_POST(self):
        adapter = _ADAPTER
        try:
            length = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            length = 0
        if length <= 0 or length > 64 * 1024 * 1024:
            self._error(400, "missing or oversized request body")
            return
        raw = self.rfile.read(length)
        try:
            request = json.loads(raw)
        except ValueError:
            self._error(400, "request body is not valid JSON")
            return
        if adapter is None:
            self._error(503, "adapter not started")
            return
        path = self.path.split("?")[0].rstrip("/")
        if path.endswith("/messages/count_tokens"):
            self._json(200, {"input_tokens": max(1, len(raw) // 4)})
            return
        if not path.endswith("/messages"):
            self._error(404, "unsupported path; only /v1/messages is translated")
            return
        try:
            if request.get("stream"):
                self._stream_response(adapter, request)
            else:
                self._json(200, adapter.complete(request))
        except (BrokenPipeError, ConnectionAbortedError, ConnectionResetError):
            # The DSH side dropped the connection (task aborted); nothing to send.
            self.close_connection = True
        except urllib.error.HTTPError as exc:
            detail = ""
            try:
                detail = exc.read().decode("utf-8", "replace")[:300]
            except Exception:
                pass
            self._error(502, "上游服务拒绝了请求（HTTP %d）：%s" % (exc.code, detail))
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            self._error(502, "上游请求失败：" + str(exc)[:250])
        except Exception as exc:  # translation bugs must not kill the server
            self._error(500, "adapter error: " + type(exc).__name__ + ": " + str(exc)[:250])

    def _stream_response(self, adapter, request):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "close")
        self.close_connection = True
        self.end_headers()

        def emit(event, data):
            payload = json.dumps(data, ensure_ascii=False)
            self.wfile.write(("event: %s\ndata: %s\n\n" % (event, payload)).encode("utf-8"))
            self.wfile.flush()

        emit("message_start", {"type": "message_start", "message": {
            "id": "msg_adapter_pending", "type": "message", "role": "assistant",
            "model": request.get("model") or adapter.model, "content": [],
            "stop_reason": None, "stop_sequence": None,
            "usage": {"input_tokens": 0, "output_tokens": 0}}})
        try:
            usage = adapter.stream(request, emit)
        except (BrokenPipeError, ConnectionAbortedError, ConnectionResetError):
            raise
        except Exception as exc:
            detail = str(exc)[:300]
            try:
                emit("message_delta", {"delta": {"stop_reason": "end_turn", "stop_sequence": None},
                                       "usage": {"output_tokens": 0}})
                emit("message_stop", {"type": "message_stop"})
                self.wfile.write(("event: error\ndata: %s\n\n" % json.dumps(
                    {"type": "error", "error": {"type": "api_error",
                                                "message": "adapter stream failed: " + detail}})).encode("utf-8"))
                self.wfile.flush()
            except Exception:
                pass
            return
        del usage


def start_adapter(endpoint, key, model, session_header="moleculestudio-dsh"):
    """Start (or reuse) the loopback adapter and return it."""
    global _ADAPTER
    with _ADAPTER_LOCK:
        if _ADAPTER is not None and _ADAPTER.server is not None:
            return _ADAPTER
        adapter = _Adapter(endpoint, key, model, session_header)
        server = None
        last_error = None
        for _ in range(3):
            try:
                server = ThreadingHTTPServer(("127.0.0.1", 0), _Handler)
                break
            except OSError as exc:
                last_error = exc
        if server is None:
            raise OSError("无法为高级绘图启动本地适配器（绑定 127.0.0.1 失败：" + ((getattr(last_error, "strerror", "") or str(last_error))) + "）") from None
        adapter.server = server
        adapter.server.daemon_threads = True
        adapter.url = "http://127.0.0.1:%d" % adapter.server.server_address[1]
        adapter.thread = threading.Thread(target=adapter.server.serve_forever,
                                          kwargs={"poll_interval": 0.5}, daemon=True)
        adapter.thread.start()
        _ADAPTER = adapter
        return adapter


def adapter_status():
    if _ADAPTER is None or _ADAPTER.server is None:
        return {"running": False}
    return {"running": True, "url": _ADAPTER.url, "upstream_calls": _ADAPTER.upstream_calls,
            "last_error": _ADAPTER.last_error[:200]}
