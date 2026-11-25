// Unit tests for MemberTarget.h. Build:
//   g++ -std=c++14 -I linux/service/source/Tools -o test_member_target linux/tools/test_member_target.cpp
#include "MemberTarget.h"

#include <cstdio>

using namespace chatrobot;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) { g_pass++; } else { g_fail++; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main() {
    const std::vector<TargetCandidate> m = {
        {1, "7UNFCSSc1111111111111111111111111111111111", "e2e-B"},
        {2, "E1tkwT93jGoaxigBKLmEUbri4sssAVukbSZPvdxKRyVf", "Rio"},
        {3, "E1tkwT94aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "rio"},
        {4, "AyAhKU5i2222222222222222222222222222222222", "sim.beagles.eth"},
        {12, "BMm4Yz3QKuLCyD3SiuPzRTvednHoavQFrcygAuBxNq61", "weili wang"},
    };
    // number
    auto r = resolveTarget("12", m);
    CHECK(r.kind == TargetMatch::One && r.matches[0].userid[0] == 'B');
    CHECK(resolveTarget("99", m).kind == TargetMatch::None);
    CHECK(resolveTarget(" 4 ", m).kind == TargetMatch::One);
    // full userid
    r = resolveTarget("E1tkwT93jGoaxigBKLmEUbri4sssAVukbSZPvdxKRyVf", m);
    CHECK(r.kind == TargetMatch::One && r.matches[0].index == 2);
    CHECK(resolveTarget("@E1tkwT93jGoaxigBKLmEUbri4sssAVukbSZPvdxKRyVf", m).kind == TargetMatch::One);
    // prefix (8+), unique and ambiguous; 7 chars is not a prefix
    r = resolveTarget("BMm4Yz3Q", m);
    CHECK(r.kind == TargetMatch::One && r.matches[0].index == 12);
    r = resolveTarget("E1tkwT9", m);
    CHECK(r.kind == TargetMatch::None);
    r = resolveTarget("E1tkwT93", m);
    CHECK(r.kind == TargetMatch::One && r.matches[0].index == 2);
    r = resolveTarget("E1tkwT9a", m);
    CHECK(r.kind == TargetMatch::None);
    // name, case-insensitive, ambiguous
    r = resolveTarget("RIO", m);
    CHECK(r.kind == TargetMatch::Many && r.matches.size() == 2);
    r = resolveTarget("weili wang", m);
    CHECK(r.kind == TargetMatch::One && r.matches[0].index == 12);
    r = resolveTarget("Sim.Beagles.eth", m);
    CHECK(r.kind == TargetMatch::One && r.matches[0].index == 4);
    // beagles.eth name not among display names -> look it up
    r = resolveTarget("Liwei.beagles.eth", m);
    CHECK(r.kind == TargetMatch::NeedEns && r.ens_name == "liwei.beagles.eth");
    CHECK(resolveTarget("nobody", m).kind == TargetMatch::None);
    CHECK(resolveTarget("", m).kind == TargetMatch::None);
    CHECK(resolveTarget("a b.beagles.eth", m).kind == TargetMatch::None);

    // ambiguous reply stays under 1 KB even with many long matches
    std::vector<TargetCandidate> many;
    std::string longname; for (int i = 0; i < 60; i++) longname += "\xE5\x90\x8D";
    for (int i = 0; i < 40; i++) many.push_back({1000 + i, "E1tkwT93" + std::string(36, 'x'), longname});
    std::string rep = formatTargetMatches("block", longname, many);
    CHECK(rep.size() < 1000);
    CHECK(rep.find("and 30 more") != std::string::npos);
    CHECK(rep.find("Type /block <number>") != std::string::npos);
    std::printf("worst ambiguity reply %zu bytes\n", rep.size());
    rep = formatTargetMatches("del", "rio", resolveTarget("rio", m).matches);
    CHECK(rep == "\"rio\" matches 2 members, so nothing was done:\n2: Rio (E1tkwT93\xE2\x80\xA6)\n3: rio (E1tkwT94\xE2\x80\xA6)\n"
                 "Type /del <number> or the full userid.");

    CHECK(targetArg({"del", "weili", "wang", "SENDER"}) == "weili wang");
    CHECK(targetArg({"del", "SENDER"}) == "");

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
