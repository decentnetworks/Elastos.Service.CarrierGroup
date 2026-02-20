//
// Created by luocf on 2019/6/12.
//

#include "DatabaseProxy.h"
#include <map>
#include <iostream>
#include <../../common/Log.hpp>

namespace chatrobot {
    DatabaseProxy::DatabaseProxy() {
        mMessageList = std::make_shared<std::vector<std::shared_ptr<MessageInfo>>>();
        mMemberList = std::make_shared<std::vector<std::shared_ptr<MemberInfo>>>();
        mBlockMemberList = std::make_shared<std::vector<std::shared_ptr<MemberInfo>>>();
        mRemovedMemberList = std::make_shared<std::vector<std::shared_ptr<MemberInfo>>>();
        mAgentMap = std::make_shared<std::map<std::string, std::string>>();
    }

    DatabaseProxy::~DatabaseProxy() {
        mMemberList.reset();
        mMessageList.reset();
        mBlockMemberList.reset();
        mRemovedMemberList.reset();
        mAgentMap.reset();
    }

    void DatabaseProxy::updateMemberInfo(std::shared_ptr<std::string> friendid,
                                         std::shared_ptr<std::string> nickname,
                                         int status,
                                         std::time_t time_stamp) {
        std::shared_ptr<MemberInfo> member_info = this->getMemberInfo(friendid);
        MUTEX_LOCKER locker_sync_data(_SyncedMemberList);
        std::shared_ptr<std::string> tmp_nickname = nickname;
        std::string t_strSql = "";
        char *errMsg = NULL;
        if (member_info.get() != nullptr) {
            member_info->Lock();
            if (!tmp_nickname->empty()) {
                member_info->mNickName = tmp_nickname;
            } else {
                tmp_nickname = member_info->mNickName;
            }
            if (time_stamp > member_info->mMsgTimeStamp) {
                member_info->mMsgTimeStamp = time_stamp;
            }
            member_info->mStatus = status;
            t_strSql = "update member_table set NickName='"+*tmp_nickname.get()+"'";
            t_strSql += ",Status="+std::to_string(member_info->mStatus);
            t_strSql += ",MsgTimeStamp="+std::to_string(member_info->mMsgTimeStamp);
            t_strSql += " where Friendid='"+*member_info->mFriendid.get()+"';";
            member_info->UnLock();

        } else {
            member_info = std::make_shared<chatrobot::MemberInfo>(
                    friendid, nickname, status, time_stamp);
            mMemberList->push_back(member_info);
            t_strSql = "insert into member_table values(NULL,'"+*member_info->mFriendid.get()+"'";
            t_strSql += ",'"+*member_info->mNickName.get()+"'";
            t_strSql += ","+std::to_string(member_info->mStatus);
            t_strSql += ",0);";
        }
        //消息直接入库
        int rv = sqlite3_exec(mDb, t_strSql.c_str(), callback, this, &errMsg);
        if (rv != SQLITE_OK) {
            Log::I(DatabaseProxy::TAG, "SQLite update updateMemberInfo error: %s\n",
                   errMsg);
        }
    }

    std::shared_ptr<MemberInfo>
    DatabaseProxy::getMemberInfo(std::shared_ptr<std::string> friendid) {
        MUTEX_LOCKER locker_sync_data(_SyncedMemberList);
        for (int i = 0; i < mMemberList->size(); i++) {
            std::shared_ptr<MemberInfo> member_info = mMemberList->at(i);
            if (member_info.get() == nullptr) {
                continue;
            }
            member_info->Lock();
            if (member_info->mFriendid->compare(*friendid.get()) == 0) {
                member_info->UnLock();
                return member_info;
            }
            member_info->UnLock();
        }
        return std::shared_ptr<MemberInfo>(nullptr);
    }

