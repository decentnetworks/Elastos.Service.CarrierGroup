// Unit tests for FloodGuard (feedback#21). Build:
//   g++ -std=c++14 -O1 -I. -I<json.hpp dir> -o test_floodguard test_floodguard.cpp FloodGuard.cpp
// Run: ./test_floodguard [gojibot-1124.tsv]
#include "FloodGuard.h"
#include <json.hpp>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>

using chatrobot::FloodConfig;
using chatrobot::FloodDecision;
using chatrobot::FloodGuard;
using V = FloodDecision::Verdict;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) { g_pass++; } else { g_fail++; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const int64_t S = 1000, MIN = 60 * S, H = 60 * MIN, D = 24 * H;

static std::string txt(int i) { return "distinct message number " + std::string(1, char('a' + i % 26)) + std::string(i / 26 + 1, 'x'); }

static void testRatePerSecond() {
    FloodGuard g;
    int64_t t = 1000 * D;
    CHECK(g.check("A", txt(0), true, t, false).forward());
    CHECK(g.check("A", txt(1), true, t + 100, false).forward());
    auto d3 = g.check("A", txt(2), true, t + 200, false);
    CHECK(d3.verdict == V::DropRate);
    CHECK(!d3.notice.empty());
    auto d4 = g.check("A", txt(3), true, t + 300, false);
    CHECK(d4.verdict == V::DropRate);
    CHECK(d4.notice.empty());                       // one notice per window
    CHECK(g.check("A", txt(4), true, t + 1100, false).forward());
    CHECK(g.check("B", txt(5), true, t + 300, false).forward());   // per member
}

static void testRatePerMinute() {
    FloodGuard g;
    int64_t t = 1000 * D;
    int fwd = 0;
    for (int i = 0; i < 21; i++) fwd += g.check("A", txt(i), true, t + i * 2 * S, false).forward();
    CHECK(fwd == 20);
    CHECK(g.check("A", txt(30), true, t + 61 * S, false).forward());   // first one aged out
}

static void testDuplicates() {
    FloodGuard g;
    int64_t t = 1000 * D;
    const std::string a = "the quick brown fox jumps";
    CHECK(g.check("A", a, true, t, false).forward());
    auto d = g.check("A", a, true, t + 5 * MIN, false);
    CHECK(d.verdict == V::DropDuplicate);
    CHECK(!d.notice.empty());
    CHECK(g.check("A", a, true, t + 11 * MIN, false).forward());
    // anyone within 1 min
    const std::string b = "breaking news everybody look";
    CHECK(g.check("A", b, true, t + 20 * MIN, false).forward());
    CHECK(g.check("B", b, true, t + 20 * MIN + 30 * S, false).verdict == V::DropDuplicate);
    CHECK(g.check("C", b, true, t + 22 * MIN, false).forward());
    // similar: numbers and whitespace ignored, same member, 5 min
    CHECK(g.check("D", "BTC price 63,829.7 now", true, t, false).forward());
    CHECK(g.check("D", "BTC  price 64,001.2 now", true, t + 4 * MIN, false).verdict == V::DropDuplicate);
    CHECK(g.check("D", "BTC price 64,500.0 now", true, t + 10 * MIN, false).forward());
    CHECK(g.check("E", "BTC price 64,777.0 now", true, t + 10 * MIN + S, false).forward()); // similar is per member
    // short texts exempt
    FloodGuard g2;
    CHECK(g2.check("A", "ok", true, t, false).forward());
    CHECK(g2.check("A", "ok", true, t + 2 * S, false).forward());
    CHECK(g2.check("B", "ok", true, t + 3 * S, false).forward());
    // 10 characters counted as code points, not bytes
    CHECK(FloodGuard::codePoints("好的好的好的") == 6);
    CHECK(g2.check("A", "好的好的好的", true, t + 10 * S, false).forward());
    CHECK(g2.check("A", "好的好的好的", true, t + 20 * S, false).forward());
    // trailing date marker stripped
    FloodGuard g3;
    CHECK(g3.check("A", "hello world again [2026-11-24 10:00:00]", true, t, false).forward());
    CHECK(g3.check("A", "hello world again [2026-11-24 10:00:05]", true, t + 5 * S, false).verdict == V::DropDuplicate);
    CHECK(FloodGuard::stripMarker("hi there friend \n[2026-11-24 10:00:00 UTC]") == "hi there friend");
    CHECK(FloodGuard::stripMarker("no marker here") == "no marker here");
    CHECK(FloodGuard::stripMarker("[2026-11-24 10:00:00] leading only") == "[2026-11-24 10:00:00] leading only");
    // file messages: rate only
    FloodGuard g4;
    CHECK(g4.check("A", "same-bytes-file-payload", false, t, false).forward());
    CHECK(g4.check("A", "same-bytes-file-payload", false, t + 2 * S, false).forward());
}

// Nonstop flooding (2 msgs every second, distinct texts) for 30 minutes.
static int64_t floodUntilMuted(FloodConfig cfg, int64_t *muted_after) {
    FloodGuard g(cfg);
    int64_t t0 = 1000 * D;
    for (int64_t ms = 0; ms < 30 * MIN; ms += 500) {
        auto d = g.check("A", txt(static_cast<int>(ms / 500)), true, t0 + ms, false);
        if (d.muted_now) { *muted_after = ms; return 1; }
    }
    return 0;
}

static void testEpisodesAndMute() {
    int64_t after = -1;
    CHECK(floodUntilMuted(FloodConfig(), &after) == 1);
    CHECK(after >= 20 * MIN && after < 21 * MIN);   // 3 episodes at 0, 10, 20 min
    FloodConfig per_drop;
    per_drop.count_mode = FloodConfig::CountMode::PerDrop;
    after = -1;
    CHECK(floodUntilMuted(per_drop, &after) == 1);
    CHECK(after < 60 * S);
    // three separate short floods within an hour mute
    FloodGuard g;
    int64_t t = 1000 * D;
    bool muted = false;
    for (int burst = 0; burst < 3; burst++) {
        int64_t b = t + burst * 15 * MIN;
        for (int i = 0; i < 5; i++) muted |= g.check("A", txt(burst * 10 + i), true, b + i * 100, false).muted_now;
    }
    CHECK(muted);
    auto d = g.check("A", txt(99), true, t + 40 * MIN, false);
    CHECK(d.verdict == V::DropMuted);
    CHECK(g.check("A", txt(98), true, t + 40 * MIN + S, false).notice.empty());  // reminder at most hourly
    CHECK(g.check("A", txt(97), true, t + 30 * MIN + 25 * H, false).forward());   // mute over
}

static bool muteOnce(FloodGuard &g, const std::string &who, int64_t t, bool never_block, bool *blocked) {
    bool muted = false;
    for (int burst = 0; burst < 3; burst++) {
        for (int i = 0; i < 5; i++) {
            auto d = g.check(who, txt(burst * 10 + i), true, t + burst * 15 * MIN + i * 100, never_block);
            muted |= d.muted_now;
            *blocked |= d.block_now;
        }
    }
    return muted;
}

static void testBlock() {
    FloodGuard g;
    int64_t t = 1000 * D;
    bool blocked = false;
    CHECK(muteOnce(g, "A", t, false, &blocked));
    CHECK(muteOnce(g, "A", t + 2 * D, false, &blocked));
    CHECK(!blocked);
    CHECK(muteOnce(g, "A", t + 4 * D, false, &blocked));
    CHECK(blocked);
    // creator/signer: muted, never blocked
    FloodGuard g2;
    bool b2 = false;
    for (int k = 0; k < 3; k++) muteOnce(g2, "C", t + k * 2 * D, true, &b2);
    CHECK(!b2);
    // mutes older than 7 days do not count
    FloodGuard g3;
    bool b3 = false;
    muteOnce(g3, "A", t, false, &b3);
    muteOnce(g3, "A", t + 2 * D, false, &b3);
    muteOnce(g3, "A", t + 8 * D, false, &b3);
    CHECK(!b3);
    // seeded mutes survive a restart; clear() undoes them
    FloodGuard g4;
    g4.seedMute("A", t - D, t - D + 24 * H);
    g4.seedMute("A", t - 3 * D, t - 3 * D + 24 * H);
    bool b4 = false;
    muteOnce(g4, "A", t, false, &b4);
    CHECK(b4);
    g4.clear("A");
    CHECK(!g4.isMuted("A", t + 1));
    CHECK(g4.check("A", txt(77), true, t + H, false).forward());
}

static void testSeedForwarded() {
    FloodGuard g;
    int64_t t = 1000 * D;
    for (int i = 0; i < 120; i++) g.seedForwarded("A", txt(i), true, t - 50 * MIN + i * 20 * S, t);
    CHECK(g.check("A", txt(200), true, t, false).verdict == V::DropRate);  // 120/h already
    g.seedForwarded("B", "a long enough text here", true, t - 2 * MIN, t);
    g.seedForwarded("C", "an old enough text here", true, t - 20 * MIN, t);
    CHECK(g.check("C", "an old enough text here", true, t, false).forward());
    CHECK(g.check("B", "a long enough text here", true, t, false).verdict == V::DropDuplicate);
}

static void replayGojibot(const char *path, FloodConfig cfg, const char *label) {
    std::ifstream in(path);
    if (!in) { std::printf("skip replay: %s not found\n", path); return; }
    FloodGuard g(cfg);
    std::string line;
    int total = 0, burst_fwd = 0, burst_drop = 0, after_fwd = 0, after_drop = 0, notices = 0;
    bool muted = false, blocked = false;
    while (std::getline(in, line)) {
        size_t tab = line.find('\t');
        int64_t ts_ms = std::stoll(line.substr(0, tab)) / 1000;
        std::string text = nlohmann::json::parse(line.substr(tab + 1)).get<std::string>();
        auto d = g.check("8mtSvgSNa4juR9LUaEum6QahccuziaT87Km7uP3wQsGD", text, true, ts_ms, false);
        total++;
        if (!d.notice.empty()) notices++;
        muted |= d.muted_now;
        blocked |= d.block_now;
        // the burst: 09:53-09:56 UTC; everything after 10:00 is the hourly cadence
        std::time_t sec = ts_ms / 1000;
        std::tm tm{}; gmtime_r(&sec, &tm);
        bool in_burst = tm.tm_hour == 9;
        if (in_burst) { d.forward() ? burst_fwd++ : burst_drop++; }
        else { d.forward() ? after_fwd++ : after_drop++; }
    }
    std::printf("replay [%s]: %d msgs | burst forwarded %d dropped %d | hourly after forwarded %d dropped %d"
                " | notices %d | muted %d blocked %d\n",
                label, total, burst_fwd, burst_drop, after_fwd, after_drop, notices, muted, blocked);
    CHECK(total == 186);
    if (cfg.trip_hold_ms >= 10 * MIN) {
        CHECK(burst_fwd <= 20);          // decent-hq acceptance: only the first ~20
        CHECK(after_drop == 0 && after_fwd == 12);
        CHECK(!muted && !blocked);
        CHECK(notices == 1);
    } else if (cfg.count_mode == FloodConfig::CountMode::Episode) {
        CHECK(burst_drop >= 100);        // most of the backlog does not get through
        CHECK(after_drop == 0);          // hourly reports still pass
        CHECK(!muted && !blocked);
        CHECK(notices <= 15);            // one per window, not one per message
    } else {
        CHECK(burst_fwd <= 20);
        CHECK(muted);
    }
}

// The service config: a nonstop flooder is held throughout and muted at ~20 min;
// a member who pauses 10 min is forwarded again.
static void testServiceHold() {
    FloodConfig cfg;
    cfg.trip_hold_ms = 10 * MIN;
    int64_t after = -1;
    CHECK(floodUntilMuted(cfg, &after) == 1);
    CHECK(after >= 20 * MIN && after < 21 * MIN);
    FloodGuard g(cfg);
    int64_t t = 1000 * D;
    for (int i = 0; i < 3; i++) g.check("A", txt(i), true, t + i * 100, false);   // trips 2/s
    CHECK(g.check("A", txt(10), true, t + 5 * MIN, false).verdict == V::DropRate);  // still held
    CHECK(g.check("A", txt(11), true, t + 14 * MIN, false).verdict == V::DropRate); // 5->14 < 10 min quiet? no: 9 min
    CHECK(g.check("A", txt(12), true, t + 25 * MIN, false).forward());               // 11 min quiet
}

int main(int argc, char **argv) {
    testRatePerSecond();
    testRatePerMinute();
    testDuplicates();
    testEpisodesAndMute();
    testBlock();
    testSeedForwarded();
    const char *fixture = argc > 1 ? argv[1] : "gojibot-1124.tsv";
    replayGojibot(fixture, FloodConfig(), "episode (default)");
    FloodConfig per_drop;
    per_drop.count_mode = FloodConfig::CountMode::PerDrop;
    replayGojibot(fixture, per_drop, "per-drop (literal)");
    FloodConfig hold;
    hold.trip_hold_ms = 10 * MIN;
    replayGojibot(fixture, hold, "episode + hold until 10 min quiet (service)");
    testServiceHold();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
