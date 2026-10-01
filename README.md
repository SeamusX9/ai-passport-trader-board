# AI Passport Trader Board

Pocket trader dashboard firmware for [FoloToy AI Passport](https://ai-passport.folotoy.cn).

Shows remote account summary (assets, P&L, position, cash), holdings, and latest trade after Wi‑Fi provisioning to a LAN API (default port `8766`).

## Setup (device)

1. Home → **打开配网** → OK starts open hotspot `Passport-Board`.
2. Phone joins the hotspot, open `http://192.168.4.1`, enter Wi‑Fi + PC LAN IP/port, Save.
3. Status **网络已连接** means success (use 2.4 GHz if needed).
4. Home → **打开看板**: Up/Down switch profile 1–5; OK cycles assets → positions → trade; long-press OK to return.

## Build

Requires ESP-IDF (ESP32-C3 target). From this directory:

```bash
idf.py set-target esp32c3
idf.py build
```

Do not commit `build/` or merged firmware binaries.

## License

MIT (see `LICENSE`). Based on the FoloToy AI Passport open firmware tree.
