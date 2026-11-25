// Who "/del x", "/block x", "/unblock x" mean (Weili 2026-11-24: "why not
// /del <userid> and /block <userid> or ens name? number need /list first").
// Pure functions, unit-tested in linux/tools/test_member_target.cpp.
#ifndef CHATROBOT_MEMBER_TARGET_H
#define CHATROBOT_MEMBER_TARGET_H

#include <algorithm>
#include <string>
#include <vector>

#include "MemberList.h"

namespace chatrobot {

    struct TargetCandidate {
        int index;            // /list number (or block-list number)
        std::string userid;
        std::string name;     // display name
    };

    struct TargetMatch {
        enum Kind { None, One, Many, NeedEns } kind = None;
        std::vector<TargetCandidate> matches;
        std::string ens_name;    // NeedEns: the full name to look up, lower case
    };

    constexpr size_t kTargetPrefixMin = 8;
    constexpr size_t kTargetListMax = 10;

    inline bool isBase58Text(const std::string &s) {
        static const std::string a = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
        if (s.empty()) return false;
        for (char c : s) if (a.find(c) == std::string::npos) return false;
        return true;
    }

    inline std::string asciiLower(std::string s) {
        for (auto &c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return s;
    }

    inline std::string trimSpaces(const std::string &s) {
        size_t b = s.find_first_not_of(" \t");
        if (b == std::string::npos) return "";
        size_t e = s.find_last_not_of(" \t");
        return s.substr(b, e - b + 1);
    }

    // Everything between the command and the sender userid Do() appends.
    inline std::string targetArg(const std::vector<std::string> &args) {
        std::string out;
        for (size_t i = 1; i + 1 < args.size(); i++) {
            if (!out.empty()) out += " ";
            out += args[i];
        }
        return out;
    }

    inline bool isBeaglesEthName(const std::string &q) {
        const std::string l = asciiLower(q);
        const std::string suf = ".beagles.eth";
        return l.size() > suf.size() && l.compare(l.size() - suf.size(), suf.size(), suf) == 0
               && l.find(' ') == std::string::npos;
    }

    // number | userid | unique userid prefix (8+) | display name (case-insensitive)
    // | <x>.beagles.eth (NeedEns when no display name matches).
    inline TargetMatch resolveTarget(std::string q, const std::vector<TargetCandidate> &all) {
        TargetMatch r;
        q = trimSpaces(q);
        if (!q.empty() && q[0] == '@') q = q.substr(1);
        if (q.empty()) return r;

        if (q.size() <= 9 && std::all_of(q.begin(), q.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            const int n = std::atoi(q.c_str());
            for (const auto &c : all) if (c.index == n) { r.kind = TargetMatch::One; r.matches.push_back(c); }
            return r;
        }
        for (const auto &c : all) {
            if (c.userid == q) { r.kind = TargetMatch::One; r.matches = {c}; return r; }
        }
        const std::string ql = asciiLower(q);
        const bool prefix_ok = q.size() >= kTargetPrefixMin && isBase58Text(q);
        for (const auto &c : all) {
            const bool by_prefix = prefix_ok && c.userid.compare(0, q.size(), q) == 0;
            const bool by_name = asciiLower(trimSpaces(c.name)) == ql;
            if (by_prefix || by_name) r.matches.push_back(c);
        }
        if (r.matches.size() == 1) r.kind = TargetMatch::One;
        else if (r.matches.size() > 1) r.kind = TargetMatch::Many;
        else if (isBeaglesEthName(q)) { r.kind = TargetMatch::NeedEns; r.ens_name = ql; }
        return r;
    }

    inline std::string shortUserId(const std::string &u) {
        return u.size() > 8 ? u.substr(0, 8) + "\xE2\x80\xA6" : u;
    }

    // "rio" matches 3 members: ... Type /del <number>.   (under 1 KB)
    inline std::string formatTargetMatches(const std::string &verb, const std::string &query,
                                           const std::vector<TargetCandidate> &m) {
        std::string out = "\"" + memberListName(query, 40) + "\" matches " + std::to_string(m.size()) +
                          " members, so nothing was done:\n";
        for (size_t i = 0; i < m.size() && i < kTargetListMax; i++) {
            out += std::to_string(m[i].index) + ": " + memberListName(m[i].name, kMemberListNameBytes) +
                   " (" + shortUserId(m[i].userid) + ")\n";
        }
        if (m.size() > kTargetListMax) out += "\xE2\x80\xA6 and " + std::to_string(m.size() - kTargetListMax) + " more\n";
        out += "Type /" + verb + " <number> or the full userid.";
        return out;
    }

}  // namespace chatrobot

#endif
