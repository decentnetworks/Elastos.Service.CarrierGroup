#include "FloodGuard.h"

#include <algorithm>
#include <ctime>
#include <regex>

namespace chatrobot {

    namespace {
        std::string utcTime(int64_t ms) {
            std::time_t t = static_cast<std::time_t>(ms / 1000);
            std::tm tm{};
            gmtime_r(&t, &tm);
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M UTC", &tm);
            return buf;
        }

        std::string humanWait(int64_t ms) {
            int64_t s = (ms + 999) / 1000;
            if (s < 60) return std::to_string(s) + " s";
            if (s < 3600) return std::to_string((s + 59) / 60) + " min";
            if (s < 86400) return std::to_string((s + 3599) / 3600) + " h";
            return std::to_string((s + 86399) / 86400) + " days";
        }

        template <typename Map>
        void pruneOlder(Map &map, int64_t cutoff) {
            for (auto it = map.begin(); it != map.end();) {
                if (it->second <= cutoff) it = map.erase(it); else ++it;
            }
        }
    }

    FloodGuard::FloodGuard(FloodConfig config) : mConfig(std::move(config)) {}

    std::string FloodGuard::stripMarker(const std::string &text) {
        // Only the tail can hold the marker; never run the regex over a 5 MB text.
        static const std::regex kMarker(
                R"(\s*\[\d{4}[-/]\d{2}[-/]\d{2}[ T]\d{2}:\d{2}(:\d{2})?( ?[A-Za-z]{1,5}| ?[+-]\d{2}:?\d{2})?\]\s*$)");
        size_t start = text.size() > 64 ? text.size() - 64 : 0;
        std::string tail = text.substr(start);
        std::smatch m;
        std::string out = text;
        if (std::regex_search(tail, m, kMarker)) {
            out = text.substr(0, start + static_cast<size_t>(m.position(0)));
        }
        size_t end = out.find_last_not_of(" \t\r\n");
        return end == std::string::npos ? std::string() : out.substr(0, end + 1);
    }

