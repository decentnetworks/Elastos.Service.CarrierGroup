#include <Command/ChatRobotCmd.hpp>
#include <Tools/CommandText.h>

#include <iostream>
#include <iterator>
#include <sstream>

/* =========================================== */
/* === variables initialize =========== */
/* =========================================== */
const std::vector<ChatRobotCmd::CommandInfo> ChatRobotCmd::gCommandInfoList{
        {"h", "help",  ChatRobotCmd::Help,            "Print help usages."},
        {"a", "address",   ChatRobotCmd::Address,     "Show group address."},
        {"b", "block", ChatRobotCmd::BlockFriend,     "Block a friend by index."},
        {"d", "del",   ChatRobotCmd::DelFriend,       "Delete a friend  by index."},
        //{'i', "info",  ChatRobotCmd::PrintInfo, "Print friend's detail infommation"},
        {"l", "list",  ChatRobotCmd::ListFriends,     "List friends."},
        {"u", "update",  ChatRobotCmd::UpdateNickName,     "Update group name."},
        {"g", "agent",  ChatRobotCmd::Agent,      "Manage agent. /agent add <address> | /agent del <userid> | /agent list"},
        {"s", "signer",  ChatRobotCmd::Signer,    "Manage beagles.eth signers. /signer add <userid> | /signer del <userid> | /signer list"},
        {"n", "ens",  ChatRobotCmd::Ens,          "beagles.eth name. /ens register <name> | /ens show"},
        {"p", "profile",  ChatRobotCmd::Profile,  "Group profile. /profile avatar <https-url> | /profile intro <text> | /profile show"},
        {"ub", "unblock",  ChatRobotCmd::Unblock,  "Unblock a member. /unblock <userid>"},
        {"um", "unmute",  ChatRobotCmd::Unmute,    "End a flood mute. /unmute <userid>"},
        {"pending", "pending",  ChatRobotCmd::Pending,  "Join requests waiting. /pending [page]"},
        {"approve", "approve",  ChatRobotCmd::Approve,  "Let a join request in. /approve <n>"},
        {"reject", "reject",  ChatRobotCmd::Reject,     "Drop a join request. /reject <n>"},
        {"join", "join",  ChatRobotCmd::JoinMode,       "Who may join. /join open | /join approve | /join"},
        {"e", "exit",  ChatRobotCmd::DeleteGroup,     "Delete group and exit."},

};

inline std::string& ltrim(std::string &str) {
    std::string::iterator p = std::find_if(str.begin(), str.end(), std::not1(std::ptr_fun<int, int>(isspace)));
    str.erase(str.begin(), p);
    return str;
}

/* =========================================== */
/* === function implement ============= */
/* =========================================== */
int ChatRobotCmd::Do(void* context,
                     const std::string &cmd_msg,
                     std::string &errMsg) {
    std::string trim_msg = cmd_msg;
    trim_msg = ltrim(trim_msg);
    if (trim_msg.find('/') != 0) {
        errMsg = "not command";
        return kNotCommand;
    }

    const std::string &cmdLine = trim_msg.substr(1);
    auto wsfront = std::find_if_not(cmdLine.begin(), cmdLine.end(),
                                    [](int c) { return std::isspace(c); });
    auto wsback = std::find_if_not(cmdLine.rbegin(), cmdLine.rend(),
                                   [](int c) { return std::isspace(c); }).base();
    auto trimCmdLine = (wsback <= wsfront ? std::string() : std::string(wsfront, wsback));

    std::istringstream iss(trimCmdLine);
    std::vector<std::string> args{std::istream_iterator<std::string>{iss},
                                  std::istream_iterator<std::string>{}};
    if (args.size() <= 0) {
        return 0;
    }
    const auto &cmd = args[0];

    for (const auto &cmdInfo : gCommandInfoList) {
        if (cmd != cmdInfo.mCmd
            && cmd != cmdInfo.mLongCmd) {
            continue;
        }

        // args.back() is the sender's userid (appended by the caller).
        if (chatrobot::isCreatorOnlyCommand(cmd) && args.size() >= 2
            && !reinterpret_cast< chatrobot::CarrierRobot *>(context)->isGroupCreator(args.back())) {
            errMsg = "/" + cmdInfo.mLongCmd + " is for the group's creator. Type /help.";
            return kCreatorOnly;
        }
        int ret = cmdInfo.mFunc(context, args, errMsg);
        return ret;
    }

    errMsg = "Unknown command. Type /help.";
    return kUnknownCommand;
}

/* =========================================== */
/* === class public function implement  ====== */
/* =========================================== */

/* =========================================== */
/* === class protected function implement  === */
/* =========================================== */


/* =========================================== */
/* === class private function implement  ===== */
/* =========================================== */

