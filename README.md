# AI Passport Trader Board

Pocket trader dashboard firmware for [FoloToy AI Passport](https://ai-passport.folotoy.cn).

Shows remote account summary (assets, P&L, position, cash), holdings, and latest trade after Wi‑Fi provisioning to a **JSON relay** on your LAN (default port `8766`).

## Architecture

```text
Trading app / push client  --PUT JSON-->  relay/server.py  <--GET--  AI Passport
```

The device never talks to the trading engine directly. Keep `relay/server.py` reachable from the same Wi‑Fi (or a host the device can route to), then point Passport’s API host/port at that relay.

See **[relay/README.md](relay/README.md)** for HTTP rules, CLI usage, and sample JSON under [`relay/examples/`](relay/examples/).

## Setup (device)

1. On a PC, start the relay: `python relay/server.py --host 0.0.0.0 --port 8766`, and push trader snapshots (`relay/client.py push` or your own PUT client).
2. On the device Home → **打开配网** → OK starts open hotspot `Passport-Board`.
3. Phone joins the hotspot, open `http://192.168.4.1`, enter Wi‑Fi + **relay host LAN IP** + port `8766`, Save.
4. Status **网络已连接** means success (use 2.4 GHz if needed).
5. Home → **打开看板**: Up/Down switch profile 1–5; OK cycles assets → positions → trade; long-press OK to return.

## Build (firmware)

Requires ESP-IDF (ESP32-C3 target). From this directory:

```bash
idf.py set-target esp32c3
idf.py build
```

Do not commit `build/` or merged firmware binaries.

## License

MIT (see `LICENSE`). Based on the FoloToy AI Passport open firmware tree.
