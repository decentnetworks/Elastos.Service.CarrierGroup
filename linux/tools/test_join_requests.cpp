// Unit tests for JoinRequests.h. Build:
//   g++ -std=c++14 -I linux/service/source/Tools -o test_join_requests linux/tools/test_join_requests.cpp
#include "JoinRequests.h"

#include <cstdio>

using namespace chatrobot;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) { g_pass++; } else { g_fail++; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main() {
    JoinRequest r;
    r.id = 12; r.friendid = "9qJwpGJDxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"; r.name = "rio"; r.hello = "hi, I'd like to join";
    std::string n = joinRequestNotice(r);
    CHECK(n == "Join request #12 from rio: \"hi, I'd like to join\"\n"
               "Type /approve 12 to let them in, /reject 12 to drop it. /pending lists all.");
    CHECK(n.find(r.friendid) == std::string::npos);

    // Worst-case notice: long multi-byte name and hello, newlines in them.
    JoinRequest w;
    w.id = 999999999;
    for (int i = 0; i < 200; i++) { w.name += "\xE5\x90\x8D"; w.hello += "\xE5\x90\x8D\n"; }
    std::string wn = joinRequestNotice(w);
    CHECK(wn.size() < 1000);
    CHECK(wn.find("\n") == wn.find("\nType /approve"));   // the only newline is the separator
    r.hello = "";
    CHECK(joinRequestNotice(r).find("rio\nType") != std::string::npos);

    // /pending pages stay under 1 KB.
    std::vector<JoinRequest> all;
    for (int i = 0; i < 25; i++) { JoinRequest x = w; x.id = 100000 + i; all.push_back(x); }
    for (size_t p = 1; p <= 3; p++) CHECK(formatPendingPage(all, p).size() < 1000);
    std::printf("worst /pending page %zu bytes, worst notice %zu bytes\n", formatPendingPage(all, 1).size(), wn.size());
    CHECK(formatPendingPage(all, 1).find("Join requests 1\xE2\x80\x93" "10 of 25:\n#100000 ") == 0);
    CHECK(formatPendingPage(all, 1).find("and 15 more. Type /pending 2.") != std::string::npos);
    CHECK(formatPendingPage(all, 3).find("Join requests 21\xE2\x80\x93" "25 of 25") == 0);
    CHECK(formatPendingPage(all, 3).find("more") == std::string::npos);
    CHECK(formatPendingPage(all, 4) == "No page 4. 25 join requests, 3 pages. Type /pending 1.");
    CHECK(formatPendingPage({}, 1) == "No join requests waiting.");

    CHECK(joinPendingSummary(1) == "1 join request is waiting for you. Type /pending.");
    CHECK(joinPendingSummary(3) == "3 join requests are waiting for you. Type /pending.");

    CHECK(joinRequestNumberArg({"approve", "12", "U"}) == 12);
    CHECK(joinRequestNumberArg({"approve", "#7", "U"}) == 7);
    CHECK(joinRequestNumberArg({"approve", "U"}) == -1);
    CHECK(joinRequestNumberArg({"approve", "x", "U"}) == -1);
    CHECK(joinRequestNumberArg({"approve", "0", "U"}) == -1);
    CHECK(joinRequestNumberArg({"reject", "-1", "U"}) == -1);

    std::string big(2000, 'a');
    CHECK(joinStoredText(big).size() == kJoinStoredBytes);
    std::string mb; for (int i = 0; i < 300; i++) mb += "\xE5\x90\x8D";
    std::string cut = joinStoredText(mb);
    CHECK(cut.size() <= kJoinStoredBytes && cut.size() % 3 == 0);
    CHECK(joinStoredText("short") == "short");

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
