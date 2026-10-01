# -*- coding: utf-8 -*-
"""操盘数据中转 Server Demo（本机 / 有公网 IP 的中转机均可运行）。

用途：内网无公网 IP 的机器把操盘手 JSON 推到本服务，其他客户端从本服务拉取。

接口：
  GET  /api/health                 健康检查
  GET  /api/traders                全部操盘手快照列表
  GET  /api/traders/<id>           指定操盘手最新快照
  PUT  /api/traders/<id>           推送/覆盖该操盘手快照（JSON body，按字段合并）
  POST /api/traders/<id>           同上（兼容）
  DELETE /api/traders/<id>         删除缓存
  GET  /api/traders/<id>/history   最近推送历史（最多 50 条）

按需调取（查询参数 include / fields，逗号分隔）：
  summary | day_summary | overview  → 操盘概要（day_summary + 资金盈亏字段）
  trades                            → 成交记录
  positions                         → 持仓记录
  省略 include                      → 返回完整缓存

示例：
  GET /api/traders?include=summary
  GET /api/traders/profile1?include=trades,positions
  GET /api/traders/profile1?fields=summary

要求：Python >= 3.6（仅标准库）

启动：
  python relay/server.py --host 0.0.0.0 --port 8766

说明文档与示例 JSON：见同目录 README.md / examples/
"""
import argparse
import json
import threading
from datetime import datetime
from http.server import BaseHTTPRequestHandler, HTTPServer
from socketserver import ThreadingMixIn
from typing import Any, Dict, List, Optional, Tuple
from urllib.parse import parse_qs, unquote, urlparse


class ThreadingHTTPServer(ThreadingMixIn, HTTPServer):
    """Python 3.6 compatible threaded HTTP server (stdlib added this in 3.7)."""
    daemon_threads = True


def _now() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


SECTION_ALIASES = {
    "summary": "summary",
    "day_summary": "summary",
    "overview": "summary",
    "trades": "trades",
    "trade": "trades",
    "positions": "positions",
    "position": "positions",
}

# 基础身份字段：按需过滤时仍保留
BASE_KEYS = {
    "profile_id", "name", "short_name", "role_type", "strategy",
    "running", "status", "bound_analyst_id", "bound_analyst_name",
    "sections", "pushed_at", "source_host", "seq",
}

SUMMARY_KEYS = {
    "total_assets", "cash", "holdings_value", "position_pct",
    "pnl", "pnl_pct", "unrealized_pnl", "unrealized_pct",
    "holdings_count", "trade_count", "day_summary",
}


def normalize_sections(raw) -> list:
    if raw is None:
        return []
    if isinstance(raw, str):
        parts = [p.strip() for p in raw.replace(";", ",").split(",") if p.strip()]
    elif isinstance(raw, (list, tuple, set)):
        parts = [str(p).strip() for p in raw if str(p).strip()]
    else:
        parts = []
    out = []
    for p in parts:
        key = SECTION_ALIASES.get(p.lower())
        if key and key not in out:
            out.append(key)
    return out


def filter_payload(payload: dict, sections: list) -> dict:
    """按 sections 裁剪 payload；sections 空则原样返回。"""
    if not payload or not isinstance(payload, dict):
        return payload or {}
    if not sections:
        return payload
    out = {k: payload[k] for k in BASE_KEYS if k in payload}
    out["include"] = sections
    if "summary" in sections:
        for k in SUMMARY_KEYS:
            if k in payload:
                out[k] = payload[k]
    if "trades" in sections and "trades" in payload:
        out["trades"] = payload.get("trades") or []
        if "trade_count" in payload:
            out["trade_count"] = payload["trade_count"]
        else:
            out["trade_count"] = len(out["trades"])
    if "positions" in sections and "positions" in payload:
        out["positions"] = payload.get("positions") or []
        if "holdings_count" in payload:
            out["holdings_count"] = payload["holdings_count"]
        else:
            out["holdings_count"] = len(out["positions"])
    # 标记服务端实际可用的段落
    available = []
    if any(k in payload for k in SUMMARY_KEYS) or "day_summary" in payload:
        available.append("summary")
    if "trades" in payload:
        available.append("trades")
    if "positions" in payload:
        available.append("positions")
    out["available_sections"] = available
    return out


def filter_record(record: dict, sections: list) -> dict:
    if not record:
        return record
    out = {
        "trader_id": record.get("trader_id"),
        "received_at": record.get("received_at"),
        "payload": filter_payload(record.get("payload") or {}, sections),
    }
    return out


