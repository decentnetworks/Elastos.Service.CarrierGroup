//
// Created by luocf on 2019/6/13.
//
#include <cstring>
#include <algorithm>
#include <cctype>
#include <future>
#include <stack>
#include <iostream>
#include <ctime>

using namespace std;

#include "CarrierCompat.h"
#include "CarrierCompat.h"
#include <carrier.h>
#include <carrier_session.h>
#include <carrier_session_deprecated.h>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <Log.hpp>
#include <json.hpp>
#include <CommonVar.h>
#include <ThirdParty/CompatibleFileSystem.hpp>
#include <Command/ChatRobotCmd.hpp>
#include <Tools/CommandText.h>
#include <Tools/MemberList.h>
#include <Tools/JoinRequests.h>
#include "CarrierRobot.h"
#include "ErrCode.h"

namespace chatrobot {
    using json = nlohmann::json;
    std::string CarrierRobot::Factory::sLocalDataDir;

    namespace {
        constexpr const char *kAgentOutboundPrefix = "CGP1 ";
        constexpr const char *kAgentReplyPrefix = "CGR1 ";
        constexpr const char *kAgentStatusPrefix = "CGS1 ";

        // File message (docs/GROUP_FILE_MESSAGE.md): one friend message
        //   [u32 BE metaLen][meta JSON {"type":"file",...}][raw bytes]
        // the same envelope clients send 1:1 to an offline friend. Binary: it
        // must never pass through C-string handling or the "nick: text" wrap.
        constexpr size_t kFileMetaMaxLen = 4096;
        // Whole-message cap for what the group relays as a file: the core will
        // not send more than this to a legacy-protocol member (Android, JS).
        constexpr size_t kFileMessageMaxLen = CARRIER_LEGACY_MAX_APP_BULKMSG_LEN;

        enum class FileMessageKind { NotFile, File, Invalid };

        // Standard base64 (RFC 4648, with padding) for the file bytes an agent
        // gets inside its CGP1 envelope (message.file.data_base64).
        std::string base64Encode(const char *data, size_t len) {
            static const char kAlphabet[] =
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            const unsigned char *b = reinterpret_cast<const unsigned char *>(data);
            std::string out;
            out.reserve(((len + 2) / 3) * 4);
            size_t i = 0;
            for (; i + 2 < len; i += 3) {
                const unsigned int v = (b[i] << 16) | (b[i + 1] << 8) | b[i + 2];
                out.push_back(kAlphabet[(v >> 18) & 0x3F]);
                out.push_back(kAlphabet[(v >> 12) & 0x3F]);
                out.push_back(kAlphabet[(v >> 6) & 0x3F]);
                out.push_back(kAlphabet[v & 0x3F]);
            }
            if (i < len) {
                unsigned int v = b[i] << 16;
                if (i + 1 < len) {
                    v |= b[i + 1] << 8;
                }
                out.push_back(kAlphabet[(v >> 18) & 0x3F]);
                out.push_back(kAlphabet[(v >> 12) & 0x3F]);
                out.push_back(i + 1 < len ? kAlphabet[(v >> 6) & 0x3F] : '=');
                out.push_back('=');
            }
            return out;
        }

        // Reader rules of the client spec (iOS ChatOfflineFileWire.unpack):
        // metaLen 1..4096, meta is a JSON object with type "file", and a
        // declared size must fit. Invalid = it claims to be a file message
        // (two leading zero bytes, sane length) but is truncated/oversized.
        FileMessageKind parseFileMessage(const std::string &raw, json *meta_out,
                                         size_t *meta_len_out) {
            if (raw.size() < 5
                || raw[0] != '\0' || raw[1] != '\0') {
                return FileMessageKind::NotFile;
            }
            const unsigned char *b = reinterpret_cast<const unsigned char *>(raw.data());
            const size_t meta_len = (static_cast<size_t>(b[0]) << 24) | (static_cast<size_t>(b[1]) << 16)
                                    | (static_cast<size_t>(b[2]) << 8) | static_cast<size_t>(b[3]);
            if (meta_len < 1 || meta_len > kFileMetaMaxLen || 4 + meta_len > raw.size()) {
                return FileMessageKind::NotFile;
            }
            json meta;
            try {
                meta = json::parse(raw.begin() + 4, raw.begin() + 4 + meta_len);
            } catch (...) {
                return FileMessageKind::NotFile;
            }
            if (!meta.is_object()) {
                return FileMessageKind::NotFile;
            }
            auto type_it = meta.find("type");
            if (type_it == meta.end() || !type_it->is_string() || type_it->get<std::string>() != "file") {
                return FileMessageKind::NotFile;
            }
            auto size_it = meta.find("size");
            if (size_it != meta.end() && size_it->is_number_unsigned()
                && 4 + meta_len + size_it->get<uint64_t>() > raw.size()) {
                return FileMessageKind::Invalid;
            }
            if (raw.size() > kFileMessageMaxLen) {
                return FileMessageKind::Invalid;
            }
            if (meta_out != nullptr) {
                *meta_out = std::move(meta);
            }
            if (meta_len_out != nullptr) {
                *meta_len_out = meta_len;
            }
            return FileMessageKind::File;
        }

        // meta[key] if it is a string, else fallback. json::value() throws on a
        // non-string value, and the relay runs inside a Carrier callback.
        std::string metaString(const json &meta, const char *key, const std::string &fallback) {
            auto it = meta.find(key);
            return it != meta.end() && it->is_string() ? it->get<std::string>() : fallback;
        }

        // A DNFT1 streamed-transfer frame (0x1E "DNFT1"). It is a 1:1
        // handshake that needs the receiver to answer; relayed into a group it
        // only ever became junk text for every member (2026-10-09).
        bool isDnft1Frame(const std::string &raw) {
            return raw.size() >= 6 && raw[0] == '\x1e' && raw.compare(1, 5, "DNFT1") == 0;
        }

        // [u32 BE len(meta_str)][meta_str][the file bytes of raw, after its own meta]
        std::string packFileMessage(const std::string &meta_str, const std::string &raw,
                                    size_t old_meta_len) {
            const size_t body_off = 4 + old_meta_len;
            std::string out;
            out.reserve(4 + meta_str.size() + raw.size() - body_off);
            const uint32_t n = static_cast<uint32_t>(meta_str.size());
            out.push_back(static_cast<char>((n >> 24) & 0xff));
            out.push_back(static_cast<char>((n >> 16) & 0xff));
            out.push_back(static_cast<char>((n >> 8) & 0xff));
            out.push_back(static_cast<char>(n & 0xff));
            out.append(meta_str);
            out.append(raw, body_off, std::string::npos);
            return out;
        }

        // Carrier profile and message data is supplied by remote peers.  Do not
        // let a malformed byte sequence from one peer terminate the whole group
        // service while serializing a relay envelope.
        std::string dumpJsonForTransport(const json &value) {
            return value.dump(-1, ' ', false, json::error_handler_t::replace);
        }

        std::string trimDisplayName(const std::string &value) {
            size_t begin = 0;
            while (begin < value.size()
                   && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
                ++begin;
            }
            size_t end = value.size();
            while (end > begin
                   && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
                --end;
            }
            return value.substr(begin, end - begin);
        }

        std::string buildFallbackUserName(const char *friendid) {
            if (friendid == nullptr || friendid[0] == '\0') {
                return "CarrierUser";
            }

            std::string friend_id(friendid);
            return std::string("CarrierUser-")
                   + friend_id.substr(0, std::min<size_t>(8, friend_id.size()));
        }

        bool startsWith(const std::string &value, const char *prefix) {
            const size_t prefix_len = strlen(prefix);
            return value.size() >= prefix_len
                   && value.compare(0, prefix_len, prefix) == 0;
        }

        std::string normalizeProtocolToken(const std::string &value) {
            std::string normalized;
            normalized.reserve(value.size());
            for (char ch : value) {
                unsigned char uch = static_cast<unsigned char>(ch);
                if (std::isalnum(uch) == 0) {
                    continue;
                }
                normalized.push_back(static_cast<char>(std::tolower(uch)));
            }
            return normalized;
        }

        std::string sanitizeJsonPayload(const std::string &value) {
            std::string sanitized;
            sanitized.reserve(value.size());
            for (char ch : value) {
                unsigned char uch = static_cast<unsigned char>(ch);
                if (uch < 0x20 && ch != '\t' && ch != '\n' && ch != '\r') {
                    continue;
                }
                sanitized.push_back(ch);
            }
            return sanitized;
        }

        const char *connectionStatusToString(ElaConnectionStatus status) {
            switch (status) {
                case ElaConnectionStatus_Connected:
                    return "connected";
                case ElaConnectionStatus_Disconnected:
                    return "disconnected";
                default:
                    return "unknown";
            }
        }

        const char *presenceStatusToString(ElaPresenceStatus presence) {
            switch (presence) {
                case ElaPresenceStatus_None:
                    return "none";
                case ElaPresenceStatus_Away:
                    return "away";
                case ElaPresenceStatus_Busy:
                    return "busy";
                default:
                    return "unknown";
            }
        }

        json buildUserInfoJson(const ElaUserInfo &user_info) {
            json user;
            user["userid"] = user_info.userid;
            user["name"] = user_info.name;
            user["description"] = user_info.description;
            user["has_avatar"] = user_info.has_avatar;
            user["gender"] = user_info.gender;
            user["phone"] = user_info.phone;
            user["email"] = user_info.email;
            user["region"] = user_info.region;
            return user;
        }

        // AgentNet proto v2: what the sender's client advertised and the profile
        // it published. Members only see each other through this envelope, so
        // without it a group is a profile black hole. Empty for legacy peers.
        json buildAgentNetJson(ElaCarrier *ela, const char *userid) {
            Carrier *carrier = reinterpret_cast<Carrier *>(ela);
            json agentnet;
            CarrierClientInfo client;
            CarrierProfileExt ext;
            if (carrier && userid && carrier_get_friend_client_info(carrier, userid, &client) == 0) {
                agentnet["proto_version"] = client.proto_version;
                agentnet["platform"] = client.platform;
                agentnet["os_version"] = client.os_version;
                agentnet["app_version"] = client.app_version;
            }
            if (carrier && userid && carrier_get_friend_profile_ext(carrier, userid, &ext) == 0) {
                agentnet["avatar_url"] = ext.avatar_url;
                agentnet["url"] = ext.url;
                agentnet["ens"] = ext.ens;
                agentnet["extra"] = ext.extra;
            }
            return agentnet;
        }

        json buildFriendInfoJson(const ElaFriendInfo &friend_info) {
            json info;
            info["label"] = friend_info.label;
            info["status"] = static_cast<int>(friend_info.status);
            info["status_text"] = connectionStatusToString(friend_info.status);
            info["presence"] = static_cast<int>(friend_info.presence);
            info["presence_text"] = presenceStatusToString(friend_info.presence);
            return info;
        }
    }

    /***********************************************/
    /***** static function implement ***************/
    /***********************************************/
    int CarrierRobot::Factory::SetLocalDataDir(const std::string &dir) {
        if (dir.empty()) {
            return ErrCode::InvalidArgument;
        }

        std::error_code stdErrCode;
        bool ret;
        ret = std::filesystem::create_directories(dir, stdErrCode);
        if (ret == false
            || stdErrCode.value() != 0) {
            int errCode = ErrCode::StdSystemErrorIndex - stdErrCode.value();
            auto errMsg = ErrCode::ToString(errCode);
            Log::D(Log::TAG, "Failed to set local data dir, errcode: %s", errMsg.c_str());
            return errCode;
        }

        sLocalDataDir = dir;

        return 0;
    }

    std::shared_ptr<CarrierRobot> CarrierRobot::Factory::Create() {
        struct Impl : CarrierRobot {
        };

        return std::make_shared<Impl>();
    }

    CarrierRobot::~CarrierRobot() {
        mCarrierConfig.reset();
        this->stop();
    }

    CarrierRobot::CarrierRobot() {
        mCarrierConfig = std::make_shared<CarrierConfig>();
        mDatabaseProxy = std::make_shared<DatabaseProxy>();
        //anypeer Id format
        std::string reg_str("(\\w{8})-(\\w{4})-(\\w{4})-(\\w{4})-(\\w{13})");
        mMsgReg = std::make_shared<std::regex>(reg_str, std::regex::icase);
        mStatus = 0;
    }

    int CarrierRobot::GetCarrierUsrIdByAddress(const std::string &address, std::string &usrId) {
        char buf[ELA_MAX_ID_LEN + 1] = {0};
        auto ret = ela_get_id_by_address(address.c_str(), buf, sizeof(buf));
        if (ret == nullptr) {
            int err = ela_get_error();
            char strerr_buf[512] = {0};
            ela_get_strerror(err, strerr_buf, sizeof(strerr_buf));
            Log::E(Log::TAG, "Failed to add friend! ret=%s(0x%x)", strerr_buf, ret);
            return ErrCode::FailedCarrier;
        }

        usrId = ret;
        return 0;
    }

    void CarrierRobot::runCarrierInner() {
        int ret = ela_run(mCarrier.get(), 500);
        if (ret < 0) {
            ela_kill(mCarrier.get());
            Log::E(Log::TAG, "Failed to run carrier!");
        }

    }

    void CarrierRobot::runCarrier() {
        mCarrieryThread = std::thread(&CarrierRobot::runCarrierInner, this); //引用
    }

