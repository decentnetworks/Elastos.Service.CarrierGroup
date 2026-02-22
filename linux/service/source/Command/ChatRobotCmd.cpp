#include <Command/ChatRobotCmd.hpp>

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
        return -10000;
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

        int ret = cmdInfo.mFunc(context, args, errMsg);
        return ret;
    }

    errMsg = "Unknown command: " + cmd;
    return -10000;
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

    msg += "h | help : Print help usages.\n";
    msg += "a | address : Show group address.\n";
    msg += "l | list : List friends.\n";
    if (is_creator) {
        msg += "b | block : Block a friend by index.\n";
        msg += "d | del : Delete a friend  by index.\n";
        msg += "u | update : Update group name.\n";
        msg += "g | agent : Manage agent. /agent add <address> | /agent del <userid> | /agent list\n";
    } else {
        msg += "g | agent : /agent list\n";
        msg += "Admin group commands are hidden in DM.\n";
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

int ChatRobotCmd::PrintInfo(void* context,
                            const std::vector<std::string> &args,
                            std::string &errMsg) {
    auto carrier_robot = reinterpret_cast< chatrobot::CarrierRobot *>(context);

    return 0;
}
