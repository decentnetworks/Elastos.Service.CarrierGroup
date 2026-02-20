//
// Created by luocf on 2019/7/23.
//


#include <map>
#include <iostream>
#include "DatabaseProxy.h"
#include "../../common/Log.hpp"

DatabaseProxy::DatabaseProxy() {
    mGroupList = std::make_shared<std::vector<std::shared_ptr<GroupInfo>>>();
}

DatabaseProxy::~DatabaseProxy() {
    mGroupList.reset();
}

void DatabaseProxy::updateGroupMemberCount(std::string friendid,
                                           int member_count) {
    std::shared_ptr<GroupInfo> group_info = this->getGroupInfo(friendid);
    MUTEX_LOCKER locker_sync_data(_SyncedGroupList);
    std::string t_strSql = "";
    char *errMsg = NULL;
    if (group_info.get() != nullptr) {
        group_info->Lock();
        group_info->setMemberCount(member_count);
        t_strSql = "update group_table ";
        t_strSql += " set MemberCount='" + std::to_string(group_info->getMemberCount()) + "'";
        t_strSql += " where Friendid='" + group_info->getAddress() + "';";
        group_info->UnLock();
        printf( "t_strSql: %s\n",
                t_strSql.c_str());
        //消息直接入库
        int rv = sqlite3_exec(mDb, t_strSql.c_str(), callback, this, &errMsg);
        if (rv != SQLITE_OK) {
            printf( "SQLite update updateGroupMemberCount error: %s\n",
                   errMsg);
        }
    }
}
void DatabaseProxy::updateGroupAddress(int service_id, std::string address) {
    std::shared_ptr<GroupInfo> group_info = this->getGroupInfo(service_id);
    MUTEX_LOCKER locker_sync_data(_SyncedGroupList);
    std::string t_strSql = "";
    char *errMsg = NULL;
    if (group_info.get() != nullptr) {
        group_info->Lock();
        group_info->setAddress(address);
        t_strSql = "update group_table set FriendId='" + address + "'";
        t_strSql += " where ServiceId='" + std::to_string(service_id) + "';";
        printf( "t_strSql: %s\n",
               t_strSql.c_str());
        group_info->UnLock();
        //消息直接入库
        int rv = sqlite3_exec(mDb, t_strSql.c_str(), callback, this, &errMsg);
        if (rv != SQLITE_OK) {
            printf( "SQLite update updateGroupAddress error: %s\n",
                   errMsg);
        }
    }
}
void DatabaseProxy::updateGroupNickName(std::string friendid, std::string nickname) {
    std::shared_ptr<GroupInfo> group_info = this->getGroupInfo(friendid);
    MUTEX_LOCKER locker_sync_data(_SyncedGroupList);
    std::string t_strSql = "";
    char *errMsg = NULL;
    if (group_info.get() != nullptr) {
        group_info->Lock();
        group_info->setNickName(nickname);
        t_strSql = "update group_table set NickName='" + nickname + "'";
        t_strSql += " where Friendid='" + group_info->getAddress() + "';";
        group_info->UnLock();
        printf( "t_strSql: %s\n",
                t_strSql.c_str());
        //消息直接入库
        int rv = sqlite3_exec(mDb, t_strSql.c_str(), callback, this, &errMsg);
        if (rv != SQLITE_OK) {
            printf( "SQLite update updateGroupNickName error: %s\n",
                   errMsg);
        }
    }

}

void DatabaseProxy::addGroup(std::string friendid,
                             std::string nickname,
                             std::string data_dir,
                             int member_count,
                             int service_id) {
    MUTEX_LOCKER locker_sync_data(_SyncedGroupList);
    std::string t_strSql = "";
    char *errMsg = NULL;

    std::shared_ptr<GroupInfo> group_info = std::make_shared<GroupInfo>(
            friendid, nickname, data_dir, 0, service_id);
    mGroupList->push_back(group_info);
    t_strSql = "insert into group_table values(NULL,'" + friendid + "'";
    t_strSql += ",'" + nickname + "'";
    t_strSql += ",'" + data_dir + "'";
    t_strSql += ",'" + std::to_string(member_count) + "'";
    t_strSql += ",'" + std::to_string(service_id) + "'";
    t_strSql += ");";

    //消息直接入库
    int rv = sqlite3_exec(mDb, t_strSql.c_str(), callback, this, &errMsg);
    if (rv != SQLITE_OK) {
        printf( "SQLite addGroup error: %s\n",
               errMsg);
    }
    printf( "SQLite addGroup t_strSql: %s\n",
           t_strSql.c_str());
}

std::shared_ptr<GroupInfo>
DatabaseProxy::getGroupInfo(std::string friendid) {
    MUTEX_LOCKER locker_sync_data(_SyncedGroupList);
    for (int i = 0; i < mGroupList->size(); i++) {
        std::shared_ptr<GroupInfo> group_info = mGroupList->at(i);
        if (group_info.get() == nullptr) {
            continue;
        }
        group_info->Lock();
        if (group_info->getAddress().compare(friendid) == 0) {
            group_info->UnLock();
            return group_info;
        }
        group_info->UnLock();
    }
    return std::shared_ptr<GroupInfo>(nullptr);
}

