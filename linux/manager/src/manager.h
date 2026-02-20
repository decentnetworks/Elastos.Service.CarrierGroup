//
// Created by luocf on 2019/7/19.
//

#ifndef TESTMANAGER_MANAGER_H
#define TESTMANAGER_MANAGER_H

#include <string>
#include <iostream>
#include <thread>
#include <future>
#include <queue>
#include <map>
#include "GroupInfo.h"
#include "DatabaseProxy.h"
typedef std::lock_guard<std::mutex> MUTEX_LOCKER;
class manager {
public:
    manager();
    ~manager();
    void start(std::string ip, int port, std::string data_root_dir);
    void stop();
    int createGroup();
    void runCommunicationThread();
    void recvServiceMsgThread(int client_fd);
    void runWorkThread();
    void removeGroup(int service_id);
    std::shared_ptr<std::vector<std::shared_ptr<GroupInfo>>> getGroupList();
private:
    void sendMsgToWorkThread(std::string msg);
    int _createGroup();
    void _removeGroup(std::string address, int socket_fd);
    void _updateGroupNickName(std::string friendid, std::string nick_name);
    void _updateGroupAddress(int service_id, std::string address);
    void _updateGroupMemberCount(std::string friendid, int member_count);
    void _bindService(int service_id, const std::string data_dir);
    void _recoveryGroupInfo();
    void _registerServiceSocket(int service_id, int client_fd);
    bool _sendCommandToService(int service_id, const std::string& message);
    std::vector<std::shared_ptr<std::thread>> mThreadList;
    std::thread mCommunicationThread;
    std::thread mWorkThread;
    std::string getLocalCacheDir(std::string class_name);
    std::string mRootDir;
    std::string mIp;
    int mPort;
    int mServiceId;
    bool mIsReady;
    std::shared_ptr<DatabaseProxy> mDataBaseProxy;
    std::queue<std::shared_ptr<std::string>> mTmpQueue;
    std::queue<std::shared_ptr<std::string>> mQueue;
    std::mutex mQueue_lock;
    std::mutex mServiceSocketMapLock;
    std::map<int, int> mServiceSocketMap;
    std::condition_variable mQueue_cond;
    std::condition_variable mWrite_cond;

public:
    bool addAgent(int service_id, const std::string& address, std::string& err);
    bool removeAgent(int service_id, const std::string& user_id, std::string& err);
};


#endif //TESTMANAGER_MANAGER_H
