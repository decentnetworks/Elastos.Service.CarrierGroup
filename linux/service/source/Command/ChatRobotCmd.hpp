#ifndef _CONTACT_TEST_CMD_HPP_
#define _CONTACT_TEST_CMD_HPP_

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <CarrierRobot.h>

class ChatRobotCmd {
public:
    /*** type define ***/

    /*** static function and variable ***/
    static int Do(void* context,
                  const std::string& cmdLine,
                  std::string& errMsg);

    /*** class function and variable ***/

protected:
    /*** type define ***/

    /*** static function and variable ***/

    /*** class function and variable ***/

private:
    /*** type define ***/
    struct CommandInfo {
        std::string mCmd;
        std::string mLongCmd;
        std::function<int(void* context, const std::vector<std::string>&, std::string&)> mFunc;
        std::string mUsage;
    };

    /*** static function and variable ***/
    static int Help(void* context,
                    const std::vector<std::string>& args,
                    std::string& errMsg);

    static int Address(void* context,
                       const std::vector<std::string>& args,
                       std::string& errMsg);

    static int BlockFriend(void* context,
                           const std::vector<std::string>& args,
                           std::string& errMsg);
    static int DelFriend(void* context,
                         const std::vector<std::string>& args,
                         std::string& errMsg);

    static int ListFriends(void* context,
                           const std::vector<std::string>& args,
                           std::string& errMsg);

    static int UpdateNickName(void* context,
                              const std::vector<std::string>& args,
                              std::string& errMsg);

    static int DeleteGroup(void* context,
                           const std::vector<std::string>& args,
                           std::string& errMsg);
    static int Agent(void* context,
                     const std::vector<std::string>& args,
                     std::string& errMsg);
    static int PrintInfo(void* context,
                         const std::vector<std::string>& args,
                         std::string& errMsg);

    static const std::vector<CommandInfo> gCommandInfoList;

    /*** class function and variable ***/
    explicit ChatRobotCmd() = delete;
    virtual ~ChatRobotCmd() = delete;
};

/***********************************************/
/***** class template function implement *******/
/***********************************************/

/***********************************************/
/***** macro definition ************************/
/***********************************************/

#endif /* _CONTACT_TEST_CMD_HPP_ */
