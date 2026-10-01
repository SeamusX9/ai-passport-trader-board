# -*- coding: utf-8 -*-
"""操盘数据推送 / 查阅 Client Demo。

场景：
  1) 本机（无公网 IP）周期性把操盘手快照 PUT 到中转 Server
  2) 其他客户端从同一 Server GET 查阅

用法示例：
  # 终端1：启动中转
  python relay/server.py --port 8766

  # 终端2：推送一条示例（或接真实 hub 数据）
  python relay/client.py push --url http://127.0.0.1:8766 --id profile1

  # 终端3：拉取列表 / 指定操盘手
  python relay/client.py list --url http://127.0.0.1:8766
  python relay/client.py get  --url http://127.0.0.1:8766 --id profile1
  # --url 写根地址即可；误写成 .../api/traders 也会自动纠正

  # 循环推送（演示心跳）
  python relay/client.py push --url http://127.0.0.1:8766 --id profile1 --loop 5

  # 从本机「股票交易助手」真实数据推送（需该工程在磁盘上且可 import）
  python relay/client.py push-live --url http://127.0.0.1:8766
"""
import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime
from typing import Any


def _now() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def _base_url(url: str) -> str:
    """--url 只需要中转根地址。误带 /api、/api/traders 时剥掉。"""
    from urllib.parse import urlparse, urlunparse

    raw = (url or "").strip() or "http://127.0.0.1:8766"
    parsed = urlparse(raw)
    path = (parsed.path or "").rstrip("/")
    while True:
        lower = path.lower()
        if lower.endswith("/api/traders"):
            path = path[: -len("/api/traders")]
            continue
        if lower.endswith("/api/health"):
            path = path[: -len("/api/health")]
            continue
        if lower.endswith("/api"):
            path = path[: -len("/api")]
            continue
        break
    cleaned = urlunparse((
        parsed.scheme or "http",
        parsed.netloc or raw,
        path.rstrip("/") or "",
        "",
        "",
        "",
    )).rstrip("/")
    return cleaned or "http://127.0.0.1:8766"


def _request(method, url, body=None, timeout=10):
    data = None
    headers = {"Accept": "application/json"}
    if body is not None:
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        headers["Content-Type"] = "application/json; charset=utf-8"
    req = urllib.request.Request(url, data=data, headers=headers, method=method.upper())
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", errors="replace")
            try:
                return json.loads(raw) if raw else {"ok": True}
            except json.JSONDecodeError:
                return {"ok": False, "error": "non-json response", "text": raw[:500]}
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", errors="replace")
        try:
            payload = json.loads(raw) if raw else {}
        except json.JSONDecodeError:
            payload = {"text": raw[:500]}
        return {"ok": False, "status_code": e.code, "error": str(e), **payload}
    except Exception as e:
        return {"ok": False, "error": str(e)}


def sample_payload(trader_id: str) -> dict:
    """演示用假数据；真实接入请用 push-live。"""
    return {
        "profile_id": trader_id,
        "name": f"演示操盘手-{trader_id}",
        "role_type": "trader",
        "strategy": "均衡型",
        "running": True,
        "total_assets": 105230.55,
        "cash": 45230.55,
        "holdings_value": 60000.0,
        "position_pct": 57.0,
        "pnl": 5230.55,
        "pnl_pct": 5.23,
        "unrealized_pnl": 1800.0,
        "unrealized_pct": 3.1,
        "holdings_count": 2,
        "positions": [
            {
                "code": "600519",
                "name": "贵州茅台",
                "shares": 100,
                "avg_cost": 1680.0,
                "price": 1720.0,
                "unrealized_pct": 2.38,
            }
        ],
        "pushed_at": _now(),
        "source_host": "demo-client",
    }


def load_live_cards(include_detail: bool = True, trade_limit: int = 100) -> list:
    """从本地 MultiTraderHub 读取真实操盘手数据（含成交/持仓/当日概要）。"""
    import os
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    if root not in sys.path:
        sys.path.insert(0, root)
    from app.multi_trader import MultiTraderHub

    hub = MultiTraderHub()
    hub.bootstrap()
    cards = hub.list_cards()
    out = []
    for c in cards:
        if (c.get("role_type") or "trader") != "trader":
            continue
        # 概要字段（去掉过大 snapshot）
        item = {k: v for k, v in c.items() if k != "snapshot"}
        if include_detail:
            try:
                detail = hub.agent_detail(c["profile_id"])
            except Exception as e:
                item["detail_error"] = str(e)
                detail = {}
            trades = list(detail.get("trades") or [])
            if trade_limit > 0:
                trades = trades[:trade_limit]
            positions = detail.get("positions") or []
            day = detail.get("day_summary") or {}
            item.update({
                "day_summary": day,
                "positions": positions,
                "trades": trades,
                "trade_count": len(detail.get("trades") or trades),
                "holdings_count": len(positions),
                "unrealized_pnl": detail.get("unrealized_pnl", item.get("unrealized_pnl")),
                "unrealized_pct": detail.get("unrealized_pct", item.get("unrealized_pct")),
                "total_assets": detail.get("total_assets", item.get("total_assets")),
                "cash": detail.get("cash", item.get("cash")),
                "holdings_value": detail.get("holdings_value", item.get("holdings_value")),
                "pnl": detail.get("pnl", item.get("pnl")),
                "pnl_pct": detail.get("pnl_pct", item.get("pnl_pct")),
                "position_pct": detail.get("position_pct", item.get("position_pct")),
                "bound_analyst_id": detail.get("bound_analyst_id", item.get("bound_analyst_id")),
                "bound_analyst_name": detail.get(
                    "bound_analyst_name", item.get("bound_analyst_name")),
            })
        item["pushed_at"] = _now()
        item["source_host"] = "arena-live"
        out.append(item)
    return out


