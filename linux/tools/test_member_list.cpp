// Unit tests for MemberList.h (feedback#26). Build:
//   g++ -std=c++14 -I linux/service/source/Tools -o test_member_list linux/tools/test_member_list.cpp
#include "MemberList.h"

#include <cstdio>

using namespace chatrobot;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) { g_pass++; } else { g_fail++; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static size_t lines(const std::string &s) {
    size_t n = 1;
    for (char c : s) if (c == '\n') n++;
    return n;
}

static bool validUtf8(const std::string &s) {
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + n > s.size()) return false;
        for (size_t k = 1; k < n; k++) if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += n;
    }
    return true;
}

int main() {
    // Group 1 shape: 507 members, 12 online.
    std::vector<MemberLine> g1;
    for (int i = 1; i <= 507; i++) g1.push_back({i, "member " + std::to_string(i), i % 42 == 0});

    std::string p1 = formatMemberPage(g1, 1);
    CHECK(p1.find("Members 1\xE2\x80\x93" "20 of 507 (12 online)\n") == 0);
    CHECK(p1.find("1: member 1\n") != std::string::npos);
    CHECK(p1.find("20: member 20\n") != std::string::npos);
    CHECK(p1.find("21: member 21") == std::string::npos);
    CHECK(p1.find("\xE2\x80\xA6 and 487 more. Type /list 2.") != std::string::npos);
    CHECK(lines(p1) == 22);                                  // header + 20 + footer

    std::string p26 = formatMemberPage(g1, 26);              // last page: 501..507
    CHECK(p26.find("Members 501\xE2\x80\x93" "507 of 507") == 0);
    CHECK(p26.find("507: member 507") != std::string::npos);
    CHECK(p26.find("more") == std::string::npos);
    CHECK(p26.back() != '\n');
    CHECK(formatMemberPage(g1, 27) == "No page 27. 507 members, 26 pages. Type /list 1.");
    CHECK(formatMemberPage(g1, 3).find("42: member 42 (online)\n") != std::string::npos);

    // Worst case stays under one Carrier message: 5-digit indexes, every name
    // long and 3-byte UTF-8, everyone online.
    std::vector<MemberLine> worst;
    std::string longname;
    for (int k = 0; k < 40; k++) longname += "\xE5\x90\x8D";   // 名 x40 = 120 bytes
    for (int i = 0; i < 60; i++) worst.push_back({99990 + i, longname, true});
    for (size_t page = 1; page <= 3; page++) {
        std::string p = formatMemberPage(worst, page);
        CHECK(p.size() <= kMemberListMaxReply);
        CHECK(validUtf8(p));
    }
    std::printf("worst-case page: %zu bytes\n", formatMemberPage(worst, 1).size());
    std::printf("group-1 page 1: %zu bytes\n", p1.size());

    // Names: truncated at a UTF-8 boundary, control characters removed.
    CHECK(memberListName("Alice", 24) == "Alice");
    CHECK(memberListName("", 24) == "(no name)");
    CHECK(memberListName("a\nb\tc", 24) == "a b c");
    std::string cut = memberListName(longname, 24);
    CHECK(validUtf8(cut) && cut.size() <= 24 + 3);
    CHECK(cut.substr(cut.size() - 3) == "\xE2\x80\xA6");

    // Small groups: one page, no footer.
    std::vector<MemberLine> g2 = {{1, "creator", true}, {2, "bob", false}};
    CHECK(formatMemberPage(g2, 1) == "Members 1\xE2\x80\x93" "2 of 2 (1 online)\n1: creator (online)\n2: bob");
    CHECK(formatMemberPage({}, 1) == "No members.");

    // Page argument: Do() appends the sender userid as the last arg.
    CHECK(memberListPageArg({"list", "USER"}) == 1);
    CHECK(memberListPageArg({"list", "2", "USER"}) == 2);
    CHECK(memberListPageArg({"l", "26", "USER"}) == 26);
    CHECK(memberListPageArg({"list", "x", "USER"}) == 1);
    CHECK(memberListPageArg({"list", "0", "USER"}) == 1);
    CHECK(memberListPageArg({"list", "-3", "USER"}) == 1);
    CHECK(memberListPageArg({"list", "99999999", "USER"}) == 1);

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
