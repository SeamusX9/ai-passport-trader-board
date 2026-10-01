#include "app_traders_logic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void app_trader_summary_clear(app_trader_summary_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
}

int app_trader_clamp_profile(int profile_id)
{
    if (profile_id < APP_TRADER_PROFILE_MIN) {
        return APP_TRADER_PROFILE_MIN;
    }
    if (profile_id > APP_TRADER_PROFILE_MAX) {
        return APP_TRADER_PROFILE_MAX;
    }
    return profile_id;
}

void app_trader_format_money(char *out, size_t cap, double value)
{
    if (!out || cap < 4) {
        return;
    }
    if (value >= 1000.0 || value <= -1000.0) {
        snprintf(out, cap, "%.0f", value);
    } else {
        snprintf(out, cap, "%.2f", value);
    }
}

void app_trader_format_signed(char *out, size_t cap, double value)
{
    if (!out || cap < 4) {
        return;
    }
    char tmp[32];
    app_trader_format_money(tmp, sizeof(tmp), value < 0 ? -value : value);
    if (value > 0.0005) {
        snprintf(out, cap, "+%s", tmp);
    } else if (value < -0.0005) {
        snprintf(out, cap, "-%s", tmp);
    } else {
        snprintf(out, cap, "%s", tmp);
    }
}

void app_trader_format_pct(char *out, size_t cap, double value, bool signed_pct)
{
    if (!out || cap < 4) {
        return;
    }
    if (signed_pct) {
        if (value > 0.0005) {
            snprintf(out, cap, "+%.2f%%", value);
        } else if (value < -0.0005) {
            snprintf(out, cap, "%.2f%%", value);
        } else {
            snprintf(out, cap, "0.00%%");
        }
    } else {
        snprintf(out, cap, "%.1f%%", value);
    }
}

static const char *find_key(const char *json, const char *key)
{
    if (!json || !key) {
        return NULL;
    }
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = json;
    size_t plen = strlen(pattern);
    while ((p = strstr(p, pattern)) != NULL) {
        const char *after = p + plen;
        while (*after == ' ' || *after == '\t' || *after == '\n' || *after == '\r') {
            after++;
        }
        if (*after == ':') {
            return after + 1;
        }
        p += plen;
    }
    return NULL;
}

static void skip_ws(const char **pp)
{
    const char *p = *pp;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    *pp = p;
}

static bool extract_string(const char *json, const char *key, char *out, size_t out_cap)
{
    if (!out || out_cap == 0) {
        return false;
    }
    out[0] = '\0';
    const char *p = find_key(json, key);
    if (!p) {
        return false;
    }
    skip_ws(&p);
    if (*p != '"') {
        return false;
    }
    p++;
    size_t o = 0;
    while (*p && *p != '"' && o + 1 < out_cap) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'n') {
                out[o++] = '\n';
            } else if (*p == 't') {
                out[o++] = '\t';
            } else {
                out[o++] = *p;
            }
            p++;
            continue;
        }
        out[o++] = *p++;
    }
    out[o] = '\0';
    return o > 0 || (*p == '"');
}

static bool extract_double(const char *json, const char *key, double *out)
{
    if (!out) {
        return false;
    }
    const char *p = find_key(json, key);
    if (!p) {
        return false;
    }
    skip_ws(&p);
    char *end = NULL;
    double v = strtod(p, &end);
    if (end == p) {
        return false;
    }
    *out = v;
    return true;
}