    std::shared_ptr<MemberInfo> DatabaseProxy::getMemberInfo(int index) {
        MUTEX_LOCKER locker_sync_data(_SyncedMemberList);
        for (int i = 0; i < mMemberList->size(); i++) {
            std::shared_ptr<MemberInfo> member_info = mMemberList->at(i);
            if (member_info.get() == nullptr) {
                continue;
            }
            member_info->Lock();
            if (member_info->mIndex == index) {
                member_info->UnLock();
                return member_info;
            }
            member_info->UnLock();
        }
        return std::shared_ptr<MemberInfo>(nullptr);
    }
    bool DatabaseProxy::deleteRemovedListMember(std::string friendid) {
        MUTEX_LOCKER locker_sync_data(_SyncedMemberList);
        for (int i = 0; i < mRemovedMemberList->size(); i++) {
            std::shared_ptr<MemberInfo> member_info = mRemovedMemberList->at(i);
            if (member_info.get() == nullptr) {
                continue;
            }
            member_info->Lock();
            if(member_info->mFriendid->compare(friendid) == 0) {
                mRemovedMemberList->push_back(member_info);
                mRemovedMemberList->erase(std::begin(*mRemovedMemberList.get()) + i);
                member_info->UnLock();
                return true;
            }
            member_info->UnLock();
        }
        return false;
    }

    std::shared_ptr<MemberInfo>
    DatabaseProxy::getBlockMemberInfo(std::shared_ptr<std::string> friendid) {
        MUTEX_LOCKER locker_sync_data(_SyncedBlockMemberList);
        for (int i = 0; i < mBlockMemberList->size(); i++) {
            std::shared_ptr<MemberInfo> member_info = mBlockMemberList->at(i);
            if (member_info.get() == nullptr) {
                continue;
            }
            member_info->Lock();
            if (member_info->mFriendid->compare(*friendid.get()) == 0) {
                member_info->UnLock();
                return member_info;
            }
            member_info->UnLock();
        }
        return std::shared_ptr<MemberInfo>(nullptr);
    }
    std::shared_ptr<MemberInfo> DatabaseProxy::getBlockMemberInfo(int index) {
        MUTEX_LOCKER locker_sync_data(_SyncedBlockMemberList);
        for (int i = 0; i < mBlockMemberList->size(); i++) {
            std::shared_ptr<MemberInfo> member_info = mBlockMemberList->at(i);
            if (member_info.get() == nullptr) {
                continue;
            }
            member_info->Lock();
            if (member_info->mIndex == index) {
                member_info->UnLock();
                return member_info;
            }
            member_info->UnLock();
        }
        return std::shared_ptr<MemberInfo>(nullptr);
    }
    std::shared_ptr<std::string> DatabaseProxy::getGroupNickName() {
        return mNickName;
    }

    void DatabaseProxy::updateGroupNickName(std::shared_ptr<std::string> nick_name) {
        MUTEX_LOCKER locker_sync_data(_SyncedGroupInfo);
        std::string t_strSql = "";
        char *errMsg = NULL;
        if (mNickName.get() == nullptr) {
            t_strSql = "insert into group_info_table values(NULL,'"+*nick_name.get()+"');";
        } else {
            t_strSql = "update group_info_table set NickName='"+*nick_name.get()+"' where id=1;";
        }

        //消息直接入库
        int rv = sqlite3_exec(mDb, t_strSql.c_str(), callback, this, &errMsg);
        if (rv != SQLITE_OK) {
            Log::I(TAG, "SQLite update updateGroupNickName error: %s\n",
                   errMsg);
        }
        mNickName = nick_name;
    }

