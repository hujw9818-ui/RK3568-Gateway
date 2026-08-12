// ======================================================================
// sqlite_store.hpp - SQLite 历史数据存储
//
// 用途: 把设备上报的数据写入 SQLite, 保存历史记录
//       表结构: telemetry(id, dev_id, temp, humi, light, ir, ts)
// 设计: db_ 用 void* 隐藏 sqlite3 实现, 减少头文件依赖 (Pimpl 思想)
// ======================================================================
#ifndef EDGEGW_DATABASE_SQLITE_STORE_HPP
#define EDGEGW_DATABASE_SQLITE_STORE_HPP 

#include <string>

#include "device/sensor_data.hpp"

namespace edgegw {
namespace database{
class SqliteStore {
public:
    //打开数据库文件并建表；成功返回true
    bool Init(const std::string& db_path);

    //插入一条传感器历史记录；成功返回true
    bool InsertTelemetry(const device::SensorData& data);

    //关闭数据库
    void Close();

    //禁止拷贝（db_是资源，不能复制）
    SqliteStore() = default;
    ~SqliteStore();
    SqliteStore(const SqliteStore&) = delete;
    SqliteStore& operator=(const SqliteStore&) = delete;

private:
    void* db_ = nullptr;  //sqlite3*(隐藏实现)
};


}  // namespace database
}  // namespace edgegw
#endif