    std::string FloodGuard::similarKey(const std::string &s) {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); i++) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if ((c >= '0' && c <= '9') || c == ' ' || c == '\t' || c == '\n' || c == '\r'
                || c == '\v' || c == '\f') {
                continue;
            }
            // Fullwidth digits U+FF10..FF19 (EF BC 90..99), ideographic space U+3000 (E3 80 80),
            // no-break space U+00A0 (C2 A0).
            if (c == 0xEF && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xBC
                && static_cast<unsigned char>(s[i + 2]) >= 0x90
                && static_cast<unsigned char>(s[i + 2]) <= 0x99) {
                i += 2;
                continue;
            }
            if (c == 0xE3 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80
                && static_cast<unsigned char>(s[i + 2]) == 0x80) {
                i += 2;
                continue;
            }
            if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA0) {
                i += 1;
                continue;
            }
            out += s[i];
        }
        return out;
    }

    size_t FloodGuard::codePoints(const std::string &s) {
        size_t n = 0;
        for (unsigned char c : s) {
            if ((c & 0xC0) != 0x80) n++;
        }
        return n;
    }

    bool FloodGuard::isMuted(const std::string &sender, int64_t now_ms) const {
        auto it = mMembers.find(sender);
        return it != mMembers.end() && it->second.muted_until > now_ms;
    }

    void FloodGuard::prune(Member &m, int64_t now_ms) {
        const int64_t longest = mConfig.windows.empty() ? 0 : mConfig.windows.back().ms;
        while (!m.forwarded.empty() && m.forwarded.front() <= now_ms - longest) m.forwarded.pop_front();
        while (!m.violations.empty() && m.violations.front() <= now_ms - mConfig.violation_window_ms)
            m.violations.pop_front();
        while (!m.mutes.empty() && m.mutes.front() <= now_ms - mConfig.mute_history_ms) m.mutes.pop_front();
        if (m.exact.size() > 32) pruneOlder(m.exact, now_ms - mConfig.dup_same_member_ms);
        if (m.similar.size() > 32) pruneOlder(m.similar, now_ms - mConfig.similar_same_member_ms);
        if (m.window_notice_until.size() != mConfig.windows.size())
            m.window_notice_until.assign(mConfig.windows.size(), 0);
    }

    void FloodGuard::pruneGlobal(int64_t now_ms) {
        if (now_ms - mLastGlobalPrune < 10000 && mAnyone.size() < 4096) return;
        mLastGlobalPrune = now_ms;
        pruneOlder(mAnyone, now_ms - mConfig.dup_anyone_ms);
    }

    void FloodGuard::record(Member &m, const std::string &stripped, bool dup_eligible, int64_t ts_ms) {
        if (m.forwarded.empty() || m.forwarded.back() <= ts_ms) {
            m.forwarded.push_back(ts_ms);
        } else {
            m.forwarded.insert(std::upper_bound(m.forwarded.begin(), m.forwarded.end(), ts_ms), ts_ms);
        }
        if (dup_eligible) {
            int64_t &e = m.exact[stripped];
            e = std::max(e, ts_ms);
            int64_t &s = m.similar[similarKey(stripped)];
            s = std::max(s, ts_ms);
            int64_t &a = mAnyone[stripped];
            a = std::max(a, ts_ms);
        }
    }

    void FloodGuard::seedForwarded(const std::string &sender, const std::string &text, bool is_text,
                                   int64_t ts_ms, int64_t now_ms) {
        Member &m = mMembers[sender];
        if (m.window_notice_until.size() != mConfig.windows.size())
            m.window_notice_until.assign(mConfig.windows.size(), 0);
        const int64_t dup_horizon = std::max(mConfig.dup_same_member_ms,
                                             std::max(mConfig.dup_anyone_ms, mConfig.similar_same_member_ms));
        const bool recent = now_ms - ts_ms < dup_horizon;
        std::string stripped = is_text && recent ? stripMarker(text) : std::string();
        record(m, stripped, recent && is_text && codePoints(stripped) >= mConfig.min_dup_chars, ts_ms);
    }

    void FloodGuard::seedMute(const std::string &sender, int64_t muted_at_ms, int64_t muted_until_ms) {
        Member &m = mMembers[sender];
        m.mutes.insert(std::upper_bound(m.mutes.begin(), m.mutes.end(), muted_at_ms), muted_at_ms);
        m.muted_until = std::max(m.muted_until, muted_until_ms);
    }

    void FloodGuard::clear(const std::string &sender) {
        auto it = mMembers.find(sender);
        if (it == mMembers.end()) return;
        Member &m = it->second;
        m.violations.clear();
        m.mutes.clear();
        m.muted_until = 0;
        m.muted_notice_until = 0;
        m.episode_last_drop = -1;
        m.dup_notice_until = 0;
        m.hold_until = 0;
        m.window_notice_until.assign(mConfig.windows.size(), 0);
    }

    FloodDecision FloodGuard::drop(Member &m, FloodDecision d, int64_t now_ms, bool never_block) {
        bool counted = false;
        if (mConfig.count_mode == FloodConfig::CountMode::PerDrop) {
            counted = true;
        } else if (m.episode_last_drop < 0 || now_ms - m.episode_last_drop > mConfig.episode_ms) {
            counted = true;                       // a new flood episode
            m.episode_last_count = now_ms;
        } else if (now_ms - m.episode_last_count >= mConfig.episode_ms) {
            counted = true;                       // the same episode, still going
            m.episode_last_count = now_ms;
        }
        m.episode_last_drop = now_ms;
        if (counted) m.violations.push_back(now_ms);
        while (!m.violations.empty() && m.violations.front() <= now_ms - mConfig.violation_window_ms)
            m.violations.pop_front();

        if (static_cast<int>(m.violations.size()) >= mConfig.violations_to_mute) {
            m.violations.clear();
            m.episode_last_drop = -1;
            m.muted_until = now_ms + mConfig.mute_ms;
            m.muted_notice_until = now_ms + mConfig.muted_notice_ms;
            m.mutes.push_back(now_ms);
            while (!m.mutes.empty() && m.mutes.front() <= now_ms - mConfig.mute_history_ms)
                m.mutes.pop_front();
            d.muted_now = true;
            d.muted_until = m.muted_until;
            d.reason += "; muted";
            d.notice = "You are muted in this group until " + utcTime(m.muted_until)
                       + ": " + std::to_string(mConfig.violations_to_mute)
                       + " flood warnings within an hour. Nothing you send is forwarded until then.";
            if (!never_block && static_cast<int>(m.mutes.size()) >= mConfig.mutes_to_block) {
                d.block_now = true;
                d.reason += "; blocked";
                d.notice = "You have been removed from this group: muted "
                           + std::to_string(m.mutes.size()) + " times within 7 days for flooding.";
            }
        }
        return d;
    }

    FloodDecision FloodGuard::check(const std::string &sender, const std::string &text, bool is_text,
                                    int64_t now_ms, bool never_block) {
        Member &m = mMembers[sender];
        prune(m, now_ms);
        pruneGlobal(now_ms);
        FloodDecision d;

        if (m.muted_until > now_ms) {
            d.verdict = FloodDecision::Verdict::DropMuted;
            d.reason = "muted until " + utcTime(m.muted_until);
            if (now_ms >= m.muted_notice_until) {
                m.muted_notice_until = now_ms + mConfig.muted_notice_ms;
                d.notice = "You are muted in this group until " + utcTime(m.muted_until)
                           + " for flooding. This message was not forwarded.";
            }
            return d;
        }

        if (m.hold_until > now_ms) {
            m.hold_until = now_ms + mConfig.trip_hold_ms;   // held until a quiet pause
            d.verdict = FloodDecision::Verdict::DropRate;
            d.reason = "held after a rate trip";
            return drop(m, d, now_ms, never_block);
        }

        // 1. Rate limits: the window that frees up last decides the notice.
        int worst = -1;
        int64_t worst_free_at = 0;
        for (size_t w = 0; w < mConfig.windows.size(); w++) {
            const auto &win = mConfig.windows[w];
            auto first = std::upper_bound(m.forwarded.begin(), m.forwarded.end(), now_ms - win.ms);
            int64_t count = m.forwarded.end() - first;
            if (count >= win.limit) {
                // The slot frees when the (count-limit+1)-th oldest in the window ages out.
                int64_t free_at = *(first + (count - win.limit)) + win.ms;
                if (worst < 0 || free_at > worst_free_at) {
                    worst = static_cast<int>(w);
                    worst_free_at = free_at;
                }
            }
        }
        if (worst >= 0) {
            const auto &win = mConfig.windows[worst];
            if (mConfig.trip_hold_ms > 0) m.hold_until = now_ms + mConfig.trip_hold_ms;
            d.verdict = FloodDecision::Verdict::DropRate;
            d.reason = "over " + std::to_string(win.limit) + " per " + win.name;
            if (now_ms >= m.window_notice_until[worst]) {
                m.window_notice_until[worst] = now_ms + win.ms;
                d.notice = "Not forwarded: more than " + std::to_string(win.limit)
                           + " messages in " + win.name + ". "
                           + (mConfig.trip_hold_ms > 0
                              ? "Nothing more from you is forwarded until you pause for "
                                + humanWait(mConfig.trip_hold_ms) + "."
                              : "You can send again in " + humanWait(worst_free_at - now_ms) + ".");
            }
            return drop(m, d, now_ms, never_block);
        }

        // 2. Duplicates.
        const std::string stripped = is_text ? stripMarker(text) : std::string();
        const bool dup_eligible = is_text && codePoints(stripped) >= mConfig.min_dup_chars;
        if (dup_eligible) {
            const char *why = nullptr;
            auto e = m.exact.find(stripped);
            auto a = mAnyone.find(stripped);
            if (e != m.exact.end() && now_ms - e->second < mConfig.dup_same_member_ms) {
                why = "same text from this member within 10 min";
            } else if (a != mAnyone.end() && now_ms - a->second < mConfig.dup_anyone_ms) {
                why = "same text from anyone within 1 min";
            } else {
                auto s = m.similar.find(similarKey(stripped));
                if (s != m.similar.end() && now_ms - s->second < mConfig.similar_same_member_ms) {
                    why = "similar text from this member within 5 min";
                }
            }
            if (why) {
                d.verdict = FloodDecision::Verdict::DropDuplicate;
                d.reason = why;
                if (now_ms >= m.dup_notice_until) {
                    m.dup_notice_until = now_ms + mConfig.dup_notice_ms;
                    d.notice = "Not forwarded: this repeats a recent message.";
                }
                return drop(m, d, now_ms, never_block);
            }
        }

        record(m, stripped, dup_eligible, now_ms);
        return d;
    }

}