    void DatabaseProxy::addMessgae(std::shared_ptr<std::string> friend_id,
                                   std::string message, std::time_t send_time) {
        MUTEX_LOCKER locker_sync_data(_SyncedMessageList);
        char *errMsg = NULL;
        std::string t_strSql;
        t_strSql = "insert into message_table values(NULL,'"+*friend_id.get()+"'";
        t_strSql += ",'"+message+"'";
        t_strSql += ","+std::to_string(send_time)+"";
        t_strSql += ");";

                //消息直接入库
        int rv = sqlite3_exec(mDb, t_strSql.c_str(), callback, this, &errMsg);
        if (rv != SQLITE_OK) {
            Log::I(DatabaseProxy::TAG, "SQLite addMessgae error: %s\n",
                   errMsg);
        }
    }
    std::shared_ptr<std::vector<std::shared_ptr<MessageInfo>>>
    DatabaseProxy::getMessages(std::shared_ptr<std::string> friend_id, std::time_t send_time, int max_limit) {
        MUTEX_LOCKER locker_sync_data(_SyncedMessageList);
        //查询一条记录
        char **azResult;   //二维数组存放结果
        char *errMsg = NULL;
        int nrow;          /* Number of result rows written here */
        int ncolumn;
        std::string t_strSql;
        t_strSql = "select * from message_table where FriendId!='"+*friend_id.get()+"'";
        t_strSql += " and SendTimeStamp>"+std::to_string(send_time);
        t_strSql += " order by SendTimeStamp desc limit 0,"+std::to_string(max_limit)+";";
        /*step 2: sql语句对象。*/
        sqlite3_stmt *pStmt;
        int rc = sqlite3_prepare_v2(
                mDb, //数据库连接对象
                t_strSql.c_str(), //指向原始sql语句字符串
                strlen(t_strSql.c_str()), //
                &pStmt,
                NULL
        );
        if (rc != SQLITE_OK) {
            Log::I(TAG, "sqlite3_prepare_v2 error:");
            return std::shared_ptr<std::vector<std::shared_ptr<MessageInfo>>>(nullptr);
        }

        rc = sqlite3_get_table(mDb, t_strSql.c_str(), &azResult, &nrow, &ncolumn, &errMsg);
        if (rc == SQLITE_OK) {
            Log::I(TAG, "getMessages, successful sql:%s", t_strSql.c_str());
        } else {
            Log::I(TAG, "getMessages, Can't get table: %s", sqlite3_errmsg(mDb));
            return std::shared_ptr<std::vector<std::shared_ptr<MessageInfo>>>(nullptr);
        }

        std::shared_ptr<std::vector<std::shared_ptr<MessageInfo>>> messages_list = std::make_shared<std::vector<std::shared_ptr<MessageInfo>>>();
        if (nrow != 0 && ncolumn != 0) {     //有查询结果,不包含表头所占行数
            for (int i = nrow; i >=1; i--) {        // 第0行为数据表头
                std::shared_ptr<MessageInfo> message_info = std::make_shared<MessageInfo>();
                message_info->mFriendid = std::make_shared<std::string>(azResult[4*i + 1]);
                message_info->mMsg = std::make_shared<std::string>(azResult[4*i + 2]);
                message_info->mSendTimeStamp = atol(azResult[4*i + 3]);
                messages_list->push_back(message_info);
            }
        }

        sqlite3_free_table(azResult);
        sqlite3_finalize(pStmt);     //销毁一个SQL语句对象
        return messages_list;
    }

    std::shared_ptr<std::vector<std::shared_ptr<MemberInfo>>> DatabaseProxy::getFriendList() {
        MUTEX_LOCKER locker_sync_data(_SyncedMemberList);
        return mMemberList;
    }
    bool DatabaseProxy::inRemovedList(std::string friendid) {
        MUTEX_LOCKER locker_sync_data(_SyncedMemberList);
        for (int i = 0; i < mRemovedMemberList->size(); i++) {
            std::shared_ptr<MemberInfo> member_info = mRemovedMemberList->at(i);
            if (member_info.get() == nullptr) {
                continue;
            }
            member_info->Lock();
            if(member_info->mFriendid->compare(friendid) == 0) {
                member_info->UnLock();
                return true;
            }
            member_info->UnLock();
        }

        Log::I(TAG, "inRemovedList not exist, friendid:%s", friendid.c_str());
        return false;
    }
    bool DatabaseProxy::removeMember(std::string friendid) {
        MUTEX_LOCKER locker_sync_data(_SyncedMemberList);
        for (int i = 0; i < mMemberList->size(); i++) {
            std::shared_ptr<MemberInfo> member_info = mMemberList->at(i);
            if (member_info.get() == nullptr) {
                continue;
            }
            member_info->Lock();
            if(member_info->mFriendid->compare(friendid) == 0) {
                mRemovedMemberList->push_back(member_info);
                mMemberList->erase(std::begin(*mMemberList.get()) + i);
                char *errMsg = NULL;
                std::string t_strSql;
                t_strSql = "delete from member_table where FriendId='" + friendid + "';";
                //消息直接入库
                int rv = sqlite3_exec(mDb, t_strSql.c_str(), callback, this, &errMsg);
                if (rv != SQLITE_OK) {
                    Log::I(DatabaseProxy::TAG, "SQLite removeMember error: %s\n",
                           errMsg);
                }
                member_info->UnLock();
                return true;
            }
            member_info->UnLock();
        }
        Log::I(TAG, "removeMember not exist, friendid:%s", friendid.c_str());
        return false;
    }

