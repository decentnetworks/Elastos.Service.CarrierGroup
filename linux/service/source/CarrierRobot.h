//
// Created by luocf on 2019/6/13.
//

#ifndef CHATROBOT_CARRIERROBOT_H
#define CHATROBOT_CARRIERROBOT_H

#include <stdlib.h>
#include <functional>
#include <string>
#include <iostream>
#include <thread>
#include <future>
#include <queue>
#include <memory> // std::unique_ptr
#include <ctime>
#include <thread>
#include <regex>
#include <ela_session.h>
#include <ela_carrier.h>
#include "DataBase/DatabaseProxy.h"
#include <CarrierConfig.h>

namespace chatrobot {
    static const char *TAG = "CarrierRobot";
    class CarrierRobot:std::enable_shared_from_this<CarrierRobot> {
    public:
        class Factory {
        public:
            static int SetLocalDataDir(const std::string& dir);
            static std::shared_ptr<CarrierRobot> Create();

        private:
            static std::string sLocalDataDir;
            friend class CarrierRobot;
        };
        ~CarrierRobot();
        int start(const char* data_dir, int service_id, int my_socket_fd);
        void stop();
        int getAddress(std::string& address);
        int acceptFriend(std::shared_ptr<std::string>friendid);
        int getUserId(std::string& userid);
        static void OnCarrierConnection(ElaCarrier *carrier,
                                        ElaConnectionStatus status, void *context);
        static void OnCarrierFriendRequest(ElaCarrier *carrier, const char *friendid,
                                           const ElaUserInfo *info,
                                           const char *hello, void *context);
        static void OnCarrierFriendInfoChanged(ElaCarrier *carrier, const char *friendid,
                                               const ElaFriendInfo *info, void *context);
        static void OnCarrierFriendConnection(ElaCarrier *carrier,const char *friendid,
                                              ElaConnectionStatus status, void *context);

        static void OnCarrierFriendMessage(ElaCarrier *carrier, const char *from,
                                           const void *msg, size_t len, void *context);

        static int GetCarrierUsrIdByAddress(const std::string& address, std::string& usrId);
        void runCarrierInner();
        void runCarrier();
        bool inRemovedList(std::shared_ptr<std::string> friendid);
        void showAddressCmd(const std::vector<std::string> &args);
        void updateMemberInfo(std::shared_ptr<std::string> friendid, std::shared_ptr<std::string> nickname,
                              ElaConnectionStatus status);
        void addMessgae(std::shared_ptr<std::string> friend_id, std::shared_ptr<std::string> message, std::time_t send_time);
        std::shared_ptr<std::vector<std::shared_ptr<MemberInfo>>>getFriendList();
        void helpCmd(const std::vector<std::string> &args, const std::string& message);
        void listCmd(const std::vector<std::string> &args);
        void delCmd(const std::vector<std::string> &args);
        void blockFriendCmd(const std::vector<std::string> &args);
        void updateNickNameCmd(const std::vector<std::string> &args);
        void deleteGroupCmd(const std::vector<std::string> &args);
        std::shared_ptr<std::string> getGroupNickName();
        int getGroupStatus();
        void registerCarrierCallBack(const std::function<void(const std::string)> &carrierCallBack);
    private:
        std::shared_ptr<std::string> resolveGroupDisplayName(int service_id);
        std::shared_ptr<std::string> resolveMemberDisplayName(const char *friendid, const char *display_name);
        int applySelfDisplayName(int service_id);

        static CarrierRobot* instance;

        explicit CarrierRobot();
        std::time_t getTimeStamp();
        std::string convertDatetimeToString(std::time_t time);
        bool relayMessages();
        std::shared_ptr<std::regex> mMsgReg;
        std::mutex _mReplyMessage;
        std::thread mCarrieryThread;
        std::shared_ptr<std::string> mLocalPath;
        std::shared_ptr<std::string> mCreaterFriendId;
        std::unique_ptr<ElaCarrier, std::function<void(ElaCarrier*)>> mCarrier;
        std::shared_ptr<CarrierConfig> mCarrierConfig;
        std::shared_ptr<DatabaseProxy> mDatabaseProxy;
        int mStatus;
        int mMySocketFd;
        std::function <void(const std::string)> mCarrierCallBack;
        std::string mAddress;
        void sendMsgForManager(std::string msg);
    };
}

#endif //CHATROBOT_CARRIERROBOT_H