int ChatRobotCmd::Help(void* context,
                       const std::vector<std::string> &args,
                       std::string &errMsg) {
    auto carrier_robot = reinterpret_cast< chatrobot::CarrierRobot *>(context);
    std::string msg = "";
    std::string friend_id = "";
    bool is_creator = false;
    if (args.size() >= 2) {
        friend_id = args.back();
        is_creator = carrier_robot->isGroupCreator(friend_id);
    }

    // One command per line, as a Markdown list so it shows as lines whether
    // the app renders plain text or Markdown (feedback#26).
    msg += "Commands:\n";
    msg += "- /help (/h): this list\n";
    msg += "- /address (/a): the group address\n";
    msg += "- /list [page] (/l): members, 20 per page\n";
    if (is_creator) {
        msg += "- /block <index> (/b): block a member\n";
        msg += "- /del <index> (/d): remove a member\n";
        msg += "- /join open | approve: who may join (/join shows it)\n";
        msg += "- /pending [page]: join requests waiting for you\n";
        msg += "- /approve <n> | /reject <n>: answer join request n\n";
        msg += "- /update <name> (/u): rename the group\n";
        msg += "- /agent add <address> | del <userid> | list (/g)\n";
        msg += "- /signer add <userid> | del <userid> | list (/s)\n";
        msg += "- /ens register <name> | show (/n)\n";
        msg += "- /profile avatar <https-url> | intro <text> | show (/p)\n";
        msg += "- /unblock <userid> (/ub)\n";
        msg += "- /unmute <userid> (/um)";
    } else {
        msg += "- /agent list (/g): agents in this group\n";
        msg += "- /signer list (/s): beagles.eth signers\n";
        msg += "- /ens show (/n): the group's beagles.eth name\n";
        msg += "- /profile show (/p): the group's profile\n";
        msg += "Creator commands are not shown.";
    }
    carrier_robot->helpCmd(args, msg);
    return 0;
}

int ChatRobotCmd::Address(void* context,
            const std::vector<std::string> &args,
            std::string &errMsg) {

    auto carrier_robot = reinterpret_cast< chatrobot::CarrierRobot *>(context);
    carrier_robot->showAddressCmd(args);
    return 0;
}


int ChatRobotCmd::BlockFriend(void* context,
                              const std::vector<std::string> &args,
                              std::string &errMsg) {
    auto carrier_robot = reinterpret_cast< chatrobot::CarrierRobot *>(context);
    carrier_robot->blockFriendCmd(args);
    return 0;
}

int ChatRobotCmd::DelFriend(void* context,
                            const std::vector<std::string> &args,
                            std::string &errMsg) {
    auto carrier_robot = reinterpret_cast< chatrobot::CarrierRobot *>(context);
    carrier_robot->delCmd(args);
    return 0;
}

int ChatRobotCmd::ListFriends(void* context,
                              const std::vector<std::string> &args,
                              std::string &errMsg) {
    auto carrier_robot = reinterpret_cast< chatrobot::CarrierRobot *>(context);
    carrier_robot->listCmd(args);
    return 0;
}

int ChatRobotCmd::UpdateNickName(void *context, const std::vector<std::string> &args,
                                 std::string &errMsg) {
    auto carrier_robot = reinterpret_cast< chatrobot::CarrierRobot *>(context);
    carrier_robot->updateNickNameCmd(args);
    return 0;
}
int ChatRobotCmd::DeleteGroup(void *context, const std::vector<std::string> &args,
                              std::string &errMsg) {
    auto carrier_robot = reinterpret_cast< chatrobot::CarrierRobot *>(context);
    carrier_robot->deleteGroupCmd(args);
    return 0;
}

int ChatRobotCmd::Agent(void *context, const std::vector<std::string> &args,
                        std::string &errMsg) {
    auto carrier_robot = reinterpret_cast< chatrobot::CarrierRobot *>(context);
    carrier_robot->agentCmd(args);
    return 0;
}

int ChatRobotCmd::Signer(void *context, const std::vector<std::string> &args,
                         std::string &errMsg) {
    auto carrier_robot = reinterpret_cast< chatrobot::CarrierRobot *>(context);
    carrier_robot->signerCmd(args);
    return 0;
}

int ChatRobotCmd::Ens(void *context, const std::vector<std::string> &args,
                      std::string &errMsg) {
    reinterpret_cast< chatrobot::CarrierRobot *>(context)->ensCmd(args);
    return 0;
}

int ChatRobotCmd::Profile(void *context, const std::vector<std::string> &args,
                          std::string &errMsg) {
    reinterpret_cast< chatrobot::CarrierRobot *>(context)->profileCmd(args);
    return 0;
}

int ChatRobotCmd::Unblock(void *context, const std::vector<std::string> &args,
                          std::string &errMsg) {
    reinterpret_cast< chatrobot::CarrierRobot *>(context)->unblockCmd(args);
    return 0;
}

int ChatRobotCmd::Unmute(void *context, const std::vector<std::string> &args,
                         std::string &errMsg) {
    reinterpret_cast< chatrobot::CarrierRobot *>(context)->unmuteCmd(args);
    return 0;
}

int ChatRobotCmd::Pending(void *context, const std::vector<std::string> &args,
                          std::string &errMsg) {
    reinterpret_cast< chatrobot::CarrierRobot *>(context)->pendingCmd(args);
    return 0;
}

int ChatRobotCmd::Approve(void *context, const std::vector<std::string> &args,
                          std::string &errMsg) {
    reinterpret_cast< chatrobot::CarrierRobot *>(context)->approveCmd(args);
    return 0;
}

int ChatRobotCmd::Reject(void *context, const std::vector<std::string> &args,
                         std::string &errMsg) {
    reinterpret_cast< chatrobot::CarrierRobot *>(context)->rejectCmd(args);
    return 0;
}

int ChatRobotCmd::JoinMode(void *context, const std::vector<std::string> &args,
                           std::string &errMsg) {
    reinterpret_cast< chatrobot::CarrierRobot *>(context)->joinModeCmd(args);
    return 0;
}

int ChatRobotCmd::PrintInfo(void* context,
                            const std::vector<std::string> &args,
                            std::string &errMsg) {
    auto carrier_robot = reinterpret_cast< chatrobot::CarrierRobot *>(context);

    return 0;
}
