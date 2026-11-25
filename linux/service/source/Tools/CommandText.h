// Which group messages are commands (feedback#22). Pure functions, unit-tested
// in linux/tools/test_command_text.cpp.
#ifndef CHATROBOT_COMMAND_TEXT_H
#define CHATROBOT_COMMAND_TEXT_H

#include <string>

namespace chatrobot {

    // A message is a command when, after leading whitespace, it starts with "/"
    // followed by an ASCII letter: "/help", "/friends list". A command is never
    // stored or relayed, known or not. "/" alone, "//", "/ x", ":/", "/1" and
    // text with a slash later on are ordinary chat.
    inline bool isSlashCommand(const std::string &text) {
        size_t i = 0;
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n'
                                   || text[i] == '\r' || text[i] == '\f' || text[i] == '\v')) {
            i++;
        }
        if (i + 1 >= text.size() || text[i] != '/') return false;
        const char c = text[i + 1];
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    // Commands only the creator may run whose handlers stay silent for anyone
    // else; ChatRobotCmd::Do refuses them for non-creators with a reply.
    // (/agent, /signer, /ens, /profile, /unblock, /unmute reply on their own.)
    inline bool isCreatorOnlyCommand(const std::string &name) {
        return name == "b" || name == "block" || name == "d" || name == "del"
               || name == "u" || name == "update" || name == "e" || name == "exit"
               || name == "pending" || name == "approve" || name == "reject";
    }

}  // namespace chatrobot

#endif
