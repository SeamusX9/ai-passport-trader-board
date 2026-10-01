# Trader Relay（操盘数据中转）

Passport 交易看板不直连内网交易程序，而是通过本目录的 **JSON 中转服务** 拉取快照。

典型链路：

```text
交易助手 / 推送端  --PUT-->  relay/server.py  <--GET--  AI Passport / 其他客户端
```

- 仅标准库，Python ≥ 3.6
- 默认端口 **8766**
- 内存缓存（进程内）；重启后需重新推送

## 快速开始

```bash
# 终端 1：启动中转（监听局域网）
python relay/server.py --host 0.0.0.0 --port 8766

# 终端 2：推送演示数据
python relay/client.py push --url http://127.0.0.1:8766 --id profile1

# 终端 3：查阅
python relay/client.py health --url http://127.0.0.1:8766
python relay/client.py get --url http://127.0.0.1:8766 --id profile1 --include summary
```

Passport 配网时填写：**运行 relay 的电脑局域网 IP** + 端口 `8766`（不要用 `127.0.0.1`，设备访问不到）。

> `client.py push-live` 依赖本机「股票交易助手」工程中的 `MultiTraderHub`，单独克隆本仓库时请用 `push` 演示数据，或自行对接真实推送端。

## 与 Passport 固件的对应关系

固件按档位 `profile1` … `profile5` 请求：

| 看板视图 | HTTP |
| --- | --- |
| 资产概要 | `GET /api/traders/profileN/summary` |
| 持仓列表 | `GET /api/traders/profileN?fields=positions` |
| 最近成交 | `GET /api/traders/profileN?fields=trades` |

响应根对象需含 `ok`、`trader_id`、`payload`；概要字段在 `payload`（及 `payload.day_summary`）内。

---

## HTTP 接口规则

根地址形如 `http://<host>:8766`。下列路径均返回 JSON（UTF-8），并带 CORS 头。

### 发现与健康

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/` 或未知路径 | 返回服务名与端点列表 |
| GET | `/api/health` | 健康检查与当前缓存的 trader id |

### 读取

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/api/traders` | 全部操盘手最新快照 |
| GET | `/api/traders/<id>` | 指定操盘手最新快照 |
| GET | `/api/traders/<id>/summary` | 仅概要段落 |
| GET | `/api/traders/<id>/trades` | 仅成交段落 |
| GET | `/api/traders/<id>/positions` | 仅持仓段落 |
| GET | `/api/traders/<id>/history` | 最近推送历史（最多约 50 条） |

查询参数（`include` / `fields` / `sections`，逗号分隔，等价）：

| 值 | 含义 |
| --- | --- |
| `summary`（别名 `day_summary` / `overview`） | 资金、盈亏、`day_summary` 等 |
| `trades`（别名 `trade`） | 成交列表 |
| `positions`（别名 `position`） | 持仓列表 |
| 省略 | 返回完整缓存 payload |

示例：

```http
GET /api/traders?include=summary
GET /api/traders/profile1?include=trades,positions
GET /api/traders/profile1?fields=summary
```

按需过滤时仍会保留身份字段：`profile_id`、`name`、`short_name`、`role_type`、`strategy`、`running`、`status`、`bound_analyst_*`、`sections`、`pushed_at`、`source_host`、`seq` 等。

### 写入 / 删除

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| PUT 或 POST | `/api/traders/<id>` | 推送快照；**按字段合并**（未出现的段落保留旧值） |
| DELETE | `/api/traders/<id>` | 删除该 id 的缓存与历史 |

Body：JSON 对象。若外层带 `{ "payload": { ... } }`，则只取内层 `payload`；否则整个 body 作为 payload。

建议在 payload 中带上：

- `profile_id` / `short_name` / `strategy` / `running` / `status`
- 概要：`total_assets`、`cash`、`position_pct`、`pnl`、`pnl_pct`、`holdings_count`、`day_summary`…
- 列表：`positions[]`、`trades[]`
- 可选：`sections`: `["summary","trades","positions"]`

### 错误形态

```json
{ "ok": false, "error": "not found" }
```

常见 HTTP 状态：`400` 参数/JSON 非法，`404` 无此 trader，`500` 服务端异常。