    void DatabaseProxy::addBlockMember(std::shared_ptr<MemberInfo> memberinfo){
        MUTEX_LOCKER locker_sync_data(_SyncedBlockMemberList);
        std::string t_strSql = "";
        char *errMsg = NULL;
        mBlockMemberList->push_back(memberinfo);
        t_strSql = "insert into block_member_table values(NULL,'"+*memberinfo->mFriendid.get()+"');";

        //消息直接入库
        int rv = sqlite3_exec(mDb, t_strSql.c_str(), callback, this, &errMsg);
        if (rv != SQLITE_OK) {
            Log::I(DatabaseProxy::TAG, "SQLite update addBlockMember error: %s\n",
                   errMsg);
        }
    }

    bool DatabaseProxy::removeBlockMember(std::string friendid) {
        MUTEX_LOCKER locker_sync_data(_SyncedBlockMemberList);
        for (int i = 0; i < mBlockMemberList->size(); i++) {
            std::shared_ptr<MemberInfo> member_info = mBlockMemberList->at(i);
            if (member_info.get() == nullptr) {
                continue;
            }
            member_info->Lock();
            if(member_info->mFriendid->compare(friendid) == 0) {
                mBlockMemberList->erase(std::begin(*mBlockMemberList.get()) + i);
                char *errMsg = NULL;
                std::string t_strSql;
                t_strSql = "delete from block_member_table where FriendId='" + friendid + "';";
                //消息直接入库
                int rv = sqlite3_exec(mDb, t_strSql.c_str(), callback, this, &errMsg);
                if (rv != SQLITE_OK) {
                    Log::I(DatabaseProxy::TAG, "SQLite removeBlockMember error: %s\n",
                           errMsg);
                }
                member_info->UnLock();
                return true;
            }
            member_info->UnLock();
        }
        Log::I(TAG, "removeBlockMember not exist, friendid:%s", friendid.c_str());
        return false;
    }

    bool DatabaseProxy::addAgent(const std::string &user_id, const std::string &address) {
        MUTEX_LOCKER locker_sync_data(_SyncedAgentList);
        if (user_id.empty() || address.empty()) {
            return false;
        }
        std::string t_strSql = "";
        char *errMsg = NULL;
        t_strSql = "INSERT OR REPLACE INTO agent_table(UserId, Address) VALUES('"
                   + user_id + "','" + address + "');";
        int rv = sqlite3_exec(mDb, t_strSql.c_str(), callback, this, &errMsg);
        if (rv != SQLITE_OK) {
            Log::I(DatabaseProxy::TAG, "SQLite addAgent error: %s\n", errMsg);
            return false;
        }
        (*mAgentMap.get())[user_id] = address;
        return true;
    }

    bool DatabaseProxy::removeAgent(const std::string &user_id) {
        MUTEX_LOCKER locker_sync_data(_SyncedAgentList);
        if (user_id.empty()) {
            return false;
        }
        std::string t_strSql = "";
        char *errMsg = NULL;
        t_strSql = "delete from agent_table where UserId='" + user_id + "';";
        int rv = sqlite3_exec(mDb, t_strSql.c_str(), callback, this, &errMsg);
        if (rv != SQLITE_OK) {
            Log::I(DatabaseProxy::TAG, "SQLite removeAgent error: %s\n", errMsg);
            return false;
        }
        mAgentMap->erase(user_id);
        return true;
    }

