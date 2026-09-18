//
// Created by luocf on 2019/6/15.
//

#ifndef CHATROBOT_GITHUB_MEMBERINFO_H
#define CHATROBOT_GITHUB_MEMBERINFO_H


#include <memory>
#include <mutex>
#include "CarrierCompat.h"
#include <ctime>
#include <memory> // std::unique_ptr


namespace chatrobot {

    class MemberInfo {
    public:
        static int sMemberCount;
        MemberInfo(std::shared_ptr<std::string>, std::shared_ptr<std::string>,  int status, std::time_t msg_timestamp);
        std::shared_ptr<std::string> mFriendid;
        std::shared_ptr<std::string> mNickName;
        int mStatus;
        std::time_t mMsgTimeStamp;
        int mIndex;
        bool mIsManager;
        void Lock();
        void UnLock();
    private:
        std::recursive_mutex mMutex;
    };
}


#endif //CHATROBOT_GITHUB_MEMBERINFO_H
