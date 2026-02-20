//
// Created by luocf on 2019/7/20.
//

#include "src/manager.h"
#include "../common/json.hpp"
extern "C" {
manager mManager;
using json = nlohmann::json;
bool ready = false;
void start(char *ip, int port, char *data_root_dir) {
    printf("start in, ready:%d", ready?1:0);
    if (ready == false) {
        mManager.start(ip, port, data_root_dir);
        ready = true;
    }
}

void createGroup() {
    printf("api createGroup in\n");
    mManager.createGroup();
    printf("api createGroup out\n");
}
void removeGroup(int service_id) {
    printf("api removeGroup in\n");
    mManager.removeGroup(service_id);
    printf("api removeGroup out\n");
}
int addAgent(int service_id, char* address) {
    std::string err;
    bool ok = mManager.addAgent(service_id, address == nullptr ? "" : std::string(address), err);
    if (!ok) {
        printf("api addAgent failed: %s\n", err.c_str());
        return -1;
    }
    return 0;
}
int removeAgent(int service_id, char* userid) {
    std::string err;
    bool ok = mManager.removeAgent(service_id, userid == nullptr ? "" : std::string(userid), err);
    if (!ok) {
        printf("api removeAgent failed: %s\n", err.c_str());
        return -1;
    }
    return 0;
}
void list(char* outdata) {
    printf("api list in\n");
    std::shared_ptr<std::vector<std::shared_ptr<GroupInfo>>> group_list = mManager.getGroupList();
    json result_json;
    for(int i=0; i<group_list->size(); i++) {
        std::shared_ptr<GroupInfo> groupinfo = group_list->at(i);
        groupinfo->Lock();
        int service_id = groupinfo->getServiceId();
        std::string address = groupinfo->getAddress();
        std::string data_dir = groupinfo->getDataDir();
        std::string nickname = groupinfo->getNickName();
        int member_count = groupinfo->getMemberCount();
        json item_json;
        item_json["id"] = std::to_string(service_id);
        item_json["address"] = address;
        item_json["members"] = std::to_string(member_count);
        item_json["nickname"] = nickname;
        result_json.push_back(item_json);
        groupinfo->UnLock();
    }
    printf("api list out\n");
    sprintf(outdata, "%s", result_json.dump().c_str());
}
}