class TraderRelayStore:
    """内存中转存储（进程内）。推送按字段合并，便于分内容多次更新。"""

    def __init__(self, history_limit: int = 50):
        self._lock = threading.RLock()
        self._latest = {}  # type: Dict[str, dict]
        self._history = {}  # type: Dict[str, list]
        self._history_limit = max(5, int(history_limit))
        self.started_at = _now()

    def upsert(self, trader_id: str, payload: dict) -> dict:
        trader_id = (trader_id or "").strip()
        if not trader_id:
            raise ValueError("trader_id 不能为空")
        if not isinstance(payload, dict):
            raise ValueError("body 须为 JSON 对象")
        with self._lock:
            prev = self._latest.get(trader_id)
            merged = {}
            if prev and isinstance(prev.get("payload"), dict):
                merged.update(prev["payload"])
            # 新包覆盖同名字段；未推送的段落保留旧值
            merged.update(payload)
            # 合并 sections 标记
            old_sec = normalize_sections(merged.get("sections"))
            new_sec = normalize_sections(payload.get("sections"))
            if "day_summary" in payload or any(k in payload for k in SUMMARY_KEYS):
                if "summary" not in new_sec:
                    new_sec.append("summary")
            if "trades" in payload and "trades" not in new_sec:
                new_sec.append("trades")
            if "positions" in payload and "positions" not in new_sec:
                new_sec.append("positions")
            for s in old_sec:
                if s not in new_sec:
                    # 旧段落仍在 merged 中则保留标记
                    if s == "summary" and (
                            "day_summary" in merged or any(k in merged for k in SUMMARY_KEYS)):
                        new_sec.append(s)
                    elif s == "trades" and "trades" in merged:
                        new_sec.append(s)
                    elif s == "positions" and "positions" in merged:
                        new_sec.append(s)
            merged["sections"] = new_sec or old_sec
            record = {
                "trader_id": trader_id,
                "received_at": _now(),
                "payload": merged,
            }
            self._latest[trader_id] = record
            hist = self._history.setdefault(trader_id, [])
            hist.append({
                "received_at": record["received_at"],
                "payload": dict(payload),  # 历史保留本次原始推送
            })
            if len(hist) > self._history_limit:
                del hist[: len(hist) - self._history_limit]
        return record

    def get(self, trader_id):
        with self._lock:
            item = self._latest.get(trader_id)
            return dict(item) if item else None

    def list_all(self) -> list:
        with self._lock:
            items = [dict(v) for v in self._latest.values()]
        items.sort(key=lambda x: x.get("received_at") or "", reverse=True)
        return items

    def history(self, trader_id: str) -> list:
        with self._lock:
            return list(self._history.get(trader_id) or [])

    def delete(self, trader_id: str) -> bool:
        with self._lock:
            existed = trader_id in self._latest
            self._latest.pop(trader_id, None)
            self._history.pop(trader_id, None)
            return existed

    def stats(self) -> dict:
        with self._lock:
            return {
                "trader_count": len(self._latest),
                "trader_ids": sorted(self._latest.keys()),
                "started_at": self.started_at,
                "server_time": _now(),
            }


STORE = TraderRelayStore()


def _json_bytes(data, code=200):
    body = json.dumps(data, ensure_ascii=False, indent=2).encode("utf-8")
    return code, body, "application/json; charset=utf-8"


def _parse_include(query: dict) -> list:
    raw = ""
    for key in ("include", "fields", "sections"):
        vals = query.get(key) or []
        if vals:
            raw = vals[0]
            break
    return normalize_sections(raw)