std::shared_ptr<GroupInfo> DatabaseProxy::getGroupInfo(int service_id) {
    MUTEX_LOCKER locker_sync_data(_SyncedGroupList);
    for (int i = 0; i < mGroupList->size(); i++) {
        std::shared_ptr<GroupInfo> group_info = mGroupList->at(i);
        if (group_info.get() == nullptr) {
            continue;
        }
        group_info->Lock();
        if (group_info->getServiceId() == service_id) {
            group_info->UnLock();
            return group_info;
        }
        group_info->UnLock();
    }
    return std::shared_ptr<GroupInfo>(nullptr);
}


std::shared_ptr<std::vector<std::shared_ptr<GroupInfo>>> DatabaseProxy::getGroupList() {
    MUTEX_LOCKER locker_sync_data(_SyncedGroupList);
    return mGroupList;
}

bool DatabaseProxy::removeGroup(std::string friendid) {
    MUTEX_LOCKER locker_sync_data(_SyncedGroupList);
    for (int i = 0; i < mGroupList->size(); i++) {
        std::shared_ptr<GroupInfo> group_info = mGroupList->at(i);
        if (group_info.get() == nullptr) {
            continue;
        }
        group_info->Lock();
        if (group_info->getAddress().compare(friendid) == 0) {
            char *errMsg = NULL;
            std::string t_strSql;
            t_strSql = "delete from group_table where FriendId='" + friendid + "';\n";
            printf("removeGroup, t_strSql:%s", t_strSql.c_str());
            //消息直接入库
            int rv = sqlite3_exec(mDb, t_strSql.c_str(), callback, this, &errMsg);
            if (rv != SQLITE_OK) {
                printf( "SQLite removeGroup error: %s\n",
                       errMsg);
            }
            group_info->UnLock();
            mGroupList->erase(std::begin(*mGroupList.get()) + i);
            printf("removeGroup, erase friendid:%s \n", friendid.c_str());
            return true;
        }
        group_info->UnLock();
    }
    printf("removeGroup not exist, friendid:%s", friendid.c_str());
    return false;
}

int DatabaseProxy::callback(void *context, int argc, char **argv, char **azColName) {
    auto database_proxy = reinterpret_cast<DatabaseProxy *>(context);
    int i;
    for (i = 0; i < argc; ++i) {
        printf( "database %s = %s\n", azColName[i],
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

void DatabaseProxy::syncGroupList() {
    MUTEX_LOCKER locker_sync_data(_SyncedGroupList);
    //查询一条记录
    char **azResult;   //二维数组存放结果
    char *errMsg = NULL;
    int nrow;          /* Number of result rows written here */
    int ncolumn;
    std::string t_strSql;
    t_strSql = "select * from group_table order by id asc";
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
        printf("sqlite3_prepare_v2 error:%s", sqlite3_errmsg(mDb));
        return;
    }

    rc = sqlite3_get_table(mDb, t_strSql.c_str(), &azResult, &nrow, &ncolumn, &errMsg);
    if (rc == SQLITE_OK) {
        printf("syncMemberList, successful sql:%s", t_strSql.c_str());
    } else {
        printf("syncMemberList, Can't get table: %s", sqlite3_errmsg(mDb));
        return;
    }

    if (nrow != 0 && ncolumn != 0) {     //有查询结果,不包含表头所占行数
        for (int i = 1; i <= nrow; i++) {        // 第0行为数据表头
            mGroupList->push_back(std::make_shared<GroupInfo>(std::string(azResult[6 * i + 1]),
                                                              std::string(azResult[6 * i + 2]),
                                                              std::string(azResult[6 * i + 3]),
                                                              atoi(azResult[6 * i + 4]),
                                                              atoi(azResult[6 * i + 5])));
        }
    }

    sqlite3_free_table(azResult);
    sqlite3_finalize(pStmt);     //销毁一个SQL语句对象
}

bool DatabaseProxy::startDb(const char *data_dir) {
    std::string strConn = std::string(data_dir) + "/chatrobotmanager.db";
    char *errMsg = NULL;
    //打开一个数据库，如果改数据库不存在，则创建一个名字为databaseName的数据库文件
    int rv;
    rv = sqlite3_config(SQLITE_CONFIG_MULTITHREAD);
    if (rv != SQLITE_OK && rv != SQLITE_MISUSE) {
        printf( "sqlite3_config error: %d\n", rv);
        return 1;
    }
    rv = sqlite3_open(strConn.c_str(), &mDb);
    if (rv != SQLITE_OK) {
        printf( "Cannot open database: %s\n", sqlite3_errmsg(mDb));
        sqlite3_close(mDb);
        return 1;
    }
    char create_table[256] = "CREATE TABLE IF NOT EXISTS group_table(id INTEGER PRIMARY KEY AUTOINCREMENT,Friendid TEXT NOT NULL, NickName TEXT, DataDir TEXT, MemberCount INTEGER, ServiceId INTEGER)";
    rv = sqlite3_exec(mDb, create_table, callback, this, &errMsg);
    if (rv != SQLITE_OK) {
        printf( "SQLite create_table statement execution error: %s\n",
               errMsg);
        return 1;
    }
    //同步Group 信息
    syncGroupList();
    return 0;
}
