//
// Created by luocf on 2019/6/15.
//

#ifndef CHATROBOT_GITHUB_MESSAGEINFO_H
#define CHATROBOT_GITHUB_MESSAGEINFO_H


#include <string>
#include <ctime>
#include "CarrierCompat.h"
#include <memory> // std::unique_ptr

class MessageInfo {

};
namespace chatrobot {
    class MessageInfo {
    public:
        MessageInfo();
        MessageInfo(std::shared_ptr<std::string> friend_id, std::shared_ptr<std::string> message, std::time_t send_time);
        std::shared_ptr<std::string> mFriendid;
        std::shared_ptr<std::string> mMsg;
        std::time_t mSendTimeStamp;
    };
}

#endif //CHATROBOT_GITHUB_MESSAGEINFO_H
