// ======================================================================
// camera_manager.hpp - 摄像头管理 (gstreamer 常驻进程)
//
// 方案 (验证过):
//   推流 = gst 常驻进程: v4l2src → mppjpegenc(硬件编码) → multifilesink
//        持续覆盖写 latest.jpg (15fps, 无进程重启开销)
//   拍照 = 从 latest.jpg 复制一份
//   录像 = 独立 gst 进程: mpph264enc(硬件编码) → mp4
// ======================================================================
#ifndef EDGEGW_CAMERA_CAMERA_MANAGER_HPP
#define EDGEGW_CAMERA_CAMERA_MANAGER_HPP

#include <string>
#include <sys/types.h>

namespace edgegw {
namespace camera {

struct CameraStatus {
    bool streaming = false;      // 推流进程是否运行
    bool recording = false;      // 录像进程是否运行
    bool frame_ready = false;    // 是否有可用帧
    std::string record_file;     // 当前录像文件
    std::string last_snapshot;   // 最近照片
};

class CameraManager {
public:
    // 初始化: 设置保存目录和设备参数
    bool Init(const std::string& data_dir,
              const std::string& device = "/dev/video0",
              int width = 640, int height = 480, int framerate = 15);

    // 启动推流 (常驻 gst 进程写 latest.jpg)
    bool Start();

    // 停止推流
    bool Stop();

    // 抓拍一张照片 (从 latest.jpg 复制); 返回路径
    std::string TakeSnapshot();

    // 抓拍并返回完整帧的 base64 (前端 data URI 直接显示)
    std::string TakeSnapshotBase64();

    // 开始录像; 返回文件路径
    std::string StartRecord();

    // 停止录像
    bool StopRecord();

    // 查询状态
    CameraStatus GetStatus() const;

    // 最新帧文件路径 (multipart 推流用)
    std::string FramePath() const;

    // 最近抓拍静态文件路径 (只在抓拍时写, 不被推流改写)
    std::string LastSnapshotPath() const;

    // 是否有可用帧
    bool HasFrame() const;

    // 停止所有子进程 (退出时调用)
    void Shutdown();

private:
    // 用 /bin/sh -c 启动命令 (shell 解析参数, 避免 execl 引号问题)
    bool Spawn(const std::string& cmd, pid_t* pid);
    bool StopProcess(pid_t* pid, const char* label);

    std::string device_ = "/dev/video0";
    int width_ = 640;
    int height_ = 480;
    int framerate_ = 15;
    std::string runtime_dir_;     // latest.jpg 所在目录
    std::string media_dir_;       // 照片/录像保存目录
    pid_t stream_pid_ = -1;
    pid_t record_pid_ = -1;
    std::string record_file_;
    std::string last_snapshot_;
};

}  // namespace camera
}  // namespace edgegw

#endif  // EDGEGW_CAMERA_CAMERA_MANAGER_HPP
