// FloodGuard — per-group anti-flood rules for the group service (feedback#21).
//
// Pure logic, no Carrier or SQLite: the caller feeds every member message
// (time in ms) and gets back whether to forward it and what to do about the
// sender. State the caller should persist (mutes) is exposed so a restart
// does not reset a mute; rate counters are seeded from message_table.
//
// Rules (defaults from feedback#21):
//  1. Rate: per member 2/s, 20/min, 120/h, 600/day, 3000/week, 10000/month
//     (month = 30 days). Only forwarded messages count. One notice per window.
//  2. Duplicates (texts of 10+ characters, trailing "[YYYY-MM-DD HH:MM:SS]"
//     marker stripped): same text from the same member within 10 min; same
//     text from anyone within 1 min; similar text (digits and whitespace
//     ignored) from the same member within 5 min.
//  3. 3 violations within 1 h -> muted 24 h. 3 mutes within 7 days -> blocked
//     (unless the caller says the sender may never be auto-blocked).
//
// A "violation" is one flood episode by default: it starts with a drop, ends
// after 10 min without drops, and counts again for every further 10 min it
// lasts. So a bot flushing a backlog for 3 minutes is one violation (dropped,
// not muted), while someone flooding nonstop is muted after ~20 minutes.
// CountMode::PerDrop counts every dropped message instead.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace chatrobot {

    struct FloodConfig {
        struct Window { int64_t ms; int limit; const char *name; };
        std::vector<Window> windows = {
                {1000LL, 2, "1 second"},
                {60LL * 1000, 20, "1 minute"},
                {3600LL * 1000, 120, "1 hour"},
                {86400LL * 1000, 600, "1 day"},
                {7LL * 86400 * 1000, 3000, "1 week"},
                {30LL * 86400 * 1000, 10000, "1 month"},
        };
        int64_t dup_same_member_ms = 10LL * 60 * 1000;
        int64_t dup_anyone_ms = 60LL * 1000;
        int64_t similar_same_member_ms = 5LL * 60 * 1000;
        size_t min_dup_chars = 10;            // code points, after stripping the marker
        int64_t dup_notice_ms = 10LL * 60 * 1000;
        enum class CountMode { Episode, PerDrop } count_mode = CountMode::Episode;
        int64_t episode_ms = 10LL * 60 * 1000;
        // >0: after a member trips a rate limit, nothing more of theirs is forwarded
        // until they have sent nothing for this long (every attempt while held extends
        // it). The group service uses 10 min: a burst "should not forward". 0 = off.
        int64_t trip_hold_ms = 0;
        int violations_to_mute = 3;
        int64_t violation_window_ms = 3600LL * 1000;
        int64_t mute_ms = 24LL * 3600 * 1000;
        int64_t muted_notice_ms = 3600LL * 1000;
        int mutes_to_block = 3;
        int64_t mute_history_ms = 7LL * 86400 * 1000;
    };

    struct FloodDecision {
        enum class Verdict { Forward, DropRate, DropDuplicate, DropMuted } verdict = Verdict::Forward;
        std::string reason;       // for the log
        std::string notice;       // non-empty: send this to the sender
        bool muted_now = false;   // caller persists the mute (muted_at, muted_until)
        int64_t muted_until = 0;
        bool block_now = false;   // caller blocks and removes the member
        bool forward() const { return verdict == Verdict::Forward; }
    };

    class FloodGuard {
    public:
        explicit FloodGuard(FloodConfig config = FloodConfig());

        // is_text=false for file messages: rate limits only, no duplicate check.
        // never_block: creator and signers are muted like anyone, never blocked.
        FloodDecision check(const std::string &sender, const std::string &text, bool is_text,
                            int64_t now_ms, bool never_block);

        // Startup replay: a message that was forwarded at ts_ms (now_ms = startup time;
        // only messages inside the duplicate windows are kept for the duplicate checks).
        void seedForwarded(const std::string &sender, const std::string &text, bool is_text,
                           int64_t ts_ms, int64_t now_ms);
        // Startup replay: a stored mute.
        void seedMute(const std::string &sender, int64_t muted_at_ms, int64_t muted_until_ms);
        // Creator /unmute or /unblock: forget the sender's mutes and violations.
        void clear(const std::string &sender);
        bool isMuted(const std::string &sender, int64_t now_ms) const;

        static std::string stripMarker(const std::string &text);
        static std::string similarKey(const std::string &stripped);
        static size_t codePoints(const std::string &s);

    private:
        struct Member {
            std::deque<int64_t> forwarded;                 // ascending
            std::vector<int64_t> window_notice_until;      // per window
            int64_t dup_notice_until = 0;
            std::deque<int64_t> violations;
            int64_t episode_last_drop = -1;                // -1: no episode
            int64_t episode_last_count = 0;
            int64_t hold_until = 0;
            std::deque<int64_t> mutes;                     // muted_at, ascending
            int64_t muted_until = 0;
            int64_t muted_notice_until = 0;
            std::unordered_map<std::string, int64_t> exact;    // stripped text -> last forwarded
            std::unordered_map<std::string, int64_t> similar;  // similar key -> last forwarded
        };

        FloodDecision drop(Member &m, FloodDecision d, int64_t now_ms, bool never_block);
        void record(Member &m, const std::string &stripped, bool dup_eligible, int64_t ts_ms);
        void prune(Member &m, int64_t now_ms);
        void pruneGlobal(int64_t now_ms);

        FloodConfig mConfig;
        std::map<std::string, Member> mMembers;
        std::unordered_map<std::string, int64_t> mAnyone;  // stripped text -> last forwarded, any sender
        int64_t mLastGlobalPrune = 0;
    };

}