    std::shared_ptr<std::string> CarrierRobot::resolveGroupDisplayName(int service_id) {
        std::string display_name;
        auto group_nickname = mDatabaseProxy->getGroupNickName();
        if (group_nickname.get() != nullptr) {
            display_name = trimDisplayName(*group_nickname.get());
        }

        if (display_name.empty()) {
            std::string base_name = "CarrierGroup";
            if (mCarrierConfig.get() != nullptr
                && mCarrierConfig->NickName.get() != nullptr) {
                std::string configured_name = trimDisplayName(*mCarrierConfig->NickName.get());
                if (!configured_name.empty()) {
                    base_name = configured_name;
                }
            }
            display_name = base_name + "-" + std::to_string(service_id);
        }

        if (display_name.size() > ELA_MAX_USER_NAME_LEN) {
            display_name.resize(ELA_MAX_USER_NAME_LEN);
        }
        return std::make_shared<std::string>(display_name);
    }

    std::shared_ptr<std::string> CarrierRobot::resolveMemberDisplayName(const char *friendid,
                                                                         const char *display_name) {
        std::string normalized_name;
        if (display_name != nullptr) {
            normalized_name = trimDisplayName(std::string(display_name));
        }
        if (normalized_name.empty()) {
            normalized_name = buildFallbackUserName(friendid);
        }
        if (normalized_name.size() > ELA_MAX_USER_NAME_LEN) {
            normalized_name.resize(ELA_MAX_USER_NAME_LEN);
        }
        return std::make_shared<std::string>(normalized_name);
    }

    int CarrierRobot::applySelfDisplayName(int service_id) {
        std::shared_ptr<std::string> display_name = resolveGroupDisplayName(service_id);
        if (display_name.get() == nullptr || display_name->empty()) {
            return 0;
        }

        ElaUserInfo self_info;
        memset(&self_info, 0, sizeof(self_info));
        int ret = ela_get_self_info(mCarrier.get(), &self_info);
        if (ret != 0) {
            Log::I(TAG, "applySelfDisplayName failed to get self info, errno:(0x%x)", ela_get_error());
            return ret;
        }

        if (std::string(self_info.name) == *display_name.get()) {
            return 0;
        }

        strncpy(self_info.name, display_name->c_str(), ELA_MAX_USER_NAME_LEN);
        self_info.name[ELA_MAX_USER_NAME_LEN] = '\0';
        ret = ela_set_self_info(mCarrier.get(), &self_info);
        if (ret != 0) {
            Log::I(TAG, "applySelfDisplayName failed to set self info, errno:(0x%x)", ela_get_error());
            return ret;
        }

        mDatabaseProxy->updateGroupNickName(display_name);
        return 0;
    }

    void CarrierRobot::OnCarrierFriendInfoChanged(ElaCarrier *carrier, const char *friendid,
                                                  const ElaFriendInfo *info, void *context) {
        auto carrier_robot = reinterpret_cast<CarrierRobot *>(context);
        const char *raw_name = info != nullptr ? info->user_info.name : nullptr;
        std::shared_ptr<std::string> nickanme = carrier_robot->resolveMemberDisplayName(friendid,
                                                                                         raw_name);
        carrier_robot->updateMemberInfo(std::make_shared<std::string>(friendid), nickanme,
                                        ElaConnectionStatus_Connected);
    }

    void CarrierRobot::OnCarrierConnection(ElaCarrier *carrier,
                                           ElaConnectionStatus status, void *context) {
        auto channel = reinterpret_cast<CarrierRobot *>(context);
        Log::I(Log::TAG, "OnCarrierConnection status: %d", status);
        std::string carrierAddr, carrierUsrId;
        channel->getAddress(carrierAddr);
        GetCarrierUsrIdByAddress(carrierAddr, carrierUsrId);
    }

    void CarrierRobot::OnCarrierFriendRequest(ElaCarrier *carrier, const char *friendid,
                                              const ElaUserInfo *info,
                                              const char *hello, void *context) {
        Log::I(Log::TAG, "OnCarrierFriendRequest from: %s", friendid);
        auto carrierRobot = reinterpret_cast<CarrierRobot *>(context);
        carrierRobot->onJoinRequest(friendid, info ? info->name : "", hello ? hello : "");
    }

    std::time_t CarrierRobot::getTimeStamp() {
        auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        return static_cast<std::time_t>(now_us);
    }

    void CarrierRobot::OnCarrierFriendConnection(ElaCarrier *carrier, const char *friendid,
                                                 ElaConnectionStatus status, void *context) {
        Log::I(Log::TAG, "OnCarrierFriendConnection from: %s %d", friendid, status);
        auto carrier_robot = reinterpret_cast<CarrierRobot *>(context);
        if (!carrier_robot->inRemovedList(std::make_shared<std::string>(friendid))) {
            ElaFriendInfo info;
            int ret = ela_get_friend_info(carrier, friendid, &info);
            const char *raw_name = ret == 0 ? info.user_info.name : nullptr;
            std::shared_ptr<std::string> nickanme = carrier_robot->resolveMemberDisplayName(friendid,
                                                                                             raw_name);
            carrier_robot->updateMemberInfo(std::make_shared<std::string>(friendid), nickanme, status);
            if (status == ElaConnectionStatus_Connected && carrier_robot->isGroupCreator(friendid)) {
                carrier_robot->tellCreatorPendingJoins();
            }

        }
    }

    bool CarrierRobot::inRemovedList(std::shared_ptr<std::string> friendid) {
        return mDatabaseProxy->inRemovedList(*friendid.get());
    }

    void CarrierRobot::updateMemberInfo(std::shared_ptr<std::string> friendid,
                                        std::shared_ptr<std::string> nickname,
                                        ElaConnectionStatus status) {
        if (mCreaterFriendId.get() == nullptr) {
            mCreaterFriendId = friendid;
        }

        std::shared_ptr<MemberInfo> block_member_info = mDatabaseProxy->getBlockMemberInfo(friendid);
        if (block_member_info.get() == nullptr) {
            mDatabaseProxy->updateMemberInfo(friendid, nickname, status == ElaConnectionStatus_Connected ? 0 : 1, 0);
            //当前状态为上线时，获取该成员offline以后的所以消息，并发送给该人,
            if (status == ElaConnectionStatus_Connected) {
                relayMessages();
            }
        }
        json msg;
        msg["cmd"] = Command_UpdateMemberCount;
        msg["friendid"] = mAddress.c_str();
        msg["membercount"] = getFriendList()->size();
        sendMsgForManager(dumpJsonForTransport(msg));
    }

    bool CarrierRobot::relayMessages() {
        MUTEX_LOCKER locker_sync_data(_mReplyMessage);
        std::shared_ptr<std::vector<std::shared_ptr<MemberInfo>>> memberlist = mDatabaseProxy->getFriendList();
        for (int i = 0; i < memberlist->size(); i++) {
            std::shared_ptr<MemberInfo> memberInfo = memberlist->at(i);
            if (memberInfo.get() == nullptr) {
                continue;
            }
            bool info_changed = false;
            memberInfo->Lock();
            if (memberInfo->mStatus != 0) {
                memberInfo->UnLock();
                continue;
            }
            std::shared_ptr<std::vector<std::shared_ptr<MessageInfo>>> message_list = mDatabaseProxy->getMessages(
                    memberInfo->mFriendid, memberInfo->mMsgTimeStamp, 100);
            if (message_list.get() != nullptr) {
                for (int i = 0; i < message_list->size(); i++) {
                    std::shared_ptr<MessageInfo> message = message_list->at(i);
                    if (message.get() != nullptr) {
                        if (memberInfo->mFriendid.get() != nullptr
                            && message->mFriendid.get() != nullptr
                            && mDatabaseProxy->hasAgent(*memberInfo->mFriendid.get())
                            && memberInfo->mFriendid->compare(*message->mFriendid.get()) == 0) {
                            // Skip echoing an agent's own message back to itself.
                            info_changed = true;
                            memberInfo->mMsgTimeStamp = message->mSendTimeStamp;
                            continue;
                        }
                        std::string msg = buildOutboundMessageForRecipient(
                                *memberInfo->mFriendid.get(), message);
                        if (msg.empty()) {
                            info_changed = true;
                            memberInfo->mMsgTimeStamp = message->mSendTimeStamp;
                            continue;
                        }
                        int msg_ret = ela_send_friend_message_noreceipt(mCarrier.get(),
                                                              memberInfo->mFriendid.get()->c_str(),
                                                              msg.data(), msg.size());
                        if (msg_ret != 0) {
                            const int err = ela_get_error();
                            Log::I(TAG,
                                   "relayMessages send failed to=%s from=%s ts=%lld len=%zu err=0x%x",
                                   memberInfo->mFriendid.get() != nullptr
                                   ? memberInfo->mFriendid->c_str() : "",
                                   message->mFriendid.get() != nullptr
                                   ? message->mFriendid->c_str() : "",
                                   static_cast<long long>(message->mSendTimeStamp),
                                   msg.size(), err);
                            if (err == CARRIER_GENERAL_ERROR(ERROR_INVALID_ARGS)) {
                                // Permanent for this member (e.g. over a legacy
                                // peer's 5 MB cap): skip it, or the member is
                                // stuck behind this message forever.
                                info_changed = true;
                                memberInfo->mMsgTimeStamp = message->mSendTimeStamp;
                                continue;
                            }
                            break;
                        }
                        info_changed = true;
                        memberInfo->mMsgTimeStamp = message->mSendTimeStamp;
                    }
                }
            }
            memberInfo->UnLock();
            if (info_changed) {
                mDatabaseProxy->updateMemberInfo(memberInfo->mFriendid, memberInfo->mNickName, memberInfo->mStatus, memberInfo->mMsgTimeStamp);
            }

        }
        return false;
    }

    namespace {
        // The core's timestamp for an Express message, as microseconds. The
        // node stamps it in us; accept s/ms too rather than trust one unit.
        int64_t normalizeToMicros(int64_t ts) {
            if (ts <= 0) return 0;
            if (ts < 100000000000LL) return ts * 1000000LL;      // seconds
            if (ts < 100000000000000LL) return ts * 1000LL;      // milliseconds
            return ts;                                           // microseconds
        }
    }

    bool CarrierRobot::acceptOfflineCopy(const std::string &friend_id, const std::string &stored_form,
                                         std::time_t recv_time, int64_t post_time) {
        constexpr int64_t kMinute = 60LL * 1000000LL;
        int64_t posted = normalizeToMicros(post_time);
        if (posted > static_cast<int64_t>(recv_time) + 60 * kMinute) {
            posted = 0;  // nonsense from the node: judge by arrival only
        }
        // Parked before this group pulled Express at all (an hour of slack
        // for node clocks): delivered weeks late into a busy group it would
        // only confuse. Kept in express_backlog_table, not relayed.
        if (posted > 0 && mExpressSince > 0 && posted < static_cast<int64_t>(mExpressSince) - 60 * kMinute) {
            // The nodes hand the same message out again on every pull (11-03:
            // the core's watermark never passes them), so keep it once.
            if (mDatabaseProxy->addExpressBacklog(friend_id, stored_form, static_cast<std::time_t>(posted), recv_time)) {
                Log::I(TAG, "addMessgae express backlog kept, not relayed: from=%s len=%zu posted_us=%lld",
                       friend_id.c_str(), stored_form.size(), static_cast<long long>(posted));
            }
            return false;
        }
        // The core re-sends a live message through Express when its receipt
        // does not come back in 30 s, so a message can arrive twice, and the
        // nodes re-deliver the same parked message on every pull. Same
        // sender, same bytes, stored since 10 min before it was posted = the
        // copy we already have. The post time is fixed per message, so a
        // re-delivery weeks later still matches. Without a post time: a day.
        int64_t since = posted > 0
                        ? std::min<int64_t>(posted, recv_time) - 10 * kMinute
                        : static_cast<int64_t>(recv_time) - 24 * 60 * kMinute;
        if (mDatabaseProxy->hasMessageSince(friend_id, stored_form, static_cast<std::time_t>(since))) {
            Log::I(TAG, "addMessgae express duplicate dropped: from=%s len=%zu", friend_id.c_str(),
                   stored_form.size());
            return false;
        }
        return true;
    }

