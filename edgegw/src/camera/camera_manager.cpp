// ======================================================================
// camera_manager.cpp - 摄像头管理实现 (fork+exec gstreamer)
//
// 原理: 网关程序通过 fork()+exec() 启动 gst-launch-1.0 子进程,
//       拍照/录像都是独立的 gstreamer 管道, 与网关主进程隔离
// ======================================================================
#include "camera/camera_manager.hpp"

#include "logger/logger.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>

namespace edgegw {
namespace camera {

bool CameraManager::Init(const std::string& data_dir) {
    snapshots_dir_ = data_dir + "/snapshots";
    records_dir_ = data_dir + "/records";

    // 创建目录 (mkdir -p 语义)
    std::string cmd = "mkdir -p " + snapshots_dir_ + " " + records_dir_;
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        logger::Logger::Error("[camera] 创建目录失败: " + cmd);
        return false;
    }

    logger::Logger::Info("[camera] 初始化完成: " + data_dir);
    return true;
}

// 启动 gstreamer 子进程; 返回 PID (失败返回 -1)
int CameraManager::SpawnGst(const std::string& pipeline) {
    // fork: 创建子进程
    pid_t pid = fork();
    if (pid < 0) {
        logger::Logger::Error("[camera] fork 失败");
        return -1;
    }

    if (pid == 0) {
        // ===== 子进程 =====
        // 重定向子进程输出到 /dev/null (避免污染终端)
        FILE* devnull = freopen("/dev/null", "w", stdout);
        if (devnull != nullptr) freopen("/dev/null", "w", stderr);

        // exec: 把子进程替换成 gst-launch-1.0
        // execlp 会去 PATH 找 gst-launch-1.0
        execlp("gst-launch-1.0", "gst-launch-1.0", pipeline.c_str(), nullptr);

        // exec 失败才会到这里
        logger::Logger::Error("[camera] exec gst-launch-1.0 失败");
        _exit(127);
    }

    // ===== 父进程 =====
    logger::Logger::Info("[camera] gst 子进程已启动, pid=" + std::to_string(pid));
    return static_cast<int>(pid);
}

std::string CameraManager::TakeSnapshot() {
    // 生成文件名: snap_<时间戳>.jpg
    char name[64];
    std::time_t now = std::time(nullptr);
    std::snprintf(name, sizeof(name), "snap_%ld.jpg", static_cast<long>(now));
    std::string path = snapshots_dir_ + "/" + name;

    // gstreamer 管道: 摄像头 → JPEG 编码 → 写文件
    // 用 multifilesink 的 location 只写一帧后退出 (eos 机制)
    std::string pipeline =
        "v4l2src device=/dev/video0 num-buffers=1 ! "
        "video/x-raw,format=NV12,width=640,height=480 ! "
        "videoconvert ! jpegenc quality=70 ! "
        "filesink location=" + path;

    int pid = SpawnGst(pipeline);
    if (pid < 0) return "";

    // 等待拍照完成 (最多 5 秒)
    int status = 0;
    for (int i = 0; i < 50; ++i) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) break;        // 子进程结束
        usleep(100000);             // 100ms
    }

    // 检查文件是否生成
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        logger::Logger::Error("[camera] 拍照失败, 文件未生成");
        return "";
    }
    std::fclose(f);

    last_snapshot_ = path;
    logger::Logger::Info("[camera] 拍照成功: " + path);
    return path;
}

std::string CameraManager::StartRecord() {
    if (recording_) {
        logger::Logger::Warn("[camera] 已在录像中");
        return record_file_;
    }

    // 生成文件名: rec_<时间戳>.mp4
    char name[64];
    std::time_t now = std::time(nullptr);
    std::snprintf(name, sizeof(name), "rec_%ld.mp4", static_cast<long>(now));
    std::string path = records_dir_ + "/" + name;

    // gstreamer 管道: 摄像头 → H.264 硬件编码 → mp4
    std::string pipeline =
        "v4l2src device=/dev/video0 ! "
        "video/x-raw,format=NV12,width=640,height=480 ! "
        "mpph264enc ! h264parse ! mp4mux ! "
        "filesink location=" + path;

    int pid = SpawnGst(pipeline);
    if (pid < 0) return "";

    record_pid_ = pid;
    record_file_ = path;
    recording_ = true;
    logger::Logger::Info("[camera] 开始录像: " + path);
    return path;
}

bool CameraManager::StopRecord() {
    if (!recording_) {
        logger::Logger::Warn("[camera] 当前未在录像");
        return false;
    }

    // 给 gst 子进程发送 SIGTERM, 让它正常收尾写文件
    if (record_pid_ > 0) {
        kill(record_pid_, SIGTERM);

        // 等待子进程退出 (最多 5 秒)
        int status = 0;
        for (int i = 0; i < 50; ++i) {
            pid_t r = waitpid(record_pid_, &status, WNOHANG);
            if (r == record_pid_) break;
            usleep(100000);
        }
    }

    recording_ = false;
    record_pid_ = -1;
    logger::Logger::Info("[camera] 录像已停止: " + record_file_);
    return true;
}

CameraStatus CameraManager::GetStatus() const {
    CameraStatus st;
    st.recording = recording_;
    st.record_file = record_file_;
    st.last_snapshot = last_snapshot_;
    return st;
}

void CameraManager::Shutdown() {
    if (recording_) {
        StopRecord();
    }
    logger::Logger::Info("[camera] 已关闭");
}

}  // namespace camera
}  // namespace edgegw
