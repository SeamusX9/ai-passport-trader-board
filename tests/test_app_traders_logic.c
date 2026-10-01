#include "app_traders_logic.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void test_clamp(void)
{
    assert(app_trader_clamp_profile(0) == 1);
    assert(app_trader_clamp_profile(3) == 3);
    assert(app_trader_clamp_profile(9) == 5);
}

static void test_format(void)
{
    char buf[32];
    app_trader_format_money(buf, sizeof(buf), 504530.15);
    assert(strcmp(buf, "504530") == 0);
    app_trader_format_signed(buf, sizeof(buf), 4530.15);
    assert(buf[0] == '+');
    app_trader_format_signed(buf, sizeof(buf), -12.5);
    assert(buf[0] == '-');
    app_trader_format_pct(buf, sizeof(buf), 0.91, true);
    assert(strstr(buf, "0.91%") != NULL);
}

static void test_parse(void)
{
    const char *sample =
        "{\"ok\":true,\"trader_id\":\"profile2\","
        "\"payload\":{"
        "\"short_name\":\"保守操盘\","
        "\"strategy\":\"均衡型\","
        "\"status\":\"stopped\","
        "\"running\":false,"
        "\"total_assets\":504530.15,"
        "\"pnl\":4530.15,"
        "\"pnl_pct\":0.91,"
        "\"cash\":90521.15,"
        "\"position_pct\":82.1,"
        "\"holdings_count\":5,"
        "\"day_summary\":{"
        "\"day_pnl\":1385.94,"
        "\"day_pnl_pct\":0.28,"
        "\"win_rate\":75.0"
        "}}}";

    app_trader_summary_t s;
    assert(app_trader_parse_summary(sample, &s));
    assert(s.ok);
    assert(s.profile_id == 2);
    assert(strcmp(s.trader_id, "profile2") == 0);
    assert(strcmp(s.short_name, "保守操盘") == 0);
    assert(strcmp(s.strategy, "均衡型") == 0);
    assert(!s.running);
    assert(fabs(s.total_assets - 504530.15) < 0.01);
    assert(fabs(s.pnl - 4530.15) < 0.01);
    assert(fabs(s.day_pnl - 1385.94) < 0.01);
    assert(s.holdings_count == 5);
}

static void test_parse_positions_trade(void)
{
    const char *pos_json =
        "{\"ok\":true,\"payload\":{\"positions\":["
        "{\"code\":\"000725\",\"name\":\"京东方A\"},"
        "{\"code\":\"002241\",\"name\":\"歌尔股份\"}"
        "]}}";
    app_trader_positions_t pos;
    assert(app_trader_parse_positions(pos_json, &pos));
    assert(pos.count == 2);
    assert(strcmp(pos.names[0], "京东方A") == 0);
    assert(strcmp(pos.names[1], "歌尔股份") == 0);

    const char *trade_json =
        "{\"ok\":true,\"payload\":{\"trades\":["
        "{\"id\":48,\"code\":\"600721\",\"name\":\"百花医药\","
        "\"side\":\"买入\",\"price\":13.22,\"shares\":100,"
        "\"trade_at\":\"2026-09-30 13:56:39\"},"
        "{\"id\":47,\"code\":\"000002\",\"name\":\"万科A\",\"side\":\"卖出\"}"
        "]}}";
    app_trader_trade_t tr;
    assert(app_trader_parse_latest_trade(trade_json, &tr));
    assert(tr.valid);
    assert(strcmp(tr.side, "买入") == 0);
    assert(strcmp(tr.name, "百花医药") == 0);
    assert(tr.shares == 100);
    assert(fabs(tr.price - 13.22) < 0.001);
}

int main(void)
{
    test_clamp();
    test_format();
    test_parse();
    test_parse_positions_trade();
    printf("test_app_traders_logic: PASS\n");
    return 0;
}