    bool DatabaseProxy::hasAgent(const std::string &user_id) {
        MUTEX_LOCKER locker_sync_data(_SyncedAgentList);
        return mAgentMap->find(user_id) != mAgentMap->end();
    }

    std::shared_ptr<std::string> DatabaseProxy::getAgentAddress(const std::string &user_id) {
        MUTEX_LOCKER locker_sync_data(_SyncedAgentList);
        auto it = mAgentMap->find(user_id);
        if (it == mAgentMap->end()) {
            return std::shared_ptr<std::string>(nullptr);
        }
        return std::make_shared<std::string>(it->second);
    }

    std::shared_ptr<std::vector<std::shared_ptr<std::string>>> DatabaseProxy::getAgentUserIdList() {
        MUTEX_LOCKER locker_sync_data(_SyncedAgentList);
        auto result = std::make_shared<std::vector<std::shared_ptr<std::string>>>();
        for (const auto &item : *mAgentMap.get()) {
            result->push_back(std::make_shared<std::string>(item.first));
        }
        return result;
    }
    int DatabaseProxy::callback(void *context, int argc, char **argv, char **azColName) {
        auto database_proxy = reinterpret_cast<DatabaseProxy *>(context);
        int i;
        for (i = 0; i < argc; ++i) {
            Log::I(DatabaseProxy::TAG, "database %s = %s\n", azColName[i],
                   argv[i] ? argv[i] : "NULL");
        }
        return 0;
    }

    bool DatabaseProxy::closeDb() {
        if (mDb != nullptr) {
            sqlite3_close(mDb);
        }
        return true;
    }

    void DatabaseProxy::syncGroupInfo() {
        //查询一条记录
        char **azResult;   //二维数组存放结果
        char *errMsg = NULL;
        int nrow;          /* Number of result rows written here */
        int ncolumn;
        std::string t_strSql;
        t_strSql = "select * from group_info_table where id=1;";
        /*step 2: sql语句对象。*/
        sqlite3_stmt *pStmt;
        int rc = sqlite3_prepare_v2(
                mDb, //数据库连接对象
                t_strSql.c_str(), //指向原始sql语句字符串
                strlen(t_strSql.c_str()), //
                &pStmt,
                NULL
        );
        if (rc != SQLITE_OK) {
            Log::I(TAG, "sqlite3_prepare_v2 error:%s", sqlite3_errmsg(mDb));
            return;
        }

        rc = sqlite3_get_table(mDb, t_strSql.c_str(), &azResult, &nrow, &ncolumn, &errMsg);
        if (rc == SQLITE_OK) {
            Log::I(TAG, "syncGroupInfo, successful sql:%s", t_strSql.c_str());
        } else {
            Log::I(TAG, "syncGroupInfo, Can't get table: %s", sqlite3_errmsg(mDb));
            return;
        }
        Log::I(TAG, "syncGroupInfo,nrow:%d, ncolumn:%d", nrow, ncolumn);
        if (nrow != 0 && ncolumn != 0) {     //有查询结果,不包含表头所占行数
            for (int i = 1; i <= nrow; i++) {        // 第0行为数据表头
                mNickName =  std::make_shared<std::string>(azResult[2*i + 1]);
                Log::I(TAG, "syncGroupInfo,azResult[2*i + 0]:%s, azResult[2*i + 1]: %s", azResult[2*i + 0],azResult[2*i + 1]);
                break;
            }
        }
        if (mNickName.get() != nullptr) {
            Log::I(TAG, "syncGroupInfo,mNickName: %s", mNickName->c_str());
        }

        sqlite3_free_table(azResult);
        sqlite3_finalize(pStmt);     //销毁一个SQL语句对象
    }

