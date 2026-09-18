//
// Created by luocf on 2019/6/13.
//
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "CarrierCompat.h"
#include "CarrierConfig.h"

namespace chatrobot {

    CarrierConfig::CarrierConfig() {
        NickName = std::make_shared<std::string>("ChatRobot");
        mLogLevel = ElaLogLevel_Verbose;
        mEnableUdp = true;
        auto node1 = std::make_shared<chatrobot::Node>(
                std::make_shared<std::string>("13.58.208.50"),
                std::make_shared<std::string>("33445"),
                std::make_shared<std::string>(
                        "89vny8MrKdDKs7Uta9RdVmspPjnRMdwMmaiEW27pZ7gh"));
        mBootstrapNodes.push_back(node1);
        auto node2 = std::make_shared<chatrobot::Node>(
                std::make_shared<std::string>("18.216.6.197"),
                std::make_shared<std::string>("33445"),
                std::make_shared<std::string>(
                        "H8sqhRrQuJZ6iLtP2wanxt4LzdNrN2NNFnpPdq1uJ9n2"));
        mBootstrapNodes.push_back(node2);
        auto node3 = std::make_shared<chatrobot::Node>(
                std::make_shared<std::string>("52.83.127.85"),
                std::make_shared<std::string>("33445"),
                std::make_shared<std::string>(
                        "CDkze7mJpSuFAUq6byoLmteyGYMeJ6taXxWoVvDMexWC"));
        mBootstrapNodes.push_back(node3);
        auto node4 = std::make_shared<chatrobot::Node>(
                std::make_shared<std::string>("52.83.127.216"),
                std::make_shared<std::string>("33445"),
                std::make_shared<std::string>(
                        "4sL3ZEriqW7pdoqHSoYXfkc1NMNpiMz7irHMMrMjp9CM"));
        mBootstrapNodes.push_back(node4);
        auto node5 = std::make_shared<chatrobot::Node>(
                std::make_shared<std::string>("52.83.171.135"),
                std::make_shared<std::string>("33445"),
                std::make_shared<std::string>(
                        "5tuHgK1Q4CYf4K5PutsEPK5E3Z7cbtEBdx7LwmdzqXHL"));
        mBootstrapNodes.push_back(node5);
        auto node6 = std::make_shared<chatrobot::Node>(
                std::make_shared<std::string>("52.83.191.228"),
                std::make_shared<std::string>("33445"),
                std::make_shared<std::string>(
                        "3khtxZo89SBScAMaHhTvD68pPHiKxgZT6hTCSZZVgNEm"));
        mBootstrapNodes.push_back(node6);

    }
}