---

## 示例数据

下列样例来自真实中转缓存（已截断过长列表与备注），完整文件见 [`examples/`](examples/)：

- [`examples/health.json`](examples/health.json)
- [`examples/summary-profile1.json`](examples/summary-profile1.json)
- [`examples/positions-profile1.json`](examples/positions-profile1.json)
- [`examples/trades-profile1.json`](examples/trades-profile1.json)
- [`examples/summary-profile2.json`](examples/summary-profile2.json)

### `GET /api/health`（节选）

```json
{
  "ok": true,
  "trader_count": 5,
  "trader_ids": ["profile1", "profile2", "profile3", "profile4", "profile5"],
  "include_options": ["summary", "trades", "positions"]
}
```

### `GET /api/traders/profile1/summary`（节选）

```json
{
  "ok": true,
  "trader_id": "profile1",
  "received_at": "2026-10-01 11:28:01",
  "payload": {
    "profile_id": "profile1",
    "short_name": "大虎B",
    "strategy": "均衡型",
    "running": false,
    "status": "stopped",
    "total_assets": 459872.33,
    "cash": 23073.33,
    "holdings_value": 436799.0,
    "position_pct": 95.0,
    "pnl": -40127.67,
    "pnl_pct": -8.03,
    "holdings_count": 8,
    "day_summary": {
      "day": "2026-10-01",
      "day_pnl": 0.0,
      "day_pnl_pct": 0.0,
      "float_pnl": -28740.36,
      "float_pnl_pct": -6.17,
      "total_pnl": -40127.67,
      "total_pnl_pct": -8.03,
      "win_rate": 0.0
    },
    "available_sections": ["summary", "trades", "positions"]
  },
  "include": ["summary"]
}
```

### `GET /api/traders/profile1/positions`（节选）

```json
{
  "ok": true,
  "trader_id": "profile1",
  "payload": {
    "profile_id": "profile1",
    "holdings_count": 8,
    "positions": [
      {
        "code": "000725",
        "name": "京东方Ａ",
        "shares": 23100,
        "avg_cost": 6.1016,
        "price": 5.72,
        "market_value": 132132.0,
        "unrealized_pnl": -8814.64,
        "unrealized_pct": -6.25
      }
    ]
  },
  "include": ["positions"]
}
```

### `GET /api/traders/profile1/trades`（节选）

```json
{
  "ok": true,
  "trader_id": "profile1",
  "payload": {
    "trade_count": 24,
    "trades": [
      {
        "id": 48,
        "code": "600721",
        "name": "百花医药",
        "side": "买入",
        "price": 13.22,
        "shares": 100,
        "trade_at": "2026-09-30 13:56:39",
        "strategy": "均衡型"
      },
      {
        "id": 47,
        "code": "000002",
        "name": "万  科Ａ",
        "side": "卖出",
        "price": 4.31,
        "shares": 300,
        "trade_at": "2026-09-30 13:56:38",
        "sell_reason_type": "换仓"
      }
    ]
  },
  "include": ["trades"]
}
```

### 推送示例（`PUT /api/traders/profile1`）

```json
{
  "profile_id": "profile1",
  "name": "演示操盘手-profile1",
  "short_name": "演示",
  "role_type": "trader",
  "strategy": "均衡型",
  "running": true,
  "total_assets": 105230.55,
  "cash": 45230.55,
  "position_pct": 57.0,
  "pnl": 5230.55,
  "pnl_pct": 5.23,
  "holdings_count": 1,
  "positions": [
    {
      "code": "600519",
      "name": "贵州茅台",
      "shares": 100,
      "avg_cost": 1680.0,
      "price": 1720.0,
      "unrealized_pct": 2.38
    }
  ],
  "sections": ["summary", "positions"],
  "pushed_at": "2026-10-01 12:00:00",
  "source_host": "demo-client"
}
```

---

## 文件说明

| 文件 | 作用 |
| --- | --- |
| `server.py` | 中转服务 |
| `client.py` | 推送 / 查阅 CLI |
| `examples/*.json` | 真实形态示例（列表已截断） |
| `requirements.txt` | 无第三方依赖说明 |