    void DatabaseProxy::syncBlockList() {
        MUTEX_LOCKER locker_sync_data(_SyncedBlockMemberList);
        //查询一条记录
        char **azResult;   //二维数组存放结果
        char *errMsg = NULL;
        int nrow;          /* Number of result rows written here */
        int ncolumn;
        std::string t_strSql;
        t_strSql = "select * from block_member_table order by id asc";
        /*step 2: sql语句对象。*/
        sqlite3_stmt *pStmt;
        int rc = sqlite3_prepare_v2(
                mDb, //数据库连接对象
                t_strSql.c_str(), //指向原始sql语句字符串
                strlen(t_strSql.c_str()), //
                &pStmt,
                NULL
        );
        if (rc != SQLITE_OK) {
            Log::I(TAG, "sqlite3_prepare_v2 error:%s", sqlite3_errmsg(mDb));
            return;
        }

        rc = sqlite3_get_table(mDb, t_strSql.c_str(), &azResult, &nrow, &ncolumn, &errMsg);
        if (rc == SQLITE_OK) {
            Log::I(TAG, "syncMemberList, successful sql:%s", t_strSql.c_str());
        } else {
            Log::I(TAG, "syncMemberList, Can't get table: %s", sqlite3_errmsg(mDb));
            return;
        }

        if (nrow != 0 && ncolumn != 0) {     //有查询结果,不包含表头所占行数
            for (int i = 1; i <= nrow; i++) {        // 第0行为数据表头
                mBlockMemberList->push_back(std::make_shared<MemberInfo>(
                        std::make_shared<std::string>(azResult[2*i + 1]),
                        std::make_shared<std::string>(""),
                        1,//初始化时为offline
                        0));
            }
        }

        sqlite3_free_table(azResult);
        sqlite3_finalize(pStmt);     //销毁一个SQL语句对象
    }

    void DatabaseProxy::syncAgentList() {
        MUTEX_LOCKER locker_sync_data(_SyncedAgentList);
        mAgentMap->clear();
        char **azResult;
        char *errMsg = NULL;
        int nrow;
        int ncolumn;
        std::string t_strSql;
        t_strSql = "select * from agent_table order by id asc";
        sqlite3_stmt *pStmt;
        int rc = sqlite3_prepare_v2(
                mDb,
                t_strSql.c_str(),
                strlen(t_strSql.c_str()),
                &pStmt,
                NULL
        );
        if (rc != SQLITE_OK) {
            Log::I(TAG, "sqlite3_prepare_v2 error:%s", sqlite3_errmsg(mDb));
            return;
        }

        rc = sqlite3_get_table(mDb, t_strSql.c_str(), &azResult, &nrow, &ncolumn, &errMsg);
        if (rc != SQLITE_OK) {
            Log::I(TAG, "syncAgentList, Can't get table: %s", sqlite3_errmsg(mDb));
            sqlite3_finalize(pStmt);
            return;
        }

        if (nrow != 0 && ncolumn != 0) {
            for (int i = 1; i <= nrow; i++) {
                std::string user_id = azResult[3 * i + 1];
                std::string address = azResult[3 * i + 2];
                (*mAgentMap.get())[user_id] = address;
            }
        }

        sqlite3_free_table(azResult);
        sqlite3_finalize(pStmt);
    }
    void DatabaseProxy::syncMemberList() {
        MUTEX_LOCKER locker_sync_data(_SyncedMemberList);
        //查询一条记录
        char **azResult;   //二维数组存放结果
        char *errMsg = NULL;
        int nrow;          /* Number of result rows written here */
        int ncolumn;
        std::string t_strSql;
        t_strSql = "select * from member_table order by id asc";
        /*step 2: sql语句对象。*/
        sqlite3_stmt *pStmt;
        int rc = sqlite3_prepare_v2(
                mDb, //数据库连接对象
                t_strSql.c_str(), //指向原始sql语句字符串
                strlen(t_strSql.c_str()), //
                &pStmt,
                NULL
        );
        if (rc != SQLITE_OK) {
            Log::I(TAG, "sqlite3_prepare_v2 error:%s", sqlite3_errmsg(mDb));
            return;
        }

        rc = sqlite3_get_table(mDb, t_strSql.c_str(), &azResult, &nrow, &ncolumn, &errMsg);
        if (rc == SQLITE_OK) {
            Log::I(TAG, "syncMemberList, successful sql:%s", t_strSql.c_str());
        } else {
            Log::I(TAG, "syncMemberList, Can't get table: %s", sqlite3_errmsg(mDb));
            return;
        }

        if (nrow != 0 && ncolumn != 0) {     //有查询结果,不包含表头所占行数
            for (int i = 1; i <= nrow; i++) {        // 第0行为数据表头
                mMemberList->push_back(std::make_shared<MemberInfo>(
                        std::make_shared<std::string>(azResult[5*i + 1]),
                        std::make_shared<std::string>(azResult[5*i + 2]),
                        1,//初始化时为offline
                        atol(azResult[5*i + 4])));
            }
        }

        sqlite3_free_table(azResult);
        sqlite3_finalize(pStmt);     //销毁一个SQL语句对象
    }

