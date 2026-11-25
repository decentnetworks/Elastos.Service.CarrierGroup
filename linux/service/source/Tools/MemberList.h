// /list replies (feedback#26). Pure functions, unit-tested in
// linux/tools/test_member_list.cpp.
//
// The old /list sent every member in one message: 16,699 bytes for the
// 507 members of group 1 on 2026-11-24, more than 16 times the 1,024 bytes
// Carrier sends as one message. It now sends one page of at most
// kMemberListPageSize members, well under that limit, and "/list <n>" pages.
#ifndef CHATROBOT_MEMBER_LIST_H
#define CHATROBOT_MEMBER_LIST_H

#include <cstdlib>
#include <string>
#include <vector>

namespace chatrobot {

    struct MemberLine {
        int index;          // what /block and /del take
        std::string name;   // display name, never the userid
        bool online;
    };

    constexpr size_t kMemberListPageSize = 20;
    constexpr size_t kMemberListNameBytes = 24;
    constexpr size_t kMemberListMaxReply = 1000;   // < CARRIER_MAX_APP_MESSAGE_LEN (1024)

    // At most max_bytes of s, cut at a UTF-8 character boundary, "…" appended
    // when cut. Control characters (a newline in a name) become spaces.
    inline std::string memberListName(const std::string &s, size_t max_bytes) {
        std::string clean;
        clean.reserve(s.size());
        for (unsigned char c : s) clean.push_back(c < 0x20 || c == 0x7f ? ' ' : static_cast<char>(c));
        if (clean.empty()) clean = "(no name)";
        if (clean.size() <= max_bytes) return clean;
        size_t n = max_bytes;
        while (n > 0 && (static_cast<unsigned char>(clean[n]) & 0xC0) == 0x80) n--;
        return clean.substr(0, n) + "\xE2\x80\xA6";
    }

    // "/list", "/list 2": the page asked for; 1 when absent or not a number.
    inline size_t memberListPageArg(const std::vector<std::string> &args) {
        // Do() appends the sender's userid, so a page number is args[1] of 3.
        if (args.size() < 3) return 1;
        const std::string &a = args[1];
        if (a.empty() || a.size() > 6) return 1;
        for (char c : a) if (c < '0' || c > '9') return 1;
        const long v = std::atol(a.c_str());
        return v < 1 ? 1 : static_cast<size_t>(v);
    }

    inline std::string formatMemberPage(const std::vector<MemberLine> &all, size_t page,
                                        size_t per_page = kMemberListPageSize,
                                        size_t name_bytes = kMemberListNameBytes) {
        const size_t total = all.size();
        size_t online = 0;
        for (const auto &m : all) if (m.online) online++;
        const size_t pages = total == 0 ? 1 : (total + per_page - 1) / per_page;
        if (page < 1) page = 1;
        if (page > pages) {
            return "No page " + std::to_string(page) + ". " + std::to_string(total) + " members, " +
                   std::to_string(pages) + (pages == 1 ? " page" : " pages") + ". Type /list 1.";
        }
        const size_t first = (page - 1) * per_page;
        const size_t last = first + per_page < total ? first + per_page : total;   // exclusive
        std::string out;
        if (total == 0) return "No members.";
        out += "Members " + std::to_string(first + 1) + "\xE2\x80\x93" + std::to_string(last) +
               " of " + std::to_string(total) + " (" + std::to_string(online) + " online)\n";
        for (size_t i = first; i < last; i++) {
            const MemberLine &m = all[i];
            out += std::to_string(m.index) + ": " + memberListName(m.name, name_bytes) +
                   (m.online ? " (online)" : "") + "\n";
        }
        if (last < total) {
            out += "\xE2\x80\xA6 and " + std::to_string(total - last) + " more. Type /list " +
                   std::to_string(page + 1) + ".";
        } else if (!out.empty() && out.back() == '\n') {
            out.pop_back();
        }
        return out;
    }

}  // namespace chatrobot

#endif
