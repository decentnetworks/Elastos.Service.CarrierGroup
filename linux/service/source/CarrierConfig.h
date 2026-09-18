//
// Created by luocf on 2019/6/13.
//

#ifndef CHATROBOT_CARRIERCONFIG_H
#define CHATROBOT_CARRIERCONFIG_H

#include <string>
#include <vector>
#include "CarrierCompat.h"
#include "Node.h"
#include <memory> // std::unique_ptr

namespace chatrobot {
    class CarrierConfig {
    public:
        CarrierConfig();
        std::shared_ptr<std::string> NickName;
        std::vector<std::shared_ptr<Node>> mBootstrapNodes;
        /*std::vector<std::shared_ptr<Node>> mHiveNodes;*/
        ElaLogLevel mLogLevel;
        bool mEnableUdp;
    };
}

#endif //CHATROBOT_CARRIERCONFIG_H