class RelayHandler(BaseHTTPRequestHandler):
    server_version = "TraderRelay/1.1"

    def log_message(self, fmt, *args):
        print(f"[{_now()}] {self.address_string()} {fmt % args}")

    def _read_json(self) -> dict:
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length > 0 else b"{}"
        if not raw:
            return {}
        try:
            data = json.loads(raw.decode("utf-8"))
        except Exception as e:
            raise ValueError(f"JSON 解析失败: {e}") from e
        if not isinstance(data, dict):
            raise ValueError("JSON 根须为对象")
        return data

    def _send(self, code: int, body: bytes, content_type: str):
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _send_json(self, data: Any, code: int = 200):
        self._send(*_json_bytes(data, code))

    def do_OPTIONS(self):
        self._send(204, b"", "text/plain")

    def do_GET(self):
        parsed = urlparse(self.path)
        path = unquote(parsed.path).rstrip("/") or "/"
        query = parse_qs(parsed.query or "")
        include = _parse_include(query)
        try:
            if path in ("/api/health", "/health"):
                self._send_json({
                    "ok": True,
                    **STORE.stats(),
                    "include_options": ["summary", "trades", "positions"],
                })
                return
            if path == "/api/traders":
                items = [filter_record(it, include) for it in STORE.list_all()]
                self._send_json({
                    "ok": True,
                    "items": items,
                    "include": include or ["summary", "trades", "positions"],
                    **STORE.stats(),
                })
                return
            if path.startswith("/api/traders/"):
                rest = path[len("/api/traders/"):]
                if rest.endswith("/history"):
                    tid = rest[: -len("/history")].strip("/")
                    hist = STORE.history(tid)
                    if include:
                        hist = [
                            {
                                "received_at": h.get("received_at"),
                                "payload": filter_payload(h.get("payload") or {}, include),
                            }
                            for h in hist
                        ]
                    self._send_json({
                        "ok": True,
                        "trader_id": tid,
                        "include": include or None,
                        "history": hist,
                    })
                    return
                # 快捷：/api/traders/<id>/summary|trades|positions
                parts = [p for p in rest.split("/") if p]
                if len(parts) == 2 and parts[1] in (
                        "summary", "trades", "positions", "day_summary"):
                    tid, sec = parts[0], parts[1]
                    sec_n = normalize_sections(sec) or ["summary"]
                    item = STORE.get(tid)
                    if not item:
                        self._send_json({"ok": False, "error": "not found"}, 404)
                        return
                    self._send_json({
                        "ok": True,
                        **filter_record(item, sec_n),
                        "include": sec_n,
                    })
                    return
                tid = rest.strip("/")
                if "/" in tid:
                    self._send_json({"ok": False, "error": "invalid id"}, 400)
                    return
                item = STORE.get(tid)
                if not item:
                    self._send_json({"ok": False, "error": "not found"}, 404)
                    return
                self._send_json({
                    "ok": True,
                    **filter_record(item, include),
                    "include": include or ["summary", "trades", "positions"],
                })
                return
            self._send_json({
                "ok": True,
                "service": "trader-relay",
                "endpoints": [
                    "GET /api/health",
                    "GET /api/traders?include=summary,trades,positions",
                    "GET /api/traders/<id>?include=summary",
                    "GET /api/traders/<id>/summary",
                    "GET /api/traders/<id>/trades",
                    "GET /api/traders/<id>/positions",
                    "PUT|POST /api/traders/<id>",
                    "DELETE /api/traders/<id>",
                    "GET /api/traders/<id>/history",
                ],
            })
        except Exception as e:
            self._send_json({"ok": False, "error": str(e)}, 500)

    def do_PUT(self):
        self._upsert()

    def do_POST(self):
        self._upsert()

    def do_DELETE(self):
        path = unquote(urlparse(self.path).path).rstrip("/") or "/"
        if not path.startswith("/api/traders/"):
            self._send_json({"ok": False, "error": "not found"}, 404)
            return
        tid = path[len("/api/traders/"):].strip("/")
        if not tid or "/" in tid:
            self._send_json({"ok": False, "error": "invalid id"}, 400)
            return
        existed = STORE.delete(tid)
        self._send_json({"ok": True, "deleted": existed, "trader_id": tid})

    def _upsert(self):
        path = unquote(urlparse(self.path).path).rstrip("/") or "/"
        if not path.startswith("/api/traders/"):
            self._send_json({"ok": False, "error": "use PUT/POST /api/traders/<id>"}, 404)
            return
        tid = path[len("/api/traders/"):].strip("/")
        if not tid or "/" in tid:
            self._send_json({"ok": False, "error": "invalid id"}, 400)
            return
        try:
            payload = self._read_json()
            if "payload" in payload and isinstance(payload["payload"], dict):
                body = payload["payload"]
            else:
                body = payload
            record = STORE.upsert(tid, body)
            self._send_json({"ok": True, **record})
        except ValueError as e:
            self._send_json({"ok": False, "error": str(e)}, 400)
        except Exception as e:
            self._send_json({"ok": False, "error": str(e)}, 500)


def main():
    ap = argparse.ArgumentParser(description="操盘手数据中转 Server Demo")
    ap.add_argument(
        "--host", default="0.0.0.0",
        help="监听地址，默认 0.0.0.0（允许局域网其他设备访问；勿用 127.0.0.1）")
    ap.add_argument("--port", type=int, default=8766, help="端口，默认 8766")
    ap.add_argument(
        "--localhost-only", action="store_true",
        help="仅本机访问（绑定 127.0.0.1）；Passport 等局域网设备将连不上")
    args = ap.parse_args()

    host = "127.0.0.1" if args.localhost_only else (args.host or "0.0.0.0").strip()
    if host in ("127.0.0.1", "localhost") and not args.localhost_only:
        print(
            f"[{_now()}] WARNING: host={host} 仅本机可访问；"
            "已自动改为 0.0.0.0。若确实只要本机，请加 --localhost-only"
        )
        host = "0.0.0.0"

    ThreadingHTTPServer.allow_reuse_address = True
    httpd = ThreadingHTTPServer((host, args.port), RelayHandler)
    lan_hint = ""
    if host == "0.0.0.0":
        try:
            import socket
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                s.connect(("8.8.8.8", 80))
                lan_ip = s.getsockname()[0]
            lan_hint = f"  LAN: http://{lan_ip}:{args.port}"
        except Exception:
            lan_hint = "  LAN: http://<本机局域网IP>:" + str(args.port)
    print(f"[{_now()}] Trader Relay listening on http://{host}:{args.port}{lan_hint}")
    print("  GET  /api/health")
    print("  GET  /api/traders?include=summary,trades,positions")
    print("  GET  /api/traders/<id>/summary|trades|positions")
    print("  PUT  /api/traders/<id>   ← 推送 JSON（字段合并）")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print(f"\n[{_now()}] stopped")
    finally:
        httpd.server_close()


if __name__ == "__main__":
    main()