static bool extract_bool(const char *json, const char *key, bool *out)
{
    if (!out) {
        return false;
    }
    const char *p = find_key(json, key);
    if (!p) {
        return false;
    }
    skip_ws(&p);
    if (strncmp(p, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool extract_int(const char *json, const char *key, int *out)
{
    if (!out) {
        return false;
    }
    double v = 0;
    if (!extract_double(json, key, &v)) {
        return false;
    }
    *out = (int)v;
    return true;
}

static int parse_profile_num(const char *id)
{
    if (!id) {
        return 0;
    }
    if (strncmp(id, "profile", 7) != 0) {
        return 0;
    }
    int n = atoi(id + 7);
    return app_trader_clamp_profile(n);
}

bool app_trader_parse_summary(const char *json, app_trader_summary_t *out)
{
    if (!json || !out) {
        return false;
    }
    app_trader_summary_clear(out);

    bool ok = false;
    (void)extract_bool(json, "ok", &ok);
    out->ok = ok;

    (void)extract_string(json, "trader_id", out->trader_id, sizeof(out->trader_id));
    out->profile_id = parse_profile_num(out->trader_id);

    const char *payload = strstr(json, "\"payload\"");
    const char *scope = payload ? payload : json;

    (void)extract_string(scope, "short_name", out->short_name, sizeof(out->short_name));
    if (out->short_name[0] == '\0') {
        (void)extract_string(scope, "name", out->short_name, sizeof(out->short_name));
    }
    (void)extract_string(scope, "strategy", out->strategy, sizeof(out->strategy));
    (void)extract_string(scope, "status", out->status, sizeof(out->status));
    (void)extract_bool(scope, "running", &out->running);
    (void)extract_double(scope, "total_assets", &out->total_assets);
    (void)extract_double(scope, "pnl", &out->pnl);
    (void)extract_double(scope, "pnl_pct", &out->pnl_pct);
    (void)extract_double(scope, "cash", &out->cash);
    (void)extract_double(scope, "position_pct", &out->position_pct);
    (void)extract_int(scope, "holdings_count", &out->holdings_count);

    const char *day = strstr(scope, "\"day_summary\"");
    if (day) {
        (void)extract_double(day, "day_pnl", &out->day_pnl);
        (void)extract_double(day, "day_pnl_pct", &out->day_pnl_pct);
        (void)extract_double(day, "win_rate", &out->win_rate);
    }

    if (out->profile_id == 0 && out->short_name[0] == '\0' && !ok) {
        return false;
    }
    if (out->profile_id == 0) {
        out->profile_id = APP_TRADER_PROFILE_MIN;
    }
    return true;
}

void app_trader_positions_clear(app_trader_positions_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
}

void app_trader_trade_clear(app_trader_trade_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
}

static const char *find_array(const char *json, const char *key)
{
    const char *p = find_key(json, key);
    if (!p) {
        return NULL;
    }
    skip_ws(&p);
    if (*p != '[') {
        return NULL;
    }
    return p + 1;
}

static const char *next_object(const char *p)
{
    if (!p) {
        return NULL;
    }
    while (*p) {
        if (*p == '{') {
            return p;
        }
        if (*p == ']') {
            return NULL;
        }
        p++;
    }
    return NULL;
}

static const char *object_end(const char *obj)
{
    if (!obj || *obj != '{') {
        return NULL;
    }
    int depth = 0;
    bool in_str = false;
    for (const char *p = obj; *p; ++p) {
        if (in_str) {
            if (*p == '\\' && p[1]) {
                p++;
                continue;
            }
            if (*p == '"') {
                in_str = false;
            }
            continue;
        }
        if (*p == '"') {
            in_str = true;
            continue;
        }
        if (*p == '{') {
            depth++;
        } else if (*p == '}') {
            depth--;
            if (depth == 0) {
                return p + 1;
            }
        }
    }
    return NULL;
}

bool app_trader_parse_positions(const char *json, app_trader_positions_t *out)
{
    if (!json || !out) {
        return false;
    }
    app_trader_positions_clear(out);

    const char *arr = find_array(json, "positions");
    if (!arr) {
        return false;
    }

    const char *p = arr;
    while (out->count < APP_TRADER_POS_MAX) {
        const char *obj = next_object(p);
        if (!obj) {
            break;
        }
        const char *end = object_end(obj);
        if (!end) {
            break;
        }
        char tmp[256];
        size_t n = (size_t)(end - obj);
        if (n >= sizeof(tmp)) {
            n = sizeof(tmp) - 1;
        }
        memcpy(tmp, obj, n);
        tmp[n] = '\0';

        char name[APP_TRADER_POS_NAME_MAX];
        name[0] = '\0';
        (void)extract_string(tmp, "name", name, sizeof(name));
        if (name[0] == '\0') {
            (void)extract_string(tmp, "code", name, sizeof(name));
        }
        if (name[0]) {
            strncpy(out->names[out->count], name, APP_TRADER_POS_NAME_MAX - 1);
            out->names[out->count][APP_TRADER_POS_NAME_MAX - 1] = '\0';
            out->count++;
        }
        p = end;
    }
    return true;
}

bool app_trader_parse_latest_trade(const char *json, app_trader_trade_t *out)
{
    if (!json || !out) {
        return false;
    }
    app_trader_trade_clear(out);

    const char *arr = find_array(json, "trades");
    if (!arr) {
        return false;
    }
    const char *obj = next_object(arr);
    if (!obj) {
        return true; /* empty list is valid */
    }
    const char *end = object_end(obj);
    if (!end) {
        return false;
    }

    char tmp[512];
    size_t n = (size_t)(end - obj);
    if (n >= sizeof(tmp)) {
        n = sizeof(tmp) - 1;
    }
    memcpy(tmp, obj, n);
    tmp[n] = '\0';

    (void)extract_string(tmp, "side", out->side, sizeof(out->side));
    (void)extract_string(tmp, "name", out->name, sizeof(out->name));
    (void)extract_string(tmp, "code", out->code, sizeof(out->code));
    (void)extract_string(tmp, "trade_at", out->trade_at, sizeof(out->trade_at));
    if (out->trade_at[0] == '\0') {
        (void)extract_string(tmp, "created_at", out->trade_at, sizeof(out->trade_at));
    }
    (void)extract_double(tmp, "price", &out->price);
    (void)extract_int(tmp, "shares", &out->shares);
    out->valid = (out->name[0] != '\0' || out->code[0] != '\0');
    return true;
}