def cmd_health(args):
    r = _request("GET", f"{_base_url(args.url)}/api/health")
    print(json.dumps(r, ensure_ascii=False, indent=2))
    return 0 if r.get("ok") else 1


def cmd_list(args):
    include = (getattr(args, "include", "") or "").strip()
    q = f"?include={include}" if include else ""
    r = _request("GET", f"{_base_url(args.url)}/api/traders{q}")
    print(json.dumps(r, ensure_ascii=False, indent=2))
    return 0 if r.get("ok") else 1


def cmd_get(args):
    if not args.id:
        print("需要 --id", file=sys.stderr)
        return 2
    include = (getattr(args, "include", "") or "").strip()
    if include and "," not in include and include in (
            "summary", "trades", "positions"):
        path = f"/api/traders/{args.id}/{include}"
    else:
        q = f"?include={include}" if include else ""
        path = f"/api/traders/{args.id}{q}"
    r = _request("GET", f"{_base_url(args.url)}{path}")
    print(json.dumps(r, ensure_ascii=False, indent=2))
    return 0 if r.get("ok") else 1


def cmd_push(args):
    if not args.id:
        print("需要 --id", file=sys.stderr)
        return 2
    url = f"{_base_url(args.url)}/api/traders/{args.id}"
    loop = max(0, int(args.loop or 0))
    n = 0
    while True:
        n += 1
        payload = sample_payload(args.id)
        payload["seq"] = n
        r = _request("PUT", url, payload)
        print(f"[{_now()}] push #{n} -> {r.get('ok')} {r.get('received_at') or r.get('error')}")
        if loop <= 0:
            print(json.dumps(r, ensure_ascii=False, indent=2))
            return 0 if r.get("ok") else 1
        time.sleep(loop)


def cmd_push_live(args):
    url_base = _base_url(args.url)
    loop = max(0, int(args.loop or 0))
    trade_limit = max(0, int(getattr(args, "trade_limit", 100) or 0))
    while True:
        try:
            cards = load_live_cards(include_detail=True, trade_limit=trade_limit)
        except Exception as e:
            print(f"[{_now()}] load live failed: {e}", file=sys.stderr)
            return 1
        if not cards:
            print(f"[{_now()}] no trader cards")
        for c in cards:
            tid = c.get("profile_id") or c.get("id")
            if not tid:
                continue
            r = _request("PUT", f"{url_base}/api/traders/{tid}", c)
            name = c.get("short_name") or c.get("name") or tid
            n_tr = len(c.get("trades") or [])
            n_pos = len(c.get("positions") or [])
            print(
                f"[{_now()}] push {name}({tid}) "
                f"pnl={c.get('pnl_pct')}% float={c.get('unrealized_pnl')} "
                f"trades={n_tr} pos={n_pos} "
                f"-> {r.get('ok')} {r.get('error') or ''}"
            )
        if loop <= 0:
            return 0
        time.sleep(loop)


def main():
    ap = argparse.ArgumentParser(description="操盘数据中转 Client Demo")
    ap.add_argument(
        "command",
        choices=["health", "list", "get", "push", "push-live"],
        help="操作",
    )
    ap.add_argument(
        "--url", default="http://127.0.0.1:8766",
        help="中转根地址，例如 http://127.0.0.1:8766（不要带 /api/traders）")
    ap.add_argument("--id", default="", help="操盘手 ID（get/push）")
    ap.add_argument("--loop", type=float, default=0, help="循环间隔秒；0=只跑一次")
    ap.add_argument(
        "--trade-limit", type=int, default=100,
        help="push-live 最多附带最近成交笔数（0=全部）")
    ap.add_argument(
        "--include", default="",
        help="按需调取：summary,trades,positions（list/get）")
    args = ap.parse_args()

    handlers = {
        "health": cmd_health,
        "list": cmd_list,
        "get": cmd_get,
        "push": cmd_push,
        "push-live": cmd_push_live,
    }
    raise SystemExit(handlers[args.command](args))


if __name__ == "__main__":
    main()
