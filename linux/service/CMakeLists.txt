cmake_minimum_required(VERSION 3.5)
project(carrierService)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
set(SOURCE_FILES main.cpp carrierService.cpp carrierService.h
        ../common/Log.hpp
        ../common/Log.cpp
        ../common/json.hpp
        ../common/CommonVar.h
        source/Command/ChatRobotCmd.cpp
        source/Command/ChatRobotCmd.hpp
        source/DataBase/DatabaseProxy.h
        source/DataBase/DatabaseProxy.cpp
        source/DataBase/MemberInfo.h
        source/DataBase/MemberInfo.cpp
        source/DataBase/MessageInfo.h
        source/DataBase/MessageInfo.cpp
        source/ThirdParty/CompatibleFileSystem.hpp
        source/ThirdParty/ghc-filesystem.hpp
        source/CarrierRobot.h
        source/CarrierRobot.cpp
        source/CarrierConfig.h
        source/CarrierConfig.cpp
        source/ErrCode.cpp
        source/ErrCode.h
        source/Node.cpp
        source/Node.h)
set(CARRIER_LIB_INCLUDE lib/include/)
set(CARRIER_SDK_ROOT "$ENV{CARRIER_SDK_ROOT}" CACHE PATH "Path to Elastos.NET.Carrier.Native.SDK")
if(NOT CARRIER_SDK_ROOT)
    set(CARRIER_SDK_ROOT "${PROJECT_SOURCE_DIR}/../../Elastos.NET.Carrier.Native.SDK")
endif()

if(NOT EXISTS "${CARRIER_SDK_ROOT}/src/carrier/carrier.h")
    message(FATAL_ERROR "CARRIER_SDK_ROOT is invalid: ${CARRIER_SDK_ROOT}. Set -DCARRIER_SDK_ROOT=/path/to/Elastos.NET.Carrier.Native.SDK")
endif()

INCLUDE_DIRECTORIES(${CARRIER_LIB_INCLUDE})
INCLUDE_DIRECTORIES("${CARRIER_SDK_ROOT}/src/carrier")
INCLUDE_DIRECTORIES("${CARRIER_SDK_ROOT}/src/session")
INCLUDE_DIRECTORIES("${CARRIER_SDK_ROOT}/src/filetransfer")
INCLUDE_DIRECTORIES(source/)
INCLUDE_DIRECTORIES(../common/)

LINK_DIRECTORIES("source/")
add_executable(carrierService ${SOURCE_FILES})
#TARGET_LINK_LIBRARIES(carrierService pthread sqlite3 crystal elacarrier elafiletrans elasession)
TARGET_LINK_LIBRARIES(carrierService pthread sqlite3
        "${CARRIER_SDK_ROOT}/build/linux/intermediates/lib/libcrystal.so"
        "${CARRIER_SDK_ROOT}/build/linux/src/carrier/libcarrier.so"
        "${CARRIER_SDK_ROOT}/build/linux/src/filetransfer/libcarrierfiletrans.so"
        "${CARRIER_SDK_ROOT}/build/linux/src/session/libcarriersession.so")