    void CarrierRobot::addMessgae(std::shared_ptr<std::string> friend_id,
                                  std::shared_ptr<std::string> message, std::time_t send_time,
                                  bool offline, int64_t post_time) {
        std::string errMsg;
        if (isDnft1Frame(*message.get())) {
            Log::I(TAG, "addMessgae drop DNFT1 frame from=%s len=%zu", friend_id->c_str(),
                   message->size());
            return;
        }
        switch (parseFileMessage(*message.get(), nullptr, nullptr)) {
            case FileMessageKind::File:
                // Stored and relayed byte for byte; never a command or agent reply.
                Log::I(TAG, "addMessgae file message from=%s len=%zu offline=%d", friend_id->c_str(),
                       message->size(), offline ? 1 : 0);
                if (offline && !acceptOfflineCopy(*friend_id, *message.get(), send_time, post_time)) {
                    return;
                }
                if (!floodAllows(*friend_id, *message.get(), false)) {
                    return;
                }
                mDatabaseProxy->addMessgae(friend_id, *message.get(), send_time,
                                           originalSendTime(offline, send_time, post_time));
                relayMessages();
                return;
            case FileMessageKind::Invalid:
                Log::I(TAG, "addMessgae drop truncated/oversized file message from=%s len=%zu",
                       friend_id->c_str(), message->size());
                return;
            case FileMessageKind::NotFile:
                break;
        }
        std::string msg = normalizeIncomingMessageForStorage(*friend_id.get(), *message.get());
        // An agent's CGR1 envelope that was unwrapped keeps today's handling below.
        const bool agent_envelope = (msg != *message.get());
        std::string status_message = normalizeAgentStatusEnvelope(*friend_id.get(), *message.get());
        if (!status_message.empty()) {
            relayAgentStatusToMembers(*friend_id.get(), status_message);
            return;
        }
        if (msg.size() > 38 &&
            std::regex_match(msg.substr(0, 37).c_str(), *mMsgReg.get()) == true) {
            msg = msg.substr(38);//包含空格
        }
        // Checked before commands too: a /command parked weeks ago must not run now.
        if (offline && !acceptOfflineCopy(*friend_id, msg, send_time, post_time)) {
            return;
        }
        // feedback#22: "/" + letter is a command. It is never stored or relayed; an unknown
        // or refused one gets a private reply. Commands (and so their replies) count under
        // the anti-flood rate limits, except the creator's, so /unmute always works.
        if (!agent_envelope && isSlashCommand(msg)) {
            if (!isGroupCreator(*friend_id) && !floodAllows(*friend_id, msg, false)) {
                return;
            }
            std::string pre_cmd = msg + " " + *friend_id.get();
            int ret = ChatRobotCmd::Do(this, pre_cmd, errMsg);
            if (ret == ChatRobotCmd::kUnknownCommand || ret == ChatRobotCmd::kCreatorOnly) {
                Log::I(TAG, "command refused from=%s len=%zu: %s", friend_id->c_str(), msg.size(),
                       errMsg.c_str());
                sendCommandResponse(*friend_id, errMsg);
            }
            return;
        }
        int ret = ChatRobotCmd::kNotCommand;
        if (agent_envelope) {
            std::string pre_cmd = msg + " " + *friend_id.get();//Pretreatment cmd
            ret = ChatRobotCmd::Do(this, pre_cmd, errMsg);
        }
        if (ret == ChatRobotCmd::kNotCommand || ret == ChatRobotCmd::kUnknownCommand) {
            if (!floodAllows(*friend_id, msg, true)) {
                return;
            }
            //save message
            mDatabaseProxy->addMessgae(friend_id, msg, send_time,
                                       originalSendTime(offline, send_time, post_time));
            //将该消息转发给其他人
            relayMessages();
        }
    }

    std::shared_ptr<std::vector<std::shared_ptr<MemberInfo>>> CarrierRobot::getFriendList() {
        return mDatabaseProxy->getFriendList();
    }

    void CarrierRobot::OnCarrierFriendMessage(ElaCarrier *carrier, const char *from,
                                              const void *msg, size_t len,
                                              int64_t timestamp, bool offline,
                                              void *context) {
        Log::I(Log::TAG, "OnCarrierFriendMessage from: %s len=%zu offline=%d ts=%lld", from, len,
               offline ? 1 : 0, static_cast<long long>(timestamp));
        auto carrier_robot = reinterpret_cast<CarrierRobot *>(context);
        // Carrier payloads are not NUL-terminated; take exactly len bytes and drop trailing NULs
        // (fix carried on the live box since 2026-09-14, folded in here). A file message is
        // binary and may legitimately end in 0x00, so it is kept as is.
        std::string text((const char *) msg, len);
        if (parseFileMessage(text, nullptr, nullptr) == FileMessageKind::NotFile) {
            while (!text.empty() && text.back() == '\0') text.pop_back();
        }
        std::time_t send_time = carrier_robot->getTimeStamp();
        carrier_robot->addMessgae(std::make_shared<std::string>(from),
                                  std::make_shared<std::string>(text),
                                  send_time, offline, timestamp);
    }

    void CarrierRobot::stop() {
        mDatabaseProxy->closeDb();
    }

    int CarrierRobot::start(const char *data_dir, int service_id, int my_socket_fd) {
        mMySocketFd = my_socket_fd;
        ElaOptions carrierOpts;
        ElaCallbacks carrierCallbacks;
        mLocalPath = std::make_shared<std::string>(data_dir);
        mDatabaseProxy->startDb(data_dir);
        seedFloodGuard();
        memset(&carrierOpts, 0, sizeof(carrierOpts));
        memset(&carrierCallbacks, 0, sizeof(carrierCallbacks));
        carrierCallbacks.connection_status = OnCarrierConnection;
        carrierCallbacks.friend_request = OnCarrierFriendRequest;
        carrierCallbacks.friend_connection = OnCarrierFriendConnection;
        carrierCallbacks.friend_message = OnCarrierFriendMessage;
        carrierCallbacks.friend_info = OnCarrierFriendInfoChanged;
        carrierOpts.udp_enabled = mCarrierConfig->mEnableUdp;
        carrierOpts.persistent_location = data_dir;

        // set BootstrapNode
        size_t carrierNodeSize = mCarrierConfig->mBootstrapNodes.size();
        BootstrapNode carrierNodeArray[carrierNodeSize];

        memset(carrierNodeArray, 0, sizeof(carrierNodeArray));
        for (int idx = 0; idx < carrierNodeSize; idx++) {
            const auto &node = mCarrierConfig->mBootstrapNodes[idx];
            carrierNodeArray[idx].ipv4 = strdup(node->mIpv4->c_str());
            carrierNodeArray[idx].ipv6 = NULL;
            carrierNodeArray[idx].port = strdup(node->mPort->c_str());
            carrierNodeArray[idx].public_key = strdup(node->mPublicKey->c_str());
        }
        carrierOpts.bootstraps_size = carrierNodeSize;
        carrierOpts.bootstraps = carrierNodeArray;

        size_t expressNodeSize = mCarrierConfig->mExpressNodes.size();
        std::vector<ExpressNode> expressNodeArray(expressNodeSize);
        for (size_t idx = 0; idx < expressNodeSize; idx++) {
            const auto &node = mCarrierConfig->mExpressNodes[idx];
            expressNodeArray[idx].ipv4 = strdup(node->mIpv4->c_str());
            expressNodeArray[idx].ipv6 = NULL;
            expressNodeArray[idx].port = strdup(node->mPort->c_str());
            expressNodeArray[idx].public_key = strdup(node->mPublicKey->c_str());
        }
        carrierOpts.express_nodes_size = expressNodeSize;
        carrierOpts.express_nodes = expressNodeSize > 0 ? expressNodeArray.data() : nullptr;
        mExpressSince = mDatabaseProxy->getOrInitMeta("express_since_us", getTimeStamp());
        Log::I(TAG, "CarrierRobot::start express nodes=%zu express_since_us=%lld", expressNodeSize,
               static_cast<long long>(mExpressSince));
        ela_log_init(static_cast<ElaLogLevel>(mCarrierConfig->mLogLevel), nullptr, nullptr);

        auto creater = [&]() -> ElaCarrier * {
            ela_log_init(ElaLogLevel::ElaLogLevel_Warning, data_dir, nullptr);
            auto ptr = ela_new(&carrierOpts, &carrierCallbacks, this);
            return ptr;
        };

        auto deleter = [=](ElaCarrier *ptr) -> void {
            if (ptr != nullptr) {
                ela_session_cleanup(ptr);
                ela_kill(ptr);
            }
        };

        mCarrier = std::unique_ptr<ElaCarrier, std::function<void(ElaCarrier *)>>(creater(),
                deleter);
        if (mCarrier == nullptr) {
            Log::E(TAG, "Failed to new carrier!");
            int err = ela_get_error();
            char strerr_buf[512] = {0};
            ela_get_strerror(err, strerr_buf, sizeof(strerr_buf));
            Log::E(Log::TAG, "CarrierRobot::start failed! ret=%s(0x%x)", strerr_buf, err);
            return ErrCode::FailedCarrier;
        }

        int name_ret = applySelfDisplayName(service_id);
        if (name_ret != 0) {
            Log::I(TAG, "CarrierRobot::start continue with default self info, err=%d", name_ret);
        }
        std::shared_ptr<MemberInfo> member_info = mDatabaseProxy->getMemberInfo(1);
        if (member_info.get() != nullptr) {
            mCreaterFriendId = member_info->mFriendid;
        }
        getAddress(mAddress);
        json msg;
        msg["cmd"] = Command_UpdateAddress;
        msg["serviceid"] = service_id;
        msg["friendid"] = mAddress.c_str();
        sendMsgForManager(dumpJsonForTransport(msg));
        return 0;
    }

    int CarrierRobot::getUserId(std::string &userid) {
        char addr[ELA_MAX_ID_LEN + 1] = {0};
        auto ret = ela_get_userid(mCarrier.get(), addr, sizeof(addr));
        if (ret == nullptr) {
            int err = ela_get_error();
            char strerr_buf[512] = {0};
            ela_get_strerror(err, strerr_buf, sizeof(strerr_buf));
            return ErrCode::FailedCarrier;
        }
        userid = addr;
        return 0;
    }

    int CarrierRobot::acceptFriend(std::shared_ptr<std::string>friendid) {
        std::shared_ptr<MemberInfo> member_info = mDatabaseProxy->getBlockMemberInfo(friendid);
        if (member_info.get() == nullptr) {
            mDatabaseProxy->deleteRemovedListMember(*friendid.get());
            ela_accept_friend(mCarrier.get(), friendid->c_str());
        } else {
            Log::D(TAG, "acceptFriend can't accept friend, block member:%s", friendid->c_str());
        }

        return 0;
    }

    bool CarrierRobot::requestAgentFriend(const std::string &address, std::string &error_message) {
        std::string agent_user_id;
        int parse_ret = GetCarrierUsrIdByAddress(address, agent_user_id);
        if (parse_ret != 0 || agent_user_id.empty()) {
            error_message = "invalid carrier address";
            return false;
        }

        if (!mAddress.empty()
            && mAddress == address) {
            error_message = "cannot use the group address as agent";
            return false;
        }

        if (mDatabaseProxy->hasAgent(agent_user_id)) {
            mDatabaseProxy->addAgent(agent_user_id, address);
            return true;
        }

        std::shared_ptr<MemberInfo> agent_member = mDatabaseProxy->getMemberInfo(
                std::make_shared<std::string>(agent_user_id));
        if (agent_member.get() != nullptr) {
            // The target is already a group member: no friend request needed,
            // but the agent registration must still be recorded — returning
            // without addAgent() reported "Agent registered" while relay kept
            // sending plain text (agent_table never gained the userid).
            if (!mDatabaseProxy->addAgent(agent_user_id, address)) {
                error_message = "failed to persist agent registration";
                return false;
            }
            return true;
        }

        ElaFriendInfo carrier_friend_info;
        memset(&carrier_friend_info, 0, sizeof(carrier_friend_info));
        if (ela_get_friend_info(mCarrier.get(), agent_user_id.c_str(), &carrier_friend_info) == 0) {
            mDatabaseProxy->addAgent(agent_user_id, address);
            return true;
        }

        std::shared_ptr<MemberInfo> agent_block_member = mDatabaseProxy->getBlockMemberInfo(
                std::make_shared<std::string>(agent_user_id));
        if (agent_block_member.get() != nullptr) {
            error_message = "target agent user is blocked in this group";
            return false;
        }

        int add_ret = ela_add_friend(mCarrier.get(), address.c_str(), "openclaw agent invite");
        if (add_ret != 0) {
            int err = ela_get_error();
            char strerr_buf[512] = {0};
            ela_get_strerror(err, strerr_buf, sizeof(strerr_buf));
            error_message = std::string("failed to add agent friend: ") + strerr_buf;
            return false;
        }

        if (!mDatabaseProxy->addAgent(agent_user_id, address)) {
            error_message = "friend request sent, but failed to save agent metadata";
            return false;
        }
        return true;
    }

    int CarrierRobot::getAddress(std::string &address) {
        char addr[ELA_MAX_ADDRESS_LEN + 1] = {0};
        auto ret = ela_get_address(mCarrier.get(), addr, sizeof(addr));
        if (ret == nullptr) {
            int err = ela_get_error();
            char strerr_buf[512] = {0};
            ela_get_strerror(err, strerr_buf, sizeof(strerr_buf));

            return ErrCode::FailedCarrier;
        }

        address = addr;
        return 0;
    }

    std::time_t CarrierRobot::originalSendTime(bool offline, std::time_t recv_time, int64_t post_time) {
        // Only an offline (Express) copy carries the sender's time; a live message's
        // timestamp is its arrival. Kept only when it is sane and clearly earlier.
        if (!offline) return 0;
        constexpr int64_t kMinute = 60LL * 1000000LL;
        int64_t posted = normalizeToMicros(post_time);
        if (posted <= 0 || posted > static_cast<int64_t>(recv_time) - kMinute) return 0;
        return static_cast<std::time_t>(posted);
    }

