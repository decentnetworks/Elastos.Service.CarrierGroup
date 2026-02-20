//
// Created by luocf on 2019/7/19.
//

#ifndef CARRIERSERVICE_TESTSERVICE_H
#define CARRIERSERVICE_TESTSERVICE_H
#include <string>
#include <iostream>
#include "../common/Log.hpp"
#include "../common/json.hpp"
#include "./source/CarrierRobot.h"

class carrierService {
public:
    carrierService();
    ~carrierService();
    void start(std::string ip, int port, std::string data_root_dir, int service_id);
    void runCarrier();
    void runCommunicationThread();
    void runCommandThread(int sockfd);
    void sendMsgToWorkThread(const std::string msg);
private:
    std::shared_ptr<chatrobot::CarrierRobot> mCarrierRobot;
    std::thread mCommunicationThread;
    std::thread mCommandThread;
    std::string mRootDir;
    std::string mIp;
    int mPort;
    int mServiceId;
    std::queue<std::shared_ptr<std::string>> mTmpQueue;
    std::queue<std::shared_ptr<std::string>> mQueue;
    std::mutex mQueue_lock;
    std::condition_variable mQueue_cond;
    std::condition_variable mWrite_cond;
};


#endif //CARRIERSERVICE_TESTSERVICE_H