    bool DatabaseProxy::startDb(const char *data_dir) {
        std::string strConn = std::string(data_dir) + "/chatrobot.db";
        char *errMsg = NULL;
        //打开一个数据库，如果改数据库不存在，则创建一个名字为databaseName的数据库文件
        int rv;
        rv = sqlite3_config(SQLITE_CONFIG_MULTITHREAD);
        if (rv != SQLITE_OK && rv != SQLITE_MISUSE) {
            Log::I(DatabaseProxy::TAG, "sqlite3_config error: %d\n", rv);
            return 1;
        }
        rv = sqlite3_open(strConn.c_str(), &mDb);
        if (rv != SQLITE_OK) {
            Log::I(DatabaseProxy::TAG, "Cannot open database: %s\n", sqlite3_errmsg(mDb));
            sqlite3_close(mDb);
            return 1;
        }
        char create_table[256] = "CREATE TABLE IF NOT EXISTS member_table(id INTEGER PRIMARY KEY AUTOINCREMENT,Friendid TEXT NOT NULL, NickName TEXT, Status INTEGER, MsgTimeStamp INTEGER)";
        rv = sqlite3_exec(mDb, create_table, callback, this, &errMsg);
        if (rv != SQLITE_OK) {
            Log::I(DatabaseProxy::TAG, "SQLite create_table statement execution error: %s\n",
                   errMsg);
            return 1;
        }
        char create_message_table[256] = "CREATE TABLE IF NOT EXISTS message_table (id INTEGER PRIMARY KEY AUTOINCREMENT,Friendid TEXT NOT NULL, Msg TEXT, SendTimeStamp INTEGER)";
        rv = sqlite3_exec(mDb, create_message_table, callback, this, &errMsg);
        if (rv != SQLITE_OK) {
            Log::I(DatabaseProxy::TAG,
                   "SQLite create_message_table statement execution error: %s\n", errMsg);
            return 1;
        }
        char create_blackmember_table[256] = "CREATE TABLE IF NOT EXISTS block_member_table (id INTEGER PRIMARY KEY AUTOINCREMENT,Friendid TEXT NOT NULL)";
        rv = sqlite3_exec(mDb, create_blackmember_table, callback, this, &errMsg);
        if (rv != SQLITE_OK) {
            Log::I(DatabaseProxy::TAG,
                   "SQLite create_blackmember_table statement execution error: %s\n", errMsg);
            return 1;
        }
        char create_groupInfo_table[256] = "CREATE TABLE IF NOT EXISTS group_info_table (id INTEGER PRIMARY KEY AUTOINCREMENT,NickName TEXT NOT NULL)";
        rv = sqlite3_exec(mDb, create_groupInfo_table, callback, this, &errMsg);
        if (rv != SQLITE_OK) {
            Log::I(DatabaseProxy::TAG,
                   "SQLite group_info_table statement execution error: %s\n", errMsg);
            return 1;
        }
        char create_agent_table[256] = "CREATE TABLE IF NOT EXISTS agent_table (id INTEGER PRIMARY KEY AUTOINCREMENT,UserId TEXT NOT NULL UNIQUE,Address TEXT NOT NULL)";
        rv = sqlite3_exec(mDb, create_agent_table, callback, this, &errMsg);
        if (rv != SQLITE_OK) {
            Log::I(DatabaseProxy::TAG,
                   "SQLite agent_table statement execution error: %s\n", errMsg);
            return 1;
        }
        //同步Member信息
        syncMemberList();
        syncGroupInfo();
        syncBlockList();
        syncAgentList();
        return 0;
    }
}