    std::string CarrierRobot::convertDatetimeToUtcString(std::time_t time) {
        std::time_t t = time;
        if (t > 100000000000000LL) {
            t /= 1000000LL;
        } else if (t > 100000000000LL) {
            t /= 1000LL;
        }
        std::tm tm_utc{};
        gmtime_r(&t, &tm_utc);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_utc);
        return std::string(buf);
    }

    std::string CarrierRobot::convertDatetimeToString(std::time_t time) {
        std::time_t normalized_time = time;
        if (normalized_time > 100000000000000LL) {
            normalized_time /= 1000000LL;
        } else if (normalized_time > 100000000000LL) {
            normalized_time /= 1000LL;
        }
        tm *tm_ = localtime(&normalized_time);                // 将time_t格式转换为tm结构体
        int year, month, day, hour, minute, second;// 定义时间的各个int临时变量。
        year = tm_->tm_year +
               1900;                // 临时变量，年，由于tm结构体存储的是从1900年开始的时间，所以临时变量int为tm_year加上1900。
        month = tm_->tm_mon +
                1;                   // 临时变量，月，由于tm结构体的月份存储范围为0-11，所以临时变量int为tm_mon加上1。
        day = tm_->tm_mday;                        // 临时变量，日。
        hour = tm_->tm_hour;                       // 临时变量，时。
        minute = tm_->tm_min;                      // 临时变量，分。
        second = tm_->tm_sec;                      // 临时变量，秒。
        char yearStr[5], monthStr[3], dayStr[3], hourStr[3], minuteStr[3], secondStr[3];// 定义时间的各个char*变量。
        sprintf(yearStr, "%d", year);              // 年。
        sprintf(monthStr, "%d", month);            // 月。
        sprintf(dayStr, "%d", day);                // 日。
        sprintf(hourStr, "%d", hour);              // 时。
        sprintf(minuteStr, "%d", minute);          // 分。
        if (minuteStr[1] == '\0')                  // 如果分为一位，如5，则需要转换字符串为两位，如05。
        {
            minuteStr[2] = '\0';
            minuteStr[1] = minuteStr[0];
            minuteStr[0] = '0';
        }
        sprintf(secondStr, "%d", second);          // 秒。
        if (secondStr[1] == '\0')                  // 如果秒为一位，如5，则需要转换字符串为两位，如05。
        {
            secondStr[2] = '\0';
            secondStr[1] = secondStr[0];
            secondStr[0] = '0';
        }
        char s[20];                                // 定义总日期时间char*变量。
        sprintf(s, "%s-%s-%s %s:%s:%s", yearStr, monthStr, dayStr, hourStr, minuteStr,
                secondStr);// 将年月日时分秒合并。
        std::string str(s);                             // 定义string变量，并将总日期时间char*变量作为构造函数的参数传入。
        return str;                                // 返回转换日期时间后的string变量。
    }

    void CarrierRobot::sendCommandResponse(const std::string &friend_id, const std::string &message) {
        int ela_ret = ela_send_friend_message_noreceipt(mCarrier.get(), friend_id.c_str(),
                                              message.data(), message.size());
        if (ela_ret != 0) {
            Log::I(Log::TAG,
                   "sendCommandResponse .c_str(): %s errno:(0x%x)",
                   message.c_str(), ela_get_error());
        }
    }

    bool CarrierRobot::addAgentByAddress(const std::string &address, std::string &error_message) {
        return requestAgentFriend(address, error_message);
    }

    bool CarrierRobot::removeAgentByUserId(const std::string &user_id, std::string &error_message) {
        if (!mDatabaseProxy->hasAgent(user_id)) {
            error_message = "agent userid not found in this group";
            return false;
        }

        int remove_ret = ela_remove_friend(mCarrier.get(), user_id.c_str());
        bool db_removed = mDatabaseProxy->removeAgent(user_id);
        if (!db_removed) {
            error_message = "failed to remove agent metadata";
            return false;
        }

        if (remove_ret != 0) {
            error_message = std::string("carrier remove returned error: ")
                            + std::to_string(ela_get_error());
            return false;
        }
        return true;
    }

    std::shared_ptr<std::vector<std::shared_ptr<std::string>>> CarrierRobot::getAgentUserIdList() {
        return mDatabaseProxy->getAgentUserIdList();
    }

    std::shared_ptr<std::string> CarrierRobot::getAgentAddressByUserId(const std::string &user_id) {
        return mDatabaseProxy->getAgentAddress(user_id);
    }

    std::string CarrierRobot::normalizeIncomingMessageForStorage(const std::string &friend_id,
                                                                 const std::string &message) {
        if (!mDatabaseProxy->hasAgent(friend_id)) {
            return message;
        }
        if (!startsWith(message, kAgentReplyPrefix)) {
            return message;
        }

        const std::string raw_payload = message.substr(strlen(kAgentReplyPrefix));
        try {
            json payload;
            try {
                payload = json::parse(raw_payload);
            } catch (...) {
                payload = json::parse(sanitizeJsonPayload(raw_payload));
            }

            auto type_it = payload.find("type");
            if (type_it != payload.end()
                && type_it->is_string()) {
                const std::string type_value = normalizeProtocolToken(type_it->get<std::string>());
                if (type_value != "carriergroupreply") {
                    return message;
                }
            }

            std::string chat_type_value;
            auto chat_type_it = payload.find("chat_type");
            if (chat_type_it != payload.end()
                && chat_type_it->is_string()) {
                chat_type_value = chat_type_it->get<std::string>();
            } else {
                auto compat_chat_type_it = payload.find("chattype");
                if (compat_chat_type_it != payload.end()
                    && compat_chat_type_it->is_string()) {
                    chat_type_value = compat_chat_type_it->get<std::string>();
                }
            }
            if (!chat_type_value.empty()
                && normalizeProtocolToken(chat_type_value) != "group") {
                return message;
            }

            auto group_it = payload.find("group");
            if (group_it != payload.end() && group_it->is_object()) {
                auto group_address_it = group_it->find("address");
                if (group_address_it != group_it->end()
                    && group_address_it->is_string()
                    && !mAddress.empty()
                    && group_address_it->get<std::string>() != mAddress) {
                    return message;
                }
            }

            std::string reply_text;
            auto message_it = payload.find("message");
            if (message_it != payload.end() && message_it->is_object()) {
                auto text_it = message_it->find("text");
                if (text_it != message_it->end() && text_it->is_string()) {
                    reply_text = text_it->get<std::string>();
                }
            } else if (message_it != payload.end() && message_it->is_string()) {
                reply_text = message_it->get<std::string>();
            }
            if (reply_text.empty()) {
                auto text_it = payload.find("text");
                if (text_it != payload.end() && text_it->is_string()) {
                    reply_text = text_it->get<std::string>();
                }
            }
            if (reply_text.empty()) {
                return message;
            }

            auto reply_to_it = payload.find("reply_to");
            if (reply_to_it != payload.end() && reply_to_it->is_object()) {
                auto user_id_it = reply_to_it->find("userid");
                if ((user_id_it == reply_to_it->end() || !user_id_it->is_string())) {
                    user_id_it = reply_to_it->find("userId");
                }
                if ((user_id_it == reply_to_it->end() || !user_id_it->is_string())) {
                    user_id_it = reply_to_it->find("friendid");
                }
                if (user_id_it != reply_to_it->end() && user_id_it->is_string()) {
                    std::shared_ptr<std::string> reply_user_id = std::make_shared<std::string>(user_id_it->get<std::string>());
                    std::shared_ptr<MemberInfo> target_member = mDatabaseProxy->getMemberInfo(reply_user_id);
                    if (target_member.get() != nullptr && target_member->mNickName.get() != nullptr) {
                        target_member->Lock();
                        std::string target_name = *target_member->mNickName.get();
                        target_member->UnLock();
                        if (!target_name.empty()) {
                            reply_text = std::string("@") + target_name + " " + reply_text;
                        }
                    }
                }
            }
            return reply_text;
        } catch (...) {
            return message;
        }
    }

    std::string CarrierRobot::normalizeAgentStatusEnvelope(const std::string &friend_id,
                                                           const std::string &message) {
        if (!mDatabaseProxy->hasAgent(friend_id)) {
            return "";
        }
        if (!startsWith(message, kAgentStatusPrefix)) {
            return "";
        }

        const std::string raw_payload = message.substr(strlen(kAgentStatusPrefix));
        if (raw_payload.empty()) {
            return "";
        }

        try {
            json payload;
            try {
                payload = json::parse(raw_payload);
            } catch (...) {
                payload = json::parse(sanitizeJsonPayload(raw_payload));
            }

            auto type_it = payload.find("type");
            if (type_it != payload.end() && type_it->is_string()) {
                const std::string type_value = normalizeProtocolToken(type_it->get<std::string>());
                if (type_value != "agentstatus"
                    && type_value != "carriergroupstatus") {
                    return "";
                }
            }

            std::string chat_type_value;
            auto chat_type_it = payload.find("chat_type");
            if (chat_type_it != payload.end() && chat_type_it->is_string()) {
                chat_type_value = chat_type_it->get<std::string>();
            } else {
                auto compat_chat_type_it = payload.find("chattype");
                if (compat_chat_type_it != payload.end() && compat_chat_type_it->is_string()) {
                    chat_type_value = compat_chat_type_it->get<std::string>();
                }
            }
            if (!chat_type_value.empty()
                && normalizeProtocolToken(chat_type_value) != "group") {
                return "";
            }

            auto group_it = payload.find("group");
            if (group_it != payload.end() && group_it->is_object()) {
                auto group_address_it = group_it->find("address");
                if (group_address_it != group_it->end()
                    && group_address_it->is_string()
                    && !mAddress.empty()
                    && group_address_it->get<std::string>() != mAddress) {
                    return "";
                }
            }

            auto status_it = payload.find("status");
            if (status_it == payload.end() || !status_it->is_object()) {
                return "";
            }
            auto state_it = status_it->find("state");
            if (state_it == status_it->end() || !state_it->is_string()) {
                return "";
            }
            std::string state = normalizeProtocolToken(state_it->get<std::string>());
            if (state.empty()) {
                return "";
            }

            json normalized;
            normalized["type"] = "agent_status";
            normalized["version"] = 1;
            normalized["chat_type"] = "group";
            normalized["source"] = "carrier_group_service";
            normalized["origin"]["userid"] = friend_id;
            normalized["origin"]["friendid"] = friend_id;
            normalized["group"]["address"] = mAddress;

            std::string group_user_id;
            if (getUserId(group_user_id) == 0) {
                normalized["group"]["userid"] = group_user_id;
            }

            std::shared_ptr<std::string> group_nickname = mDatabaseProxy->getGroupNickName();
            normalized["group"]["nickname"] = group_nickname.get() != nullptr ? *group_nickname.get() : "";

            if (group_it != payload.end() && group_it->is_object()) {
                auto group_user_id_it = group_it->find("userid");
                if (group_user_id_it != group_it->end() && group_user_id_it->is_string()) {
                    normalized["group"]["userid"] = group_user_id_it->get<std::string>();
                }
                auto group_name_it = group_it->find("name");
                if (group_name_it != group_it->end() && group_name_it->is_string()) {
                    normalized["group"]["nickname"] = group_name_it->get<std::string>();
                }
            }

            normalized["status"]["state"] = state;
            auto phase_it = status_it->find("phase");
            if (phase_it != status_it->end() && phase_it->is_string()) {
                normalized["status"]["phase"] = phase_it->get<std::string>();
            }
            auto ttl_it = status_it->find("ttl_ms");
            if (ttl_it != status_it->end() && ttl_it->is_number_integer()) {
                normalized["status"]["ttl_ms"] = ttl_it->get<int>();
            } else if (ttl_it != status_it->end() && ttl_it->is_string()) {
                try {
                    normalized["status"]["ttl_ms"] = std::stoi(ttl_it->get<std::string>());
                } catch (...) {
                    normalized["status"]["ttl_ms"] = 12000;
                }
            } else {
                normalized["status"]["ttl_ms"] = 12000;
            }
            auto seq_it = status_it->find("seq");
            if (seq_it != status_it->end() && seq_it->is_string()) {
                normalized["status"]["seq"] = seq_it->get<std::string>();
            }
            auto ts_it = status_it->find("ts");
            if (ts_it != status_it->end() && ts_it->is_number_integer()) {
                normalized["status"]["ts"] = ts_it->get<long long>();
            } else if (ts_it != status_it->end() && ts_it->is_string()) {
                try {
                    normalized["status"]["ts"] = std::stoll(ts_it->get<std::string>());
                } catch (...) {
                    normalized["status"]["ts"] = static_cast<long long>(std::time(nullptr));
                }
            } else {
                normalized["status"]["ts"] = static_cast<long long>(std::time(nullptr));
            }
            return std::string(kAgentStatusPrefix) + dumpJsonForTransport(normalized);
        } catch (...) {
            return "";
        }
    }

    bool CarrierRobot::relayAgentStatusToMembers(const std::string &agent_user_id,
                                                 const std::string &status_message) {
        std::shared_ptr<std::vector<std::shared_ptr<MemberInfo>>> member_list = mDatabaseProxy->getFriendList();
        if (member_list.get() == nullptr) {
            return false;
        }

        bool relayed = false;
        for (int i = 0; i < member_list->size(); i++) {
            std::shared_ptr<MemberInfo> member_info = member_list->at(i);
            if (member_info.get() == nullptr || member_info->mFriendid.get() == nullptr) {
                continue;
            }
            member_info->Lock();
            const std::string member_friend_id = *member_info->mFriendid.get();
            member_info->UnLock();
            if (member_friend_id.empty()
                || member_friend_id == agent_user_id
                || mDatabaseProxy->hasAgent(member_friend_id)) {
                continue;
            }

            int ela_ret = ela_send_friend_message_noreceipt(mCarrier.get(),
                                                  member_friend_id.c_str(),
                                                  status_message.data(),
                                                  status_message.size());
            if (ela_ret != 0) {
                Log::I(Log::TAG,
                       "relayAgentStatusToMembers failed: to=%s errno=(0x%x)",
                       member_friend_id.c_str(), ela_get_error());
                continue;
            }
            relayed = true;
        }
        return relayed;
    }

    std::string CarrierRobot::buildOutboundMessageForRecipient(const std::string &recipient_user_id,
                                                               std::shared_ptr<MessageInfo> message) {
        if (message.get() == nullptr || message->mMsg.get() == nullptr || message->mFriendid.get() == nullptr) {
            return "";
        }

        std::string sender_user_id = *message->mFriendid.get();
        std::string sender_nickname = buildFallbackUserName(sender_user_id.c_str());
        ElaFriendInfo sender_friend_info;
        bool has_sender_friend_info = false;
        memset(&sender_friend_info, 0, sizeof(sender_friend_info));
        if (ela_get_friend_info(mCarrier.get(), sender_user_id.c_str(), &sender_friend_info) == 0) {
            has_sender_friend_info = true;
            if (sender_friend_info.user_info.name[0] != '\0') {
                sender_nickname = sender_friend_info.user_info.name;
            }
        }

        std::shared_ptr<MemberInfo> sender_member_info = mDatabaseProxy->getMemberInfo(message->mFriendid);
        if (sender_member_info.get() != nullptr) {
            sender_member_info->Lock();
            if (sender_member_info->mNickName.get() != nullptr
                && !sender_member_info->mNickName->empty()) {
                sender_nickname = *sender_member_info->mNickName.get();
            }
            sender_member_info->UnLock();
        }

        json file_meta;
        size_t file_meta_len = 0;
        const bool is_file = parseFileMessage(*message->mMsg.get(), &file_meta, &file_meta_len)
                             == FileMessageKind::File;
        const bool to_agent = mDatabaseProxy->hasAgent(recipient_user_id);

        std::string group_user_id;
        getUserId(group_user_id);
        std::shared_ptr<std::string> group_nickname_ptr = mDatabaseProxy->getGroupNickName();
        const std::string group_nickname = group_nickname_ptr.get() != nullptr ? *group_nickname_ptr.get() : "";

        if (is_file && !to_agent) {
            // Same envelope the member sent, with the real sender added to the
            // meta (readers ignore keys they do not know). Whatever the sender
            // put under these keys is replaced, so it cannot spoof another member.
            static const char *kRelayKeys[] = {"chat_type", "group", "origin", "sent_at_ms"};
            for (const char *key : kRelayKeys) {
                file_meta.erase(key);
            }
            json relay_meta = file_meta;
            relay_meta["chat_type"] = "group";
            relay_meta["group"]["userid"] = group_user_id;
            relay_meta["group"]["address"] = mAddress;
            relay_meta["group"]["nickname"] = group_nickname;
            relay_meta["origin"]["userid"] = sender_user_id;
            relay_meta["origin"]["nickname"] = sender_nickname;
            relay_meta["sent_at_ms"] = static_cast<long long>(
                    (message->mPostTimeStamp > 0 ? message->mPostTimeStamp : message->mSendTimeStamp) / 1000);
            const size_t body_len = message->mMsg->size() - 4 - file_meta_len;
            std::string meta_str = dumpJsonForTransport(relay_meta);
            if (meta_str.size() > kFileMetaMaxLen
                || 4 + meta_str.size() + body_len > kFileMessageMaxLen) {
                // No room for the sender fields: relay the file without them.
                meta_str = dumpJsonForTransport(file_meta);
            }
            return packFileMessage(meta_str, *message->mMsg.get(), file_meta_len);
        }

        std::string body_text = *message->mMsg.get();
        if (is_file) {
            const std::string filename = metaString(file_meta, "filename", "file");
            body_text = std::string("[file] ") + filename + " ("
                        + std::to_string(message->mMsg->size() - 4 - file_meta_len) + " bytes)";
        }
        std::string plain_text = sender_nickname + ": " + body_text
                                 + " \n[" + this->convertDatetimeToUtcString(
                                         message->mPostTimeStamp > 0 ? message->mPostTimeStamp
                                                                     : message->mSendTimeStamp) + "]";
        if (!to_agent) {
            return plain_text;
        }

        json envelope;
        envelope["type"] = "carrier_group_message";
        envelope["version"] = 1;
        envelope["chat_type"] = "group";
        envelope["source"] = "carrier_group_service";
        envelope["group"]["address"] = mAddress;
        if (!group_user_id.empty()) {
            envelope["group"]["userid"] = group_user_id;
        }
        envelope["group"]["nickname"] = group_nickname;
        envelope["origin"]["userid"] = sender_user_id;
        envelope["origin"]["friendid"] = sender_user_id;
        envelope["origin"]["nickname"] = sender_nickname;
        if (has_sender_friend_info) {
            envelope["origin"]["user_info"] = buildUserInfoJson(sender_friend_info.user_info);
            envelope["origin"]["friend_info"] = buildFriendInfoJson(sender_friend_info);
            envelope["origin"]["agentnet"] = buildAgentNetJson(mCarrier.get(), sender_user_id.c_str());
        } else {
            envelope["origin"]["user_info"]["userid"] = sender_user_id;
            envelope["origin"]["user_info"]["name"] = sender_nickname;
            envelope["origin"]["user_info"]["description"] = "";
            envelope["origin"]["user_info"]["has_avatar"] = 0;
            envelope["origin"]["user_info"]["gender"] = "";
            envelope["origin"]["user_info"]["phone"] = "";
            envelope["origin"]["user_info"]["email"] = "";
            envelope["origin"]["user_info"]["region"] = "";

            int status_code = 1;
            if (sender_member_info.get() != nullptr) {
                status_code = sender_member_info->mStatus;
            }
            ElaConnectionStatus status = status_code == 0
                                         ? ElaConnectionStatus_Connected
                                         : ElaConnectionStatus_Disconnected;
            envelope["origin"]["friend_info"]["label"] = "";
            envelope["origin"]["friend_info"]["status"] = status_code;
            envelope["origin"]["friend_info"]["status_text"] = connectionStatusToString(status);
            envelope["origin"]["friend_info"]["presence"] = static_cast<int>(ElaPresenceStatus_None);
            envelope["origin"]["friend_info"]["presence_text"] = presenceStatusToString(ElaPresenceStatus_None);
        }
        envelope["message"]["text"] = body_text;
        if (is_file) {
            // Agents keep the group envelope (group + real sender) and get the
            // bytes inside it as base64 when they fit; agents that do not know
            // data_base64 ignore it and still see the description.
            envelope["message"]["file"]["filename"] = metaString(file_meta, "filename", "file");
            envelope["message"]["file"]["contentType"] =
                    metaString(file_meta, "contentType", "application/octet-stream");
            envelope["message"]["file"]["size"] =
                    static_cast<unsigned long long>(message->mMsg->size() - 4 - file_meta_len);
        }
        envelope["message"]["timestamp"] = static_cast<long long>(
                message->mPostTimeStamp > 0 ? message->mPostTimeStamp : message->mSendTimeStamp);
        envelope["render"]["plain"] = plain_text;
        if (is_file) {
            const size_t body_offset = 4 + file_meta_len;
            const size_t body_len = message->mMsg->size() - body_offset;
            // Cheap pre-check before encoding: base64 is 4/3 of the bytes.
            if (((body_len + 2) / 3) * 4 + 4096 < kFileMessageMaxLen) {
                envelope["message"]["file"]["data_base64"] =
                        base64Encode(message->mMsg->data() + body_offset, body_len);
                std::string with_data = std::string(kAgentOutboundPrefix) + dumpJsonForTransport(envelope);
                if (with_data.size() <= kFileMessageMaxLen) {
                    return with_data;
                }
                envelope["message"]["file"].erase("data_base64");
            }
        }
        return std::string(kAgentOutboundPrefix) + dumpJsonForTransport(envelope);
    }

    bool CarrierRobot::isGroupCreator(const std::string &friend_id) {
        return mCreaterFriendId.get() != nullptr
               && mCreaterFriendId->compare(friend_id) == 0;
    }

    void CarrierRobot::helpCmd(const std::vector<std::string> &args, const std::string &message) {
        if (args.size() >= 2) {
            // Do() appends the sender's userid last; "/help x" must not reply to "x".
            const std::string friend_id = args.back();
            int ela_ret = ela_send_friend_message_noreceipt(mCarrier.get(), friend_id.c_str(),
                                                  message.data(), message.size());
            if (ela_ret != 0) {
                Log::I(Log::TAG,
                       "helpCmd .c_str(): %s errno:(0x%x)",
                       message.c_str(), ela_get_error());
            }
        }

    }

    // /list [page]: one page of display names (feedback#26). The whole list
    // used to go out in one message (16.7 KB for 507 members).
    void CarrierRobot::listCmd(const std::vector<std::string> &args) {
        if (args.size() >= 2) {
            const std::string friend_id = args.back();
            std::shared_ptr<std::vector<std::shared_ptr<MemberInfo>>> memberlist = mDatabaseProxy->getFriendList();
            std::vector<MemberLine> lines;
            lines.reserve(memberlist->size());
            for (size_t i = 0; i < memberlist->size(); i++) {
                std::shared_ptr<MemberInfo> memberInfo = memberlist->at(i);
                if (memberInfo.get() == nullptr) {
                    continue;
                }
                memberInfo->Lock();
                lines.push_back({memberInfo->mIndex,
                                 memberInfo->mNickName ? *memberInfo->mNickName.get() : std::string(),
                                 memberInfo->mStatus == 0});
                memberInfo->UnLock();
            }
            const size_t page = memberListPageArg(args);
            const std::string reply = formatMemberPage(lines, page);
            Log::I(TAG, "listCmd to=%s page=%zu members=%zu bytes=%zu", friend_id.c_str(), page,
                   lines.size(), reply.size());
            sendCommandResponse(friend_id, reply);
        }
    }

    int CarrierRobot::getGroupStatus() {
        return mStatus;
    }

    std::shared_ptr<std::string> CarrierRobot::getGroupNickName() {
        std::shared_ptr<std::string> nick_name = mDatabaseProxy->getGroupNickName();
        return nick_name;
    }

    void CarrierRobot::updateNickNameCmd(const std::vector<std::string> &args) {
        MUTEX_LOCKER locker_sync_data(_mReplyMessage);
        if (args.size() >= 3) {
            // Do() appends the sender's userid as the last token; the name is
            // everything in between, so "/update CSTU Internship" keeps both words.
            const std::string friend_id = args.back();
            std::string nick_name;
            for (size_t i = 1; i + 1 < args.size(); i++) {
                if (!nick_name.empty()) nick_name += " ";
                nick_name += args[i];
            }
            if (mCreaterFriendId.get() != nullptr && nick_name.size() <= 64
                && (*mCreaterFriendId.get()).compare((friend_id)) == 0) {
                mDatabaseProxy->updateGroupNickName(std::make_shared<std::string>(nick_name));
                json msg;
                msg["cmd"] =Command_UpdateNickName;
                msg["friendid"] = mAddress.c_str();
                msg["nickname"] = nick_name.c_str();
                sendMsgForManager(dumpJsonForTransport(msg));
            }
        }
    }

    void CarrierRobot::deleteGroupCmd(const std::vector<std::string> &args) {
        MUTEX_LOCKER locker_sync_data(_mReplyMessage);
        if (args.size() >= 2) {
            const std::string friend_id = args[1];
            if ((*mCreaterFriendId.get()).compare((friend_id)) == 0) {
                mStatus = -1;
                json msg;
                msg["cmd"] = Command_UpdateStatus;
                msg["friendid"] = mAddress.c_str();
                msg["my_socket_fd"] = mMySocketFd;
                msg["status"] = mStatus;
                sendMsgForManager(dumpJsonForTransport(msg));
                //好友解除关系
                std::shared_ptr<std::vector<std::shared_ptr<MemberInfo>>> memberlist = mDatabaseProxy->getFriendList();
                for (int i = 0; i < memberlist->size(); i++) {
                    std::shared_ptr<MemberInfo> memberInfo = memberlist->at(i);
                    if (memberInfo.get() == nullptr) {
                        continue;
                    }
                    memberInfo->Lock();
                    int ela_ret = ela_remove_friend(mCarrier.get(),
                                                    memberInfo->mFriendid.get()->c_str());
                    if (ela_ret != 0) {
                        Log::I(Log::TAG,
                               "deleteGroupCmd can't delete this user: %s errno:(0x%x)",
                               memberInfo->mFriendid.get()->c_str(), ela_get_error());
                    }
                    memberInfo->UnLock();
                }
            }
        }
    }

    void CarrierRobot::blockFriendCmd(const std::vector<std::string> &args) {
        if (args.size() >= 3) {
            const std::string friend_id = args[2];
            if ((*mCreaterFriendId.get()).compare((friend_id)) == 0) {
                std::string del_userindex = args[1];
                int user_num = std::atoi(del_userindex.c_str());
                std::shared_ptr<MemberInfo> memberInfo = mDatabaseProxy->getMemberInfo(user_num);
                char msg_str[256];
                if (memberInfo.get() != nullptr) {
                    memberInfo->Lock();
                    std::string del_friend_id = *memberInfo->mFriendid.get();
                    memberInfo->UnLock();
                    mDatabaseProxy->addBlockMember(memberInfo);
                    mDatabaseProxy->removeMember(del_friend_id);

                    int ela_ret = ela_remove_friend(mCarrier.get(),
                                                    memberInfo->mFriendid.get()->c_str());
                    sprintf(msg_str, "%s has been blocked!",
                            memberInfo->mNickName.get()->c_str());
                    if (ela_ret != 0) {
                        Log::I(Log::TAG,
                               "delCmd can't delete this user: %s errno:(0x%x)",
                               memberInfo->mFriendid.get()->c_str(), ela_get_error());
                    }
                } else {
                    sprintf(msg_str, "num %s member not exist!", del_userindex.c_str());
                }
                ela_send_friend_message_noreceipt(mCarrier.get(), friend_id.c_str(),
                                        msg_str, strlen(msg_str));
                json msg;
                msg["cmd"] = Command_UpdateMemberCount;
                msg["friendid"] = mAddress.c_str();
                msg["membercount"] = getFriendList()->size();
                sendMsgForManager(dumpJsonForTransport(msg));
            }
        }
    }

    void CarrierRobot::delCmd(const std::vector<std::string> &args) {
        if (args.size() >= 3) {
            const std::string friend_id = args[2];
            if ((*mCreaterFriendId.get()).compare((friend_id)) == 0) {
                std::string del_userindex = args[1];
                int user_num = std::atoi(del_userindex.c_str());
                std::shared_ptr<MemberInfo> memberInfo = mDatabaseProxy->getMemberInfo(user_num);
                char msg_str[256];
                if (memberInfo.get() != nullptr) {
                    memberInfo->Lock();
                    std::string del_friend_id = *memberInfo->mFriendid.get();
                    memberInfo->UnLock();
                    mDatabaseProxy->removeMember(del_friend_id);
                    int ela_ret = ela_remove_friend(mCarrier.get(),
                                                    memberInfo->mFriendid.get()->c_str());
                    sprintf(msg_str, "%s has been kicked out!",
                            memberInfo->mNickName.get()->c_str());
                    if (ela_ret != 0) {
                        Log::I(Log::TAG,
                               "delCmd can't delete this user: %s errno:(0x%x)",
                               memberInfo->mFriendid.get()->c_str(), ela_get_error());
                    }
                } else {
                    sprintf(msg_str, "num %s member not exist!", del_userindex.c_str());
                }

                ela_send_friend_message_noreceipt(mCarrier.get(), friend_id.c_str(),
                                        msg_str, strlen(msg_str));

                json msg;
                msg["cmd"] = Command_UpdateMemberCount;
                msg["friendid"] = mAddress.c_str();
                msg["membercount"] = getFriendList()->size();
                sendMsgForManager(dumpJsonForTransport(msg));
            }
        }
    }

    void CarrierRobot::registerCarrierCallBack(const std::function<void (const std::string)> &carrierCallBack) {
        mCarrierCallBack = carrierCallBack;
    }

    void  CarrierRobot::sendMsgForManager(std::string msg) {
        if (mCarrierCallBack != nullptr) {
            mCarrierCallBack(msg);
        }
    }
    void CarrierRobot::showAddressCmd(const std::vector<std::string> &args) {
        if (args.size() >= 2) {
            std::string address;
            this->getAddress(address);

            const std::string friend_id = args[1];
            int ela_ret = ela_send_friend_message_noreceipt(mCarrier.get(), friend_id.c_str(),
                                                  address.data(), address.size());
            if (ela_ret != 0) {
                Log::I(Log::TAG,
                       "showAddressCmd .c_str(): %s errno:(0x%x)",
                       address.c_str(), ela_get_error());
            }
        }

    }

    void CarrierRobot::agentCmd(const std::vector<std::string> &args) {
        if (args.size() < 2) {
            return;
        }
        const std::string friend_id = args.back();

        if (args.size() == 2) {
            sendCommandResponse(friend_id,
                                "Usage: /agent <address> | /agent add <address> | /agent del <userid> | /agent list");
            return;
        }

        const std::string action_or_address = args[1];

        if (action_or_address == "list"
            || action_or_address == "ls") {
            auto agent_user_ids = mDatabaseProxy->getAgentUserIdList();
            if (agent_user_ids.get() == nullptr || agent_user_ids->empty()) {
                sendCommandResponse(friend_id, "No agent registered for this group.");
                return;
            }

            std::string result = "Agent list:\n";
            for (int i = 0; i < agent_user_ids->size(); i++) {
                const std::string user_id = *agent_user_ids->at(i).get();
                std::shared_ptr<std::string> address = mDatabaseProxy->getAgentAddress(user_id);
                result += user_id;
                result += " ";
                result += (address.get() != nullptr) ? *address.get() : "";
                result += "\n";
            }
            sendCommandResponse(friend_id, result);
            return;
        }

        if (action_or_address == "del"
            || action_or_address == "remove"
            || action_or_address == "rm") {
            if (args.size() < 4) {
                sendCommandResponse(friend_id, "Usage: /agent del <userid>");
                return;
            }
            if (!isGroupCreator(friend_id)) {
                sendCommandResponse(friend_id, "Only group creator can remove agent.");
                return;
            }

            const std::string agent_user_id = args[2];
            std::string remove_error;
            if (!removeAgentByUserId(agent_user_id, remove_error)) {
                sendCommandResponse(friend_id,
                                    std::string("Failed to remove agent: ") + remove_error);
                return;
            }

            sendCommandResponse(friend_id, std::string("Agent removed: ") + agent_user_id);
            return;
        }

        std::string agent_address = action_or_address;
        if (action_or_address == "add") {
            if (args.size() < 4) {
                sendCommandResponse(friend_id, "Usage: /agent add <address>");
                return;
            }
            agent_address = args[2];
        }

        if (!isGroupCreator(friend_id)) {
            sendCommandResponse(friend_id, "Only group creator can add agent.");
            return;
        }

        std::string error_message;
        if (!requestAgentFriend(agent_address, error_message)) {
            sendCommandResponse(friend_id,
                                std::string("Unable to add agent: ") + error_message);
            return;
        }

        std::string agent_user_id;
        GetCarrierUsrIdByAddress(agent_address, agent_user_id);
        sendCommandResponse(friend_id,
                            std::string("Agent registered: userid=") + agent_user_id
                            + " address=" + agent_address
                            + ". You only need this once unless agent is removed.");
    }

    // The signer list is the payload the group key signs for the group's
    // beagles.eth record. Sorted by byte order and comma-joined, so the same
    // set always produces the same bytes regardless of insertion order.
    std::string CarrierRobot::joinedSigners() {
        auto signers = mDatabaseProxy->getSignerUserIdList();
        std::vector<std::string> ids;
        if (signers.get() != nullptr) {
            for (int i = 0; i < signers->size(); i++) {
                ids.push_back(*signers->at(i).get());
            }
        }
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

        std::string joined;
        for (size_t i = 0; i < ids.size(); i++) {
            if (i > 0) {
                joined += ",";
            }
            joined += ids[i];
        }
        return joined;
    }

    std::string CarrierRobot::buildSignerProofPayload(std::time_t timestamp) {
        std::string group_user_id;
        getUserId(group_user_id);

        return std::string("beagle-group-signers\n") + group_user_id + "\n" + joinedSigners() + "\n"
               + std::to_string(static_cast<long long>(timestamp));
    }

    // The ONLY use of the group's Carrier key outside the SDK's own protocol:
    // an XEdDSA signature over buildSignerProofPayload(), made inside
    // libcarrier (carrier_identity_sign), which never hands the key out.
    // Stored as "<lowercase sigHex>.<unix seconds>" next to the exact signer
    // string it covers; /group/signers serves it from the group DB.
    bool CarrierRobot::signSignerList(std::string &proof, std::string &error) {
        std::string group_user_id;
        if (getUserId(group_user_id) != 0 || group_user_id.empty()) {
            error = "group userid unavailable";
            return false;
        }
        const std::time_t now = std::time(nullptr);
        const std::string signers = joinedSigners();
        const std::string payload = std::string("beagle-group-signers\n") + group_user_id + "\n"
                                    + signers + "\n"
                                    + std::to_string(static_cast<long long>(now));

        std::string hex;
        if (!signWithGroupKey(payload, hex, error)) {
            return false;
        }
        proof = hex + "." + std::to_string(static_cast<long long>(now));

        if (!mDatabaseProxy->setSignerProof(signers, proof, now)) {
            error = "failed to store proof";
            return false;
        }
        Log::I(Log::TAG, "signer list signed: [%s] ts=%lld", signers.c_str(),
               static_cast<long long>(now));
        return true;
    }

    bool CarrierRobot::signWithGroupKey(const std::string &bytes, std::string &sig_hex,
                                        std::string &error) {
        std::string group_user_id;
        if (getUserId(group_user_id) != 0 || group_user_id.empty()) {
            error = "group userid unavailable";
            return false;
        }
        uint8_t sig[CARRIER_IDENTITY_SIGNATURE_BYTES] = {0};
        Carrier *carrier = reinterpret_cast<Carrier *>(mCarrier.get());
        if (carrier_identity_sign(carrier, reinterpret_cast<const uint8_t *>(bytes.data()),
                                  bytes.size(), sig) != 0) {
            error = "carrier_identity_sign failed";
            return false;
        }
        if (carrier_identity_verify(group_user_id.c_str(),
                                    reinterpret_cast<const uint8_t *>(bytes.data()),
                                    bytes.size(), sig) != 1) {
            error = "self-verify failed";
            return false;
        }
        static const char kHex[] = "0123456789abcdef";
        sig_hex.clear();
        sig_hex.reserve(CARRIER_IDENTITY_SIGNATURE_BYTES * 2);
        for (int i = 0; i < CARRIER_IDENTITY_SIGNATURE_BYTES; i++) {
            sig_hex += kHex[sig[i] >> 4];
            sig_hex += kHex[sig[i] & 0x0f];
        }
        return true;
    }

    // Re-sign only when the stored proof does not cover the current list
    // (e.g. a DB from before signing existed).
    bool CarrierRobot::ensureSignerProof(std::string &proof, std::string &error) {
        std::string stored_signers;
        if (mDatabaseProxy->getSignerProof(stored_signers, proof)
            && stored_signers == joinedSigners() && !proof.empty()) {
            return true;
        }
        return signSignerList(proof, error);
    }

    std::string CarrierRobot::describeSigners() {
        auto signers = mDatabaseProxy->getSignerUserIdList();
        if (signers.get() == nullptr || signers->empty()) {
            return "No signer registered for this group.";
        }
        std::string result = "Signer list:\n";
        for (int i = 0; i < signers->size(); i++) {
            result += *signers->at(i).get();
            result += "\n";
        }
        return result;
    }

    void CarrierRobot::signerCmd(const std::vector<std::string> &args) {
        if (args.size() < 2) {
            return;
        }
        const std::string friend_id = args.back();

        if (args.size() == 2) {
            sendCommandResponse(friend_id,
                                "Usage: /signer add <userid> | /signer del <userid> | /signer list");
            return;
        }

        const std::string action = args[1];

        if (action == "list" || action == "ls") {
            std::string proof, error;
            std::string reply = describeSigners();
            if (joinedSigners().empty()) {
                // nothing to prove
            } else if (ensureSignerProof(proof, error)) {
                reply += "\nsignersProof: " + proof;
            } else {
                reply += "\nsignersProof unavailable: " + error;
            }
            sendCommandResponse(friend_id, reply);
            return;
        }

        if (action != "add" && action != "del" && action != "remove" && action != "rm") {
            sendCommandResponse(friend_id,
                                "Usage: /signer add <userid> | /signer del <userid> | /signer list");
            return;
        }

        if (args.size() < 4) {
            sendCommandResponse(friend_id,
                                std::string("Usage: /signer ") + action + " <userid>");
            return;
        }

        if (!isGroupCreator(friend_id)) {
            sendCommandResponse(friend_id, "Only group creator can change signers.");
            return;
        }

        // A signer is a Carrier userid (base58 public key), never an address:
        // an address carries four extra bytes and would never match a signer
        // the gateway sees.
        const std::string signer_user_id = args[2];
        static const std::string kBase58 =
                "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
        bool valid = signer_user_id.size() >= 43 && signer_user_id.size() <= 45;
        for (size_t i = 0; valid && i < signer_user_id.size(); i++) {
            if (kBase58.find(signer_user_id[i]) == std::string::npos) {
                valid = false;
            }
        }
        if (!valid) {
            sendCommandResponse(friend_id,
                                std::string("Not a Carrier userid: ") + signer_user_id
                                + ". Expected 43-45 base58 chars, not a 52-char address.");
            return;
        }

        if (action == "add") {
            if (!mDatabaseProxy->addSigner(signer_user_id)) {
                sendCommandResponse(friend_id, "Failed to add signer.");
                return;
            }
        } else {
            if (!mDatabaseProxy->hasSigner(signer_user_id)) {
                sendCommandResponse(friend_id,
                                    std::string("Not a signer: ") + signer_user_id);
                return;
            }
            if (!mDatabaseProxy->removeSigner(signer_user_id)) {
                sendCommandResponse(friend_id, "Failed to remove signer.");
                return;
            }
        }

        std::string proof, error;
        std::string reply = std::string(action == "add" ? "Signer added: " : "Signer removed: ")
                            + signer_user_id + "\n\n" + describeSigners();
        if (signSignerList(proof, error)) {
            reply += "\nsignersProof: " + proof;
        } else {
            reply += "\nsignersProof unavailable: " + error;
        }
        sendCommandResponse(friend_id, reply);
    }


    // ---------------------------------------------------------------------
    // The group's own beagles.eth record (ens-gateway /set, owner = group).
    // Same body and signed bytes as beagle-app's /api/ens-register for a
    // node's own identity: signature.message = JSON of the record, signed
    // XEdDSA by the owner (= this group's Carrier key), hex.
    // ---------------------------------------------------------------------
    namespace {
        std::string ensGateway() {
            const char *env = std::getenv("BEAGLE_ENS_GATEWAY");
            std::string url = (env && *env) ? env : "https://ens-gateway.beaglechat.workers.dev";
            while (!url.empty() && url.back() == '/') url.pop_back();
            return url;
        }

        // curl, exec'd without a shell; the body goes over stdin. Returns false
        // only when curl itself could not run; HTTP status is in `status`.
        bool httpJson(const std::string &method, const std::string &url, const std::string &body,
                      long &status, std::string &out) {
            int in_pipe[2], out_pipe[2];
            if (pipe(in_pipe) != 0) return false;
            if (pipe(out_pipe) != 0) { close(in_pipe[0]); close(in_pipe[1]); return false; }
            pid_t pid = fork();
            if (pid < 0) {
                close(in_pipe[0]); close(in_pipe[1]); close(out_pipe[0]); close(out_pipe[1]);
                return false;
            }
            if (pid == 0) {
                dup2(in_pipe[0], 0);
                dup2(out_pipe[1], 1);
                close(in_pipe[0]); close(in_pipe[1]); close(out_pipe[0]); close(out_pipe[1]);
                std::vector<const char *> argv = {"curl", "-sS", "-m", "20", "-X", method.c_str(),
                                                  "-H", "content-type: application/json",
                                                  "-w", "\n%{http_code}"};
                if (method != "GET") {
                    argv.push_back("--data-binary");
                    argv.push_back("@-");
                }
                argv.push_back(url.c_str());
                argv.push_back(nullptr);
                execv("/usr/bin/curl", const_cast<char *const *>(argv.data()));
                _exit(127);
            }
            close(in_pipe[0]);
            close(out_pipe[1]);
            signal(SIGPIPE, SIG_IGN);
            size_t off = 0;
            while (off < body.size()) {
                ssize_t n = write(in_pipe[1], body.data() + off, body.size() - off);
                if (n <= 0) break;
                off += static_cast<size_t>(n);
            }
            close(in_pipe[1]);
            out.clear();
            char buf[4096];
            ssize_t n;
            while ((n = read(out_pipe[0], buf, sizeof(buf))) > 0) {
                out.append(buf, static_cast<size_t>(n));
            }
            close(out_pipe[0]);
            int wstatus = 0;
            waitpid(pid, &wstatus, 0);
            size_t nl = out.rfind('\n');
            if (nl == std::string::npos) return false;
            status = std::strtol(out.c_str() + nl + 1, nullptr, 10);
            out.resize(nl);
            return status > 0;
        }

        std::string joinRest(const std::vector<std::string> &args, size_t from) {
            // args.back() is the sender's userid (appended by Do()).
            std::string text;
            for (size_t i = from; i + 1 < args.size(); i++) {
                if (!text.empty()) text += " ";
                text += args[i];
            }
            return text;
        }

        bool validLabel(std::string &name) {
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            const std::string suffix = ".beagles.eth";
            if (name.size() > suffix.size()
                && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                name.resize(name.size() - suffix.size());
            }
            if (name.empty() || name.size() > 63) return false;
            for (char ch : name) {
                if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')) return false;
            }
            return true;
        }
    }

    bool CarrierRobot::ensFetch(const std::string &full_name, json &record, bool &found,
                                std::string &error) {
        long status = 0;
        std::string out;
        found = false;
        if (!httpJson("GET", ensGateway() + "/get/" + full_name, "", status, out)) {
            error = "gateway unreachable";
            return false;
        }
        if (status == 404) return true;
        if (status != 200) {
            error = "gateway HTTP " + std::to_string(status);
            return false;
        }
        record = json::parse(out, nullptr, false);
        if (record.is_discarded() || !record.is_object()) {
            error = "gateway returned bad JSON";
            return false;
        }
        found = true;
        return true;
    }

    bool CarrierRobot::ensPublish(const json &record, std::string &error) {
        const std::string message = record.dump();
        std::string sig_hex;
        if (!signWithGroupKey(message, sig_hex, error)) {
            return false;
        }
        json payload = record;
        payload["expiration"] = static_cast<long long>(std::time(nullptr)) + 3600;
        payload["signature"] = {{"hash", sig_hex}, {"signerAddress", record["owner"]},
                                {"message", message}};
        long status = 0;
        std::string out;
        if (!httpJson("POST", ensGateway() + "/set", payload.dump(), status, out)) {
            error = "gateway unreachable";
            return false;
        }
        json reply = json::parse(out, nullptr, false);
        if (status < 200 || status >= 300 || reply.is_discarded() || !reply.is_object()
            || !reply.value("success", false)) {
            std::string why = "gateway HTTP " + std::to_string(status);
            if (!reply.is_discarded() && reply.is_object() && reply.contains("error")
                && reply["error"].is_string()) {
                why += ": " + reply["error"].get<std::string>();
            }
            error = why;
            return false;
        }
        return true;
    }

    void CarrierRobot::ensRun(const std::string &friend_id, const std::string &action,
                              const std::string &arg) {
        std::unique_lock<std::mutex> busy(mEnsMutex, std::try_to_lock);
        if (!busy.owns_lock()) {
            sendCommandResponse(friend_id, "Another beagles.eth update is running; try again.");
            return;
        }
        std::string group_user_id, error;
        getUserId(group_user_id);
        char addr_buf[ELA_MAX_ADDRESS_LEN + 1] = {0};
        ela_get_address(mCarrier.get(), addr_buf, sizeof(addr_buf));
        const std::string group_address = addr_buf;
        if (group_user_id.empty() || group_address.empty()) {
            sendCommandResponse(friend_id, "Group identity unavailable.");
            return;
        }

        std::string label = action == "register" ? arg : mDatabaseProxy->getMetaText("ens.name");
        if (label.empty()) {
            sendCommandResponse(friend_id, "This group has no beagles.eth name yet. Creator: /ens register <name>");
            return;
        }
        const std::string full_name = label + ".beagles.eth";

        json existing;
        bool found = false;
        if (!ensFetch(full_name, existing, found, error)) {
            sendCommandResponse(friend_id, "Could not read " + full_name + ": " + error);
            return;
        }

        if (action == "show") {
            if (!found) {
                sendCommandResponse(friend_id, full_name + " is not registered.");
                return;
            }
            json texts = existing.value("texts", json::object());
            auto text = [&](const char *k) {
                return texts.contains(k) && texts[k].is_string() ? texts[k].get<std::string>() : std::string();
            };
            sendCommandResponse(friend_id,
                                full_name + "\nowner: " + existing.value("owner", std::string())
                                + "\ndisplayName: " + text("displayName")
                                + "\ndescription: " + text("description")
                                + "\navatar: " + text("avatar")
                                + "\ncarrierUserId: " + text("carrierUserId")
                                + "\ncarrierAddress: " + text("carrierAddress")
                                + "\nupdatedAt: " + (existing.contains("updatedAt") && existing["updatedAt"].is_string()
                                                    ? existing["updatedAt"].get<std::string>() : std::string()));
            return;
        }

        if (found && existing.value("owner", std::string()) != group_user_id) {
            sendCommandResponse(friend_id, "'" + full_name + "' is already taken by someone else.");
            return;
        }
        if (!found && action != "register") {
            sendCommandResponse(friend_id, full_name + " is not registered. Creator: /ens register <name>");
            return;
        }

        // Carry everything the record already holds (wallet binds, socials) and
        // set only what this command owns.
        json texts = json::object();
        if (found && existing.contains("texts") && existing["texts"].is_object()) {
            for (auto it = existing["texts"].begin(); it != existing["texts"].end(); ++it) {
                if (it.value().is_string()) texts[it.key()] = it.value();
            }
        }
        // A self-owned record is a personal record at the gateway; a leftover
        // signer proof would turn it into a group record the owner is not listed in.
        texts.erase("signers");
        texts.erase("signersProof");
        auto nick = getGroupNickName();
        if (action == "register" || !texts.contains("displayName")) {
            texts["displayName"] = (nick.get() != nullptr && !nick->empty()) ? *nick : label;
        }
        texts["carrierUserId"] = group_user_id;
        texts["carrierAddress"] = group_address;
        if (action == "avatar") texts["avatar"] = arg;
        if (action == "intro") texts["description"] = arg;
        if (!texts.contains("description")) texts["description"] = "";

        json record = {
                {"name", full_name},
                {"owner", group_user_id},
                {"addresses", found && existing.contains("addresses") && existing["addresses"].is_object()
                              ? existing["addresses"] : json::object()},
                {"texts", texts},
                {"referee", found && existing.contains("referee") && existing["referee"].is_string()
                            ? existing["referee"] : json("")},
                {"nft", found && existing.contains("nft") && existing["nft"].is_string()
                        ? existing["nft"] : json("")},
                {"nftid", found && existing.contains("nftid") && existing["nftid"].is_number_integer()
                          ? existing["nftid"] : json(0)},
        };
        if (!ensPublish(record, error)) {
            sendCommandResponse(friend_id, "beagles.eth update failed: " + error);
            return;
        }
        if (action == "register") {
            mDatabaseProxy->setMetaText("ens.name", label);
        }
        Log::I(Log::TAG, "beagles.eth %s: %s", action.c_str(), full_name.c_str());
        sendCommandResponse(friend_id,
                            (action == "register" ? "Registered " : "Updated ") + full_name
                            + " (owner = this group, signed by the group key).\nhttps://app.beagle.chat/"
                            + label);
    }

    // /ens register <name> | /ens show   (register: creator only)
    void CarrierRobot::ensCmd(const std::vector<std::string> &args) {
        if (args.size() < 2) return;
        const std::string friend_id = args.back();
        const std::string action = args.size() >= 3 ? args[1] : "";
        if (action == "show") {
            std::thread(&CarrierRobot::ensRun, this, friend_id, std::string("show"), std::string()).detach();
            return;
        }
        if (action != "register" || args.size() != 4) {
            sendCommandResponse(friend_id, "Usage: /ens register <name> | /ens show");
            return;
        }
        if (!isGroupCreator(friend_id)) {
            sendCommandResponse(friend_id, "Only group creator can register the group's beagles.eth name.");
            return;
        }
        std::string label = args[2];
        if (!validLabel(label)) {
            sendCommandResponse(friend_id, "Name must be 1-63 chars of a-z, 0-9, hyphen.");
            return;
        }
        sendCommandResponse(friend_id, "Registering " + label + ".beagles.eth ...");
        std::thread(&CarrierRobot::ensRun, this, friend_id, std::string("register"), label).detach();
    }

    // /profile avatar <https-url> | /profile intro <text> | /profile show
    void CarrierRobot::profileCmd(const std::vector<std::string> &args) {
        if (args.size() < 2) return;
        const std::string friend_id = args.back();
        const std::string action = args.size() >= 3 ? args[1] : "";
        const std::string usage = "Usage: /profile avatar <https-url> | /profile intro <text> | /profile show";
        if (action == "show") {
            std::thread(&CarrierRobot::ensRun, this, friend_id, std::string("show"), std::string()).detach();
            return;
        }
        if (action != "avatar" && action != "intro") {
            sendCommandResponse(friend_id, usage);
            return;
        }
        if (!isGroupCreator(friend_id)) {
            sendCommandResponse(friend_id, "Only group creator can change the group's profile.");
            return;
        }
        const std::string value = joinRest(args, 2);
        if (action == "avatar") {
            if (args.size() != 4 || value.rfind("https://", 0) != 0 || value.size() > 512) {
                sendCommandResponse(friend_id, "Usage: /profile avatar <https-url> (one https:// URL, max 512 chars)");
                return;
            }
        } else if (value.empty() || value.size() > 200) {
            sendCommandResponse(friend_id, "Usage: /profile intro <text> (1-200 chars)");
            return;
        }
        std::thread(&CarrierRobot::ensRun, this, friend_id, action, value).detach();
    }


    // ---------------------------------------------------------------------
    // Anti-flood (feedback#21). Rules and defaults: Tools/FloodGuard.h.
    // Here: episode counting plus a 1-minute hold after a rate trip.
    // ---------------------------------------------------------------------
    namespace {
        bool isCarrierUserId(const std::string &s) {
            static const std::string kBase58 =
                    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
            if (s.size() < 43 || s.size() > 45) return false;
            for (char ch : s) {
                if (kBase58.find(ch) == std::string::npos) return false;
            }
            return true;
        }
    }

    void CarrierRobot::seedFloodGuard() {
        FloodConfig config;
        config.trip_hold_ms = 10LL * 60 * 1000;   // held until 10 min without sending
        std::unique_ptr<FloodGuard> guard(new FloodGuard(config));
        const int64_t now_ms = static_cast<int64_t>(getTimeStamp() / 1000);
        const int64_t month_ms = config.windows.back().ms;
        size_t n = 0, mutes = 0;
        mDatabaseProxy->forEachMessageSince(
                static_cast<std::time_t>((now_ms - month_ms) * 1000),
                [&](const std::string &friend_id, const std::string &msg, std::time_t ts_us) {
                    bool is_text = parseFileMessage(msg, nullptr, nullptr) == FileMessageKind::NotFile;
                    guard->seedForwarded(friend_id, msg, is_text, static_cast<int64_t>(ts_us / 1000), now_ms);
                    n++;
                });
        mDatabaseProxy->forEachFloodMute(now_ms - config.mute_history_ms,
                                         [&](const std::string &friend_id, int64_t at, int64_t until) {
                                             guard->seedMute(friend_id, at, until);
                                             mutes++;
                                         });
        Log::I(TAG, "flood guard seeded: %zu messages (30 days), %zu mutes (7 days)", n, mutes);
        MUTEX_LOCKER lock(mFloodMutex);
        mFloodGuard = std::move(guard);
    }

    bool CarrierRobot::floodAllows(const std::string &friend_id, const std::string &text, bool is_text) {
        FloodDecision d;
        {
            MUTEX_LOCKER lock(mFloodMutex);
            if (!mFloodGuard) return true;
            const bool never_block = isGroupCreator(friend_id) || mDatabaseProxy->hasSigner(friend_id);
            d = mFloodGuard->check(friend_id, text, is_text,
                                   static_cast<int64_t>(getTimeStamp() / 1000), never_block);
        }
        if (d.forward()) return true;
        Log::I(TAG, "flood drop from=%s len=%zu reason=%s", friend_id.c_str(), text.size(),
               d.reason.c_str());
        if (d.muted_now) {
            const int64_t now_ms = static_cast<int64_t>(getTimeStamp() / 1000);
            mDatabaseProxy->addFloodMute(friend_id, now_ms, d.muted_until);
            Log::I(TAG, "flood mute from=%s until_ms=%lld", friend_id.c_str(),
                   static_cast<long long>(d.muted_until));
        }
        if (!d.notice.empty()) {
            sendCommandResponse(friend_id, d.notice);
        }
        if (d.block_now) {
            Log::I(TAG, "flood block from=%s (muted 3 times in 7 days)", friend_id.c_str());
            blockMemberByFriendId(friend_id);
        }
        return false;
    }

    void CarrierRobot::blockMemberByFriendId(const std::string &friend_id) {
        std::shared_ptr<MemberInfo> info = mDatabaseProxy->getMemberInfo(std::make_shared<std::string>(friend_id));
        if (info.get() == nullptr) {
            Log::I(TAG, "flood block: %s is not a member", friend_id.c_str());
            return;
        }
        mDatabaseProxy->addBlockMember(info);
        mDatabaseProxy->removeMember(friend_id);
        if (ela_remove_friend(mCarrier.get(), friend_id.c_str()) != 0) {
            Log::I(TAG, "flood block: remove friend %s failed (0x%x)", friend_id.c_str(), ela_get_error());
        }
        json msg;
        msg["cmd"] = Command_UpdateMemberCount;
        msg["friendid"] = mAddress.c_str();
        msg["membercount"] = getFriendList()->size();
        sendMsgForManager(dumpJsonForTransport(msg));
    }

    // ---- Join approval (Weili 2026-11-24: "anyone, not for this group, need approval by
    // the group admin"). A friend request from a non-member is held in join_request_table
    // and the creator decides with /approve <n> or /reject <n>. Accepted directly, as before:
    // the first member of an empty group (the creator), existing members, registered agents.
    // Blocked users stay refused. Express-parked requests arrive through the same callback.
    void CarrierRobot::onJoinRequest(const std::string &friendid, const std::string &name,
                                     const std::string &hello) {
        auto fid = std::make_shared<std::string>(friendid);
        if (mDatabaseProxy->getBlockMemberInfo(fid).get() != nullptr) {
            Log::I(TAG, "join refused (blocked) from=%s", friendid.c_str());
            return;
        }
        if (!joinNeedsApproval()) {
            Log::I(TAG, "join accepted (open group) from=%s", friendid.c_str());
            acceptFriend(fid);
            return;
        }
        if (mDatabaseProxy->getMemberInfo(fid).get() != nullptr) {
            Log::I(TAG, "join accepted (already a member) from=%s", friendid.c_str());
            acceptFriend(fid);
            return;
        }
        if (mDatabaseProxy->hasAgent(friendid)) {
            Log::I(TAG, "join accepted (registered agent) from=%s", friendid.c_str());
            acceptFriend(fid);
            return;
        }
        if (getFriendList()->empty()) {
            // The first member becomes the creator. Remember who, so a second stranger
            // arriving before the first one connects is not let in too.
            const std::string first = mDatabaseProxy->getMetaText("join.first_member");
            if (first.empty() || first == friendid) {
                mDatabaseProxy->setMetaText("join.first_member", friendid);
                Log::I(TAG, "join accepted (first member, becomes creator) from=%s", friendid.c_str());
                acceptFriend(fid);
                return;
            }
        }
        const bool known = [&]() {
            for (const auto &r : mDatabaseProxy->listJoinRequests()) if (r.friendid == friendid) return true;
            return false;
        }();
        if (!known && mDatabaseProxy->countJoinRequests(false) >= kJoinPendingMax) {
            Log::I(TAG, "join dropped (%zu already waiting) from=%s", kJoinPendingMax, friendid.c_str());
            return;
        }
        int64_t id = 0;
        bool is_new = false, ignored = false;
        const int64_t now_ms = static_cast<int64_t>(getTimeStamp() / 1000);
        if (!mDatabaseProxy->upsertJoinRequest(friendid, joinStoredText(name), joinStoredText(hello), now_ms,
                                               kJoinRejectQuietMs, id, is_new, ignored)) {
            Log::I(TAG, "join request store failed from=%s", friendid.c_str());
            return;
        }
        if (ignored) {
            Log::I(TAG, "join ignored (rejected within 7 days) from=%s", friendid.c_str());
            return;
        }
        Log::I(TAG, "join held #%lld from=%s name_len=%zu hello_len=%zu new=%d", static_cast<long long>(id),
               friendid.c_str(), name.size(), hello.size(), is_new ? 1 : 0);
        if (!is_new || mCreaterFriendId.get() == nullptr) return;   // toxcore re-sends requests: tell once
        JoinRequest r;
        if (!mDatabaseProxy->getJoinRequest(id, r)) return;
        sendCommandResponse(*mCreaterFriendId, joinRequestNotice(r));
        std::shared_ptr<MemberInfo> creator = mDatabaseProxy->getMemberInfo(mCreaterFriendId);
        bool online = false;
        if (creator.get() != nullptr) {
            creator->Lock();
            online = creator->mStatus == 0;
            creator->UnLock();
        }
        if (online) mDatabaseProxy->markJoinRequestsNotified();
    }

    bool CarrierRobot::joinNeedsApproval() {
        return joinModeIsApprove(mDatabaseProxy->getMetaText("join.mode"));
    }

    // Requests held while the group needed approval are let in once it is open.
    size_t CarrierRobot::admitPendingJoins() {
        size_t n = 0;
        for (const auto &r : mDatabaseProxy->listJoinRequests()) {
            auto fid = std::make_shared<std::string>(r.friendid);
            if (mDatabaseProxy->getBlockMemberInfo(fid).get() == nullptr) {
                mDatabaseProxy->deleteRemovedListMember(r.friendid);
                if (ela_accept_friend(mCarrier.get(), r.friendid.c_str()) != 0
                    && ela_get_error() != ELA_GENERAL_ERROR(ELAERR_ALREADY_EXIST)) {
                    Log::I(TAG, "join admit #%lld failed (0x%x), kept", static_cast<long long>(r.id), ela_get_error());
                    continue;
                }
                Log::I(TAG, "join admitted #%lld from=%s (group is open)", static_cast<long long>(r.id), r.friendid.c_str());
                n++;
            }
            mDatabaseProxy->deleteJoinRequest(r.id);
        }
        return n;
    }

    // /join [open|approve]   creator: show or set who may join.
    void CarrierRobot::joinModeCmd(const std::vector<std::string> &args) {
        if (args.size() < 2) return;
        const std::string &sender = args.back();
        const std::string want = joinModeArg(args);
        if (want.empty()) {
            sendCommandResponse(sender, joinModeText(joinNeedsApproval()));
            return;
        }
        if (want == "?") {
            sendCommandResponse(sender, "Usage: /join open | /join approve | /join");
            return;
        }
        mDatabaseProxy->setMetaText("join.mode", want);
        Log::I(TAG, "join mode set to %s by creator", want.c_str());
        std::string reply = joinModeText(want == "approve");
        if (want == "open") {
            const size_t n = admitPendingJoins();
            if (n) reply += " Let in " + std::to_string(n) + " waiting request" + (n == 1 ? "." : "s.");
        }
        sendCommandResponse(sender, reply);
    }

    // The creator just came online: one line if requests arrived while they were away.
    void CarrierRobot::tellCreatorPendingJoins() {
        const size_t unnotified = mDatabaseProxy->countJoinRequests(true);
        if (unnotified == 0 || mCreaterFriendId.get() == nullptr) return;
        sendCommandResponse(*mCreaterFriendId, joinPendingSummary(mDatabaseProxy->countJoinRequests(false)));
        mDatabaseProxy->markJoinRequestsNotified();
    }

    void CarrierRobot::pendingCmd(const std::vector<std::string> &args) {
        if (args.size() < 2) return;
        sendCommandResponse(args.back(), formatPendingPage(mDatabaseProxy->listJoinRequests(),
                                                           memberListPageArg(args)));
    }

    void CarrierRobot::approveCmd(const std::vector<std::string> &args) {
        if (args.size() < 2) return;
        const std::string &sender = args.back();
        const int64_t n = joinRequestNumberArg(args);
        JoinRequest r;
        if (n < 0 || !mDatabaseProxy->getJoinRequest(n, r)) {
            sendCommandResponse(sender, n < 0 ? "Usage: /approve <n>. Type /pending."
                                              : "No join request #" + std::to_string(n) + ". Type /pending.");
            return;
        }
        auto fid = std::make_shared<std::string>(r.friendid);
        if (mDatabaseProxy->getBlockMemberInfo(fid).get() != nullptr) {
            mDatabaseProxy->deleteJoinRequest(n);
            sendCommandResponse(sender, "#" + std::to_string(n) + " is blocked. Type /unblock <userid> first.");
            return;
        }
        mDatabaseProxy->deleteRemovedListMember(r.friendid);
        int rc = ela_accept_friend(mCarrier.get(), r.friendid.c_str());
        if (rc != 0 && ela_get_error() != ELA_GENERAL_ERROR(ELAERR_ALREADY_EXIST)) {
            Log::I(TAG, "join approve #%lld accept failed (0x%x)", static_cast<long long>(n), ela_get_error());
            sendCommandResponse(sender, "Could not approve #" + std::to_string(n) + " right now. Try again.");
            return;
        }
        mDatabaseProxy->deleteJoinRequest(n);
        Log::I(TAG, "join approved #%lld from=%s by creator", static_cast<long long>(n), r.friendid.c_str());
        sendCommandResponse(sender, "Approved #" + std::to_string(n) + " (" + memberListName(r.name, kJoinNameBytes) +
                                    "). They join when they next come online.");
    }

    void CarrierRobot::rejectCmd(const std::vector<std::string> &args) {
        if (args.size() < 2) return;
        const std::string &sender = args.back();
        const int64_t n = joinRequestNumberArg(args);
        JoinRequest r;
        if (n < 0 || !mDatabaseProxy->getJoinRequest(n, r)) {
            sendCommandResponse(sender, n < 0 ? "Usage: /reject <n>. Type /pending."
                                              : "No join request #" + std::to_string(n) + ". Type /pending.");
            return;
        }
        mDatabaseProxy->rejectJoinRequest(n, static_cast<int64_t>(getTimeStamp() / 1000));
        Log::I(TAG, "join rejected #%lld from=%s by creator", static_cast<long long>(n), r.friendid.c_str());
        sendCommandResponse(sender, "Rejected #" + std::to_string(n) + " (" + memberListName(r.name, kJoinNameBytes) +
                                    "). Their requests are ignored for 7 days.");
    }

    // /unblock <userid>   creator: lift a block (manual or automatic) and its mutes.
    void CarrierRobot::unblockCmd(const std::vector<std::string> &args) {
        if (args.size() < 2) return;
        const std::string friend_id = args.back();
        if (!isGroupCreator(friend_id)) {
            sendCommandResponse(friend_id, "Only group creator can unblock.");
            return;
        }
        if (args.size() != 3 || !isCarrierUserId(args[1])) {
            sendCommandResponse(friend_id, "Usage: /unblock <userid>");
            return;
        }
        const std::string user_id = args[1];
        const bool was_blocked = mDatabaseProxy->removeBlockMember(user_id);
        mDatabaseProxy->clearFloodMutes(user_id);
        {
            MUTEX_LOCKER lock(mFloodMutex);
            if (mFloodGuard) mFloodGuard->clear(user_id);
        }
        Log::I(TAG, "flood unblock %s by creator (was blocked: %d)", user_id.c_str(), was_blocked ? 1 : 0);
        sendCommandResponse(friend_id, was_blocked
                                       ? "Unblocked " + user_id + ". They can join the group again."
                                       : user_id + " was not blocked; any mutes are cleared.");
    }

    // /unmute <userid>   creator: end a flood mute now and forget the member's mutes.
    void CarrierRobot::unmuteCmd(const std::vector<std::string> &args) {
        if (args.size() < 2) return;
        const std::string friend_id = args.back();
        if (!isGroupCreator(friend_id)) {
            sendCommandResponse(friend_id, "Only group creator can unmute.");
            return;
        }
        if (args.size() != 3 || !isCarrierUserId(args[1])) {
            sendCommandResponse(friend_id, "Usage: /unmute <userid>");
            return;
        }
        const std::string user_id = args[1];
        mDatabaseProxy->clearFloodMutes(user_id);
        {
            MUTEX_LOCKER lock(mFloodMutex);
            if (mFloodGuard) mFloodGuard->clear(user_id);
        }
        Log::I(TAG, "flood unmute %s by creator", user_id.c_str());
        sendCommandResponse(friend_id, "Unmuted " + user_id + ".");
    }

}
