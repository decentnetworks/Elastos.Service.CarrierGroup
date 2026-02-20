//
// Created by luocf on 2019/7/19.
//

#include <arpa/inet.h>
#include<unistd.h>
#include<sys/types.h>
#include <thread>
#include <cstring>
#include <iostream>
#include "carrierService.h"
#include "../common/Log.hpp"
#include "../common/CommonVar.h"
using json = nlohmann::json;
constexpr int MAX_QUEUE_SIZE = 10;

carrierService::carrierService() {
    mCarrierRobot = chatrobot::CarrierRobot::Factory::Create();

}

carrierService::~carrierService() {}

void carrierService::sendMsgToWorkThread(const std::string msg) {
    printf("service sendMsgToWorkThread msg:%s\n", msg.c_str());
    std::unique_lock<std::mutex> lk(mQueue_lock);
    mWrite_cond.wait(lk, [this] { return mQueue.size() < MAX_QUEUE_SIZE; });
    mQueue.push(std::make_shared<std::string>(msg));
    lk.unlock();
    mQueue_cond.notify_one();
}

void carrierService::runCommunicationThread() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(-1);
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(mPort);//连接端口
    addr.sin_addr.s_addr = inet_addr(mIp.c_str());//都是服务器的，改成连接IP
    printf("ip:%s, port:%d, sockfd:%d\n", mIp.c_str(), mPort, sockfd);

    int res = connect(sockfd, (struct sockaddr *) &addr, sizeof(addr));

    if (res == -1) {
        perror("connect"), exit(-1);
    }
    char buf[512] = {};
    memset(buf, 0, sizeof(buf));
    printf("child 等待读取内容\n");
    res = read(sockfd, buf, sizeof(buf));
    printf("child 接受了%d字节，内容:%s\n", res, buf);
    if (res <= 0) {//包括0和-1
        perror("read"), exit(-1);
    }
    //收到manager消息即可，不用分析是否是Ready的消息。
    auto msg_json = json::parse(buf);

    printf("连接成功\n");
    mCarrierRobot->registerCarrierCallBack(std::bind(&carrierService::sendMsgToWorkThread, this, std::placeholders::_1));
    mCarrierRobot->start(mRootDir.c_str(), mServiceId, msg_json["client_fd"]);
    mCarrierRobot->runCarrier();
    mCommandThread = std::thread(&carrierService::runCommandThread, this, sockfd);

    while (true) {
        std::unique_lock <std::mutex> lk(mQueue_lock);
        mQueue_cond.wait(lk, [this] { return !mQueue.empty(); });
        if (mQueue.empty()) {
            break;
        }
        std::shared_ptr <std::string> result = mQueue.front();
        write(sockfd, result->c_str(), strlen(result->c_str()));
        mQueue.pop();
        lk.unlock();
        mWrite_cond.notify_one();
    }
    if (mCommandThread.joinable()) {
        mCommandThread.join();
    }
    close(sockfd);
}

void carrierService::runCommandThread(int sockfd) {
    char buf[512] = {};
    while (true) {
        memset(buf, 0, sizeof(buf));
        int res = read(sockfd, buf, sizeof(buf));
        if (res <= 0) {
            return;
        }

        try {
            auto cmd_json = json::parse(buf);
            int cmd = cmd_json["cmd"];
            if (cmd == Command_AgentAdd) {
                std::string address = cmd_json["address"];
                std::string err;
                if (!mCarrierRobot->addAgentByAddress(address, err)) {
                    Log::I(Log::TAG, "runCommandThread add agent failed: %s", err.c_str());
                }
            } else if (cmd == Command_AgentRemove) {
                std::string user_id = cmd_json["userid"];
                std::string err;
                if (!mCarrierRobot->removeAgentByUserId(user_id, err)) {
                    Log::I(Log::TAG, "runCommandThread remove agent failed: %s", err.c_str());
                }
            }
        } catch (std::exception &e) {
            Log::I(Log::TAG, "runCommandThread parse command failed: %s", e.what());
        }
    }
}

void carrierService::start(std::string ip, int port, std::string data_root_dir, int service_id) {
    mRootDir = data_root_dir;
    mIp = ip;
    mPort = port;
    mServiceId = service_id;
    //启动线程接收消息
    mCommunicationThread = std::thread(&carrierService::runCommunicationThread, this); //引用
}
