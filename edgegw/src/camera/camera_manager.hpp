// ======================================================================
// camera_manager.hpp - 摄像头管理 (fork+exec gstreamer)
//
// 通过 fork+exec 调用 gst-launch-1.0 子进程:
//   - 拍照: 抓一帧 JPEG 存文件
//   - 录像: H.264 录制到文件 (可停止)
// ======================================================================
#ifndef EDGEGW_CAMERA_CAMERA_MANAGER_HPP
#define EDGEGW_CAMERA_CAMERA_MANAGER_HPP

#include <string>

namespace edgegw {
namespace camera {

struct CameraStatus {
    bool recording = false;      // 是否正在录像
    std::string record_file;     // 当前录像文件
    std::string last_snapshot;   // 最近照片
};

class CameraManager {
public:
    // 初始化: 设置数据保存目录
    bool Init(const std::string& data_dir);

    // 抓拍一张照片; 返回照片路径 (失败返回空串)
    std::string TakeSnapshot();

    // 开始录像; 返回录像文件路径
    std::string StartRecord();

    // 停止录像; 成功返回 true
    bool StopRecord();

    // 查询状态
    CameraStatus GetStatus() const;

    // 停止所有子进程 (退出时调用)
    void Shutdown();

private:
    // 启动 gstreamer 子进程; 返回 PID (失败返回 -1)
    int SpawnGst(const std::string& pipeline);

    // 保存目录
    std::string snapshots_dir_;   // 照片目录
    std::string records_dir_;     // 录像目录

    // 录像状态
    int record_pid_ = -1;         // 录像子进程 PID
    bool recording_ = false;
    std::string record_file_;
    std::string last_snapshot_;
};

}  // namespace camera
}  // namespace edgegw

#endif  // EDGEGW_CAMERA_CAMERA_MANAGER_HPP
