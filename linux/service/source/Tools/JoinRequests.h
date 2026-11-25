// Join requests held for the creator's approval (Weili 2026-11-24: "anyone ...
// need approval by the group admin"). Pure text helpers, unit-tested in
// linux/tools/test_join_requests.cpp. Storage: DatabaseProxy join_request_table.
#ifndef CHATROBOT_JOIN_REQUESTS_H
#define CHATROBOT_JOIN_REQUESTS_H

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "MemberList.h"

namespace chatrobot {

    struct JoinRequest {
        int64_t id = 0;            // the number /approve and /reject take
        std::string friendid;      // never shown to the creator in the notice text
        std::string name;
        std::string hello;
        int64_t requested_at_ms = 0;
    };

    constexpr size_t kJoinPendingMax = 200;       // held at once per group; more are dropped
    constexpr size_t kJoinPageSize = 10;
    constexpr size_t kJoinNameBytes = 32;
    constexpr size_t kJoinHelloBytes = 300;       // in the notice
    constexpr size_t kJoinHelloListBytes = 40;    // in /pending
    constexpr size_t kJoinStoredBytes = 512;      // name / hello as stored
    constexpr int64_t kJoinRejectQuietMs = 7LL * 24 * 60 * 60 * 1000;   // after /reject

    // Per-group join mode (Weili: "configurable by admin, other groups are open").
    // Stored in service_text "join.mode"; anything but "approve" is open.
    inline bool joinModeIsApprove(const std::string &stored) { return stored == "approve"; }

    // "/join" -> "" (show), "/join open" -> "open", "/join approve" -> "approve",
    // anything else -> "?" (usage).
    inline std::string joinModeArg(const std::vector<std::string> &args) {
        if (args.size() < 3) return "";
        std::string a = args[1];
        for (auto &c : a) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (a == "open" || a == "approve") return a;
        return "?";
    }

    inline std::string joinModeText(bool approve) {
        return approve ? "Joining this group needs your approval (/join approve). "
                         "Requests wait in /pending. Type /join open to let anyone in."
                       : "Anyone can join this group (/join open). "
                         "Type /join approve to approve each join request yourself.";
    }

    // "/approve 12" -> 12; -1 when missing or not a positive number.
    // Do() appends the sender's userid, so the number is args[1] of 3.
    inline int64_t joinRequestNumberArg(const std::vector<std::string> &args) {
        if (args.size() < 3) return -1;
        std::string a = args[1];
        if (!a.empty() && a[0] == '#') a = a.substr(1);
        if (a.empty() || a.size() > 12) return -1;
        for (char c : a) if (c < '0' || c > '9') return -1;
        const long long v = std::atoll(a.c_str());
        return v < 1 ? -1 : static_cast<int64_t>(v);
    }

    inline std::string joinRequestNotice(const JoinRequest &r) {
        const std::string n = std::to_string(r.id);
        std::string out = "Join request #" + n + " from " + memberListName(r.name, kJoinNameBytes);
        if (!r.hello.empty()) out += ": \"" + memberListName(r.hello, kJoinHelloBytes) + "\"";
        out += "\nType /approve " + n + " to let them in, /reject " + n + " to drop it. /pending lists all.";
        return out;
    }

    inline std::string joinPendingSummary(size_t count) {
        return std::to_string(count) + (count == 1 ? " join request is" : " join requests are") +
               " waiting for you. Type /pending.";
    }

    inline std::string formatPendingPage(const std::vector<JoinRequest> &all, size_t page) {
        if (all.empty()) return "No join requests waiting.";
        const size_t total = all.size();
        const size_t pages = (total + kJoinPageSize - 1) / kJoinPageSize;
        if (page < 1) page = 1;
        if (page > pages) {
            return "No page " + std::to_string(page) + ". " + std::to_string(total) + " join requests, " +
                   std::to_string(pages) + (pages == 1 ? " page" : " pages") + ". Type /pending 1.";
        }
        const size_t first = (page - 1) * kJoinPageSize;
        const size_t last = first + kJoinPageSize < total ? first + kJoinPageSize : total;
        std::string out = "Join requests " + std::to_string(first + 1) + "\xE2\x80\x93" + std::to_string(last) +
                          " of " + std::to_string(total) + ":\n";
        for (size_t i = first; i < last; i++) {
            const JoinRequest &r = all[i];
            out += "#" + std::to_string(r.id) + " " + memberListName(r.name, kJoinNameBytes);
            if (!r.hello.empty()) out += ": " + memberListName(r.hello, kJoinHelloListBytes);
            out += "\n";
        }
        if (last < total) {
            out += "\xE2\x80\xA6 and " + std::to_string(total - last) + " more. Type /pending " +
                   std::to_string(page + 1) + ".\n";
        }
        out += "Type /approve <n> or /reject <n>.";
        return out;
    }

    // Cut what a stranger sends before it is stored (UTF-8 safe, no "…").
    inline std::string joinStoredText(const std::string &s) {
        if (s.size() <= kJoinStoredBytes) return s;
        size_t n = kJoinStoredBytes;
        while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) n--;
        return s.substr(0, n);
    }

}  // namespace chatrobot

#endif
