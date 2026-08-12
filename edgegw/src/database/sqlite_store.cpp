// ======================================================================
// sqlite_store.cpp - SQLite 历史数据存储实现
// ======================================================================
#include "database/sqlite_store.hpp"

#include <cstdio>
#include <sqlite3.h>    // 真正的 sqlite3 头文件只在 .cpp 里包含

namespace edgegw {
namespace database {


bool SqliteStore::Init(const std::string& db_path) {
    // ① 先建真正的 sqlite3* 局部变量
    sqlite3* db = nullptr;
    int rc = sqlite3_open(db_path.c_str(), &db);
    if (rc != SQLITE_OK) {
        std::printf("[db] 打开失败: %s\n", sqlite3_errmsg(db));
        return false;
    }
    db_ = db;   // ② 成功后存入成员

    // ② 建表 (IF NOT EXISTS: 已存在则跳过)
    const char* sql =
        "CREATE TABLE IF NOT EXISTS telemetry ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " dev_id TEXT NOT NULL,"
        " temp REAL,"
        " humi REAL,"
        " light REAL,"
        " ir REAL,"
        " ts INTEGER);";

    char* errmsg = nullptr;
    rc = sqlite3_exec(static_cast<sqlite3*>(db_), sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::printf("[db] 建表失败: %s\n", errmsg);
        sqlite3_free(errmsg);
        return false;
    }
    std::printf("[db] 初始化成功: %s\n", db_path.c_str());
    return true;
}

bool SqliteStore::InsertTelemetry(const device::SensorData& data) {
    // ③ 预处理语句 (prepared statement): 用 ? 占位, 防 SQL 注入
    const char* sql =
        "INSERT INTO telemetry (dev_id, temp, humi, light, ir, ts)"
        " VALUES (?1, ?2, ?3, ?4, ?5, ?6);";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::printf("[db] prepare 失败: %s\n", sqlite3_errmsg(static_cast<sqlite3*>(db_)));
        return false;
    }

    // ④ 绑定参数 (?1 → 第1个占位符)
    sqlite3_bind_text(stmt, 1, data.dev_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, data.temp);
    sqlite3_bind_double(stmt, 3, data.humi);
    sqlite3_bind_double(stmt, 4, data.light);
    sqlite3_bind_double(stmt, 5, data.ir);
    sqlite3_bind_int64(stmt, 6, data.ts);

    // ⑤ 执行
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::printf("[db] 插入失败: %s\n", sqlite3_errmsg(static_cast<sqlite3*>(db_)));
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);   // ⑥ 释放语句 (重要! 否则内存泄漏)
    return true;
}

void SqliteStore::Close() {
    if (db_ != nullptr) {
        sqlite3_close(static_cast<sqlite3*>(db_));
        db_ = nullptr;
        std::printf("[db] 已关闭\n");
    }
}

SqliteStore::~SqliteStore() {
    Close();   // RAII: 析构时确保关闭
}

}  // namespace database
}  // namespace edgegw