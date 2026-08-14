// ======================================================================
// camera_manager.cpp - 摄像头管理实现 (gstreamer 常驻进程)
//
// 核心设计 (借鉴验证过的方案):
//   - 用 /bin/sh -c 启动 gst (shell 解析参数, 解决 execl 单参数引号问题)
//   - 推流是常驻进程: 持续覆盖写 latest.jpg, 无进程重启开销 (15fps)
//   - mppjpegenc 硬件编码 (RK3568 MPP, 比软件 jpegenc 快)
// ======================================================================
#include "camera/camera_manager.hpp"

#include "logger/logger.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace edgegw {
namespace camera {

namespace {

// 检查进程是否存活 (kill(pid,0) 不发信号只探测)
bool IsAlive(pid_t pid) {
    if (pid <= 0) return false;
    if (kill(pid, 0) == 0) return true;
    return errno == EPERM;
}

// 生成带时间戳的文件名
std::string TimestampedName(const char* prefix, const char* ext) {
    char stamp[32] = {0};
    std::time_t now = std::time(nullptr);
    std::tm* local = std::localtime(&now);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", local);
    return std::string(prefix) + "_" + stamp + "." + ext;
}

// 读完整 JPEG: 检查结束标记 FF D9, 半帧则快速重试
// 重试间隔 16ms (不是 66ms): 减少事件循环阻塞时间, 推流中抓拍不卡
bool ReadCompleteJpeg(const std::string& path, std::string& out) {
    for (int retry = 0; retry < 8; ++retry) {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            usleep(16000);
            continue;
        }
        std::string data((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        if (data.size() >= 2 &&
            static_cast<unsigned char>(data[data.size() - 2]) == 0xFF &&
            static_cast<unsigned char>(data[data.size() - 1]) == 0xD9) {
            out = data;
            return true;
        }
        usleep(16000);   // 半帧, 快速等下一帧
    }
    return false;
}

// base64 编码 (拍照数据内嵌 JSON 响应用)
std::string Base64Encode(const std::string& data) {
    static const char* table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        unsigned int n = static_cast<unsigned char>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<unsigned char>(data[i + 1]) << 8;
        if (i + 2 < data.size()) n |= static_cast<unsigned char>(data[i + 2]);
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += (i + 1 < data.size()) ? table[(n >> 6) & 0x3F] : '=';
        out += (i + 2 < data.size()) ? table[n & 0x3F] : '=';
    }
    return out;
}

}  // namespace

bool CameraManager::Init(const std::string& data_dir, const std::string& device,
                         int width, int height, int framerate) {
    device_ = device;
    width_ = width;
    height_ = height;
    framerate_ = framerate;
    runtime_dir_ = data_dir + "/runtime";
    media_dir_ = data_dir;

    // 清理可能残留的孤儿 gst 进程 (上次网关异常退出留下的推流进程)
    // 这些孤儿进程会占用 /dev/video0, 导致单帧抓拍 busy
    // 用简单模式 + -9 强杀 (busybox 正则兼容性差, 别用复杂模式)
    std::system("pkill -9 -f gst-launch-1.0 2>/dev/null");
    usleep(500000);   // 等 500ms 让设备释放

    // 创建目录 (mkdir -p 语义)
    std::string cmd = "mkdir -p " + runtime_dir_ + " " + media_dir_;
    if (std::system(cmd.c_str()) != 0) {
        logger::Logger::Error("[camera] 创建目录失败: " + cmd);
        return false;
    }

    logger::Logger::Info("[camera] 初始化完成: " + data_dir);
    return true;
}

// 用 /bin/sh -c 启动命令 (shell 解析参数和引号, 避免 execl 问题)
bool CameraManager::Spawn(const std::string& cmd, pid_t* pid) {
    pid_t child = fork();
    if (child < 0) {
        logger::Logger::Error("[camera] fork 失败: " +
                              std::string(std::strerror(errno)));
        return false;
    }
    if (child == 0) {
        // 子进程: stdout 丢弃, stderr 落盘 (排查 gst 崩溃原因)
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDOUT_FILENO);
            close(null_fd);
        }
        int err_fd = open("/tmp/gst_camera_err.log",
                          O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (err_fd >= 0) {
            dup2(err_fd, STDERR_FILENO);
            close(err_fd);
        }
        execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    *pid = child;
    logger::Logger::Info("[camera] 子进程已启动, pid=" + std::to_string(child));
    return true;
}

// 停止进程: SIGINT 优雅停止 (gst 正常拆除管道, MPP 硬件释放干净)
// SIGINT 相当于 Ctrl+C: gst-launch 触发 EOS 正常退出, 不残留硬件状态
// 2 秒没退出再 SIGKILL 兜底
bool CameraManager::StopProcess(pid_t* pid, const char* label) {
    if (*pid <= 0) return true;
    if (IsAlive(*pid)) {
        // SIGINT 优雅停止, 等 5 秒 (gst 拆管道 + rkisp STREAMOFF 需要时间)
        kill(*pid, SIGINT);
        for (int i = 0; i < 50 && IsAlive(*pid); ++i) usleep(100000);
        if (IsAlive(*pid)) {
            // 还活着: SIGTERM 再等 2 秒
            kill(*pid, SIGTERM);
            for (int i = 0; i < 20 && IsAlive(*pid); ++i) usleep(100000);
        }
        if (IsAlive(*pid)) {
            // 最后手段: SIGKILL (有风险, 会弄脏 rkisp 状态)
            logger::Logger::Warn(
                std::string("[camera] ") + label +
                " 未优雅退出, SIGKILL (可能导致设备状态异常)");
            kill(*pid, SIGKILL);
        }
        int status = 0;
        waitpid(*pid, &status, 0);
    }
    *pid = -1;
    logger::Logger::Info(std::string("[camera] ") + label + " 进程已停止");
    return true;
}

bool CameraManager::Start() {
    if (IsAlive(stream_pid_)) return true;   // 已在推流

    // 删除旧帧, 确保 HasFrame 判断准确
    unlink(FramePath().c_str());

    // 刚停止过推流时, 等 500ms 让 sensor/V4L2 完全释放
    usleep(500000);

    // 常驻推流进程: 硬件 JPEG 编码, 持续覆盖写 latest.jpg
    std::string pipeline =
        "exec gst-launch-1.0 -q v4l2src device=" + device_ +
        " ! video/x-raw,format=NV12,width=" + std::to_string(width_) +
        ",height=" + std::to_string(height_) +
        ",framerate=" + std::to_string(framerate_) + "/1" +
        " ! mppjpegenc ! multifilesink location=" + FramePath() +
        " max-files=1 post-messages=false";

    if (!Spawn(pipeline, &stream_pid_)) return false;

    // 等第一帧 (最多 2 秒)
    for (int i = 0; i < 20 && IsAlive(stream_pid_) && !HasFrame(); ++i)
        usleep(100000);

    if (!IsAlive(stream_pid_) || !HasFrame()) {
        // 第一次失败: 可能设备状态脏 (上次 SIGKILL 残留), 等 1 秒重试
        StopProcess(&stream_pid_, "推流");
        usleep(1000000);
        logger::Logger::Warn("[camera] 首次推流失败, 1秒后重试");
        unlink(FramePath().c_str());
        std::string pipeline_retry =
            "exec gst-launch-1.0 -q v4l2src device=" + device_ +
            " ! video/x-raw,format=NV12,width=" + std::to_string(width_) +
            ",height=" + std::to_string(height_) +
            ",framerate=" + std::to_string(framerate_) + "/1" +
            " ! mppjpegenc ! multifilesink location=" + FramePath() +
            " max-files=1 post-messages=false";
        if (!Spawn(pipeline_retry, &stream_pid_)) return false;
        for (int i = 0; i < 30 && IsAlive(stream_pid_) && !HasFrame(); ++i)
            usleep(100000);
        if (!IsAlive(stream_pid_) || !HasFrame()) {
            StopProcess(&stream_pid_, "推流");
            logger::Logger::Error("[camera] 推流重试仍失败");
            return false;
        }
    }

    logger::Logger::Info("[camera] 推流已启动: " + device_);
    return true;
}

bool CameraManager::Stop() {
    // 录像中: 先停录像 (不自动恢复推流, 因为本函数就是要停推流)
    if (IsAlive(record_pid_)) {
        StopProcess(&record_pid_, "录像");
        record_file_.clear();
    }
    return StopProcess(&stream_pid_, "推流");
}

std::string CameraManager::TakeSnapshot() {
    std::string name = TimestampedName("snapshot", "jpg");
    std::string path = media_dir_ + "/" + name;

    std::string frame;   // 完整帧数据

    if (IsAlive(stream_pid_)) {
        // ---- 推流中: 从 latest.jpg 读完整帧 ----
        if (!ReadCompleteJpeg(FramePath(), frame)) {
            logger::Logger::Error("[camera] 读取完整帧失败");
            return "";
        }
    } else {
        // ---- 未推流: 单帧抓拍, 直接写快照文件 (原子完整) ----
        std::string pipeline =
            "exec gst-launch-1.0 -q v4l2src device=" + device_ +
            " num-buffers=1 ! video/x-raw,format=NV12,width=" +
            std::to_string(width_) + ",height=" + std::to_string(height_) +
            ",framerate=" + std::to_string(framerate_) + "/1" +
            " ! mppjpegenc ! filesink location=" + path;

        std::string cmd = std::string("/bin/sh -c \"") + pipeline + "\"";
        if (std::system(cmd.c_str()) != 0) {
            logger::Logger::Error("[camera] 单帧抓拍失败");
            return "";
        }
        // 读回刚拍的完整帧 (用于写 latest.jpg)
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) return "";
        frame.assign((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
        if (frame.empty()) return "";
    }

    // ---- 两种情况统一: 把完整帧覆盖写到 latest.jpg ----
    // 这样前端 last_photo 永远读到完整照片 (修复"从上到下慢慢显示")
    {
        std::ofstream latest(FramePath(), std::ios::binary | std::ios::trunc);
        latest.write(frame.data(), static_cast<std::streamsize>(frame.size()));
        latest.close();
    }

    // ---- 推流模式: 复制帧到快照文件; 单帧模式: 已写好 ----
    if (IsAlive(stream_pid_)) {
        std::ofstream out(path, std::ios::binary);
        out.write(frame.data(), static_cast<std::streamsize>(frame.size()));
        if (!out.good()) {
            logger::Logger::Error("[camera] 保存抓拍失败");
            return "";
        }
    }

    last_snapshot_ = path;
    logger::Logger::Info("[camera] 抓拍成功: " + path);
    return path;
}

// 抓拍并返回 base64 (前端 data URI 直接显示)
std::string CameraManager::TakeSnapshotBase64() {
    std::string frame;

    if (IsAlive(stream_pid_)) {
        // 推流中: 从 latest.jpg 读完整帧
        if (!ReadCompleteJpeg(FramePath(), frame)) {
            logger::Logger::Error("[camera] 读取完整帧失败");
            return "";
        }
    } else {
        // 未推流: 单帧抓拍到临时文件, 读回
        std::string tmp = runtime_dir_ + "/snap_tmp.jpg";
        std::string pipeline =
            "exec gst-launch-1.0 -q v4l2src device=" + device_ +
            " num-buffers=1 ! video/x-raw,format=NV12,width=" +
            std::to_string(width_) + ",height=" + std::to_string(height_) +
            ",framerate=" + std::to_string(framerate_) + "/1" +
            " ! mppjpegenc ! filesink location=" + tmp;
        std::string cmd = std::string("/bin/sh -c \"") + pipeline + "\"";
        bool snap_ok = (std::system(cmd.c_str()) == 0);
        if (snap_ok) {
            std::ifstream in(tmp, std::ios::binary);
            if (in.is_open()) {
                frame.assign((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
            }
        }
        if (frame.empty()) {
            // 单帧失败 (可能有孤儿进程占设备): 回退从 latest.jpg 复制
            if (!ReadCompleteJpeg(FramePath(), frame)) {
                logger::Logger::Error("[camera] 抓拍失败 (单帧+复制都失败)");
                return "";
            }
        }
    }

    // 保存快照文件 (留档) + 写静态 latest_snap.jpg
    std::string name = TimestampedName("snapshot", "jpg");
    std::string path = media_dir_ + "/" + name;
    {
        std::ofstream out(path, std::ios::binary);
        out.write(frame.data(), static_cast<std::streamsize>(frame.size()));
    }
    {
        std::ofstream snap(LastSnapshotPath(),
                           std::ios::binary | std::ios::trunc);
        snap.write(frame.data(), static_cast<std::streamsize>(frame.size()));
    }
    last_snapshot_ = path;

    return Base64Encode(frame);
}

std::string CameraManager::StartRecord() {
    if (IsAlive(record_pid_)) return record_file_;   // 已在录像

    // 录像需要独占 /dev/video0, 先停推流 (rkisp 主通道只能开一个)
    if (IsAlive(stream_pid_)) {
        logger::Logger::Info("[camera] 录像前暂停推流");
        StopProcess(&stream_pid_, "推流");
        usleep(500000);   // 等 sensor/V4L2 完全释放
    }

    std::string name = TimestampedName("record", "mp4");
    std::string path = media_dir_ + "/" + name;

    // 独立录像进程: 硬件 H.264 编码 → mp4
    // -e (eos-on-shutdown): SIGINT 停止时强制发 EOS, mp4mux 才能写 moov header
    std::string pipeline =
        "exec gst-launch-1.0 -e -q v4l2src device=" + device_ +
        " ! video/x-raw,format=NV12,width=" + std::to_string(width_) +
        ",height=" + std::to_string(height_) +
        ",framerate=" + std::to_string(framerate_) + "/1" +
        " ! mpph264enc ! h264parse ! mp4mux faststart=true" +
        " ! filesink location=" + path;

    if (!Spawn(pipeline, &record_pid_)) return "";

    // 等 700ms 确认进程没立刻崩溃
    usleep(700000);
    if (!IsAlive(record_pid_)) {
        record_pid_ = -1;
        logger::Logger::Error("[camera] 录像启动失败");
        return "";
    }

    record_file_ = path;
    logger::Logger::Info("[camera] 录像已启动: " + path);
    return path;
}

bool CameraManager::StopRecord() {
    const bool ok = StopProcess(&record_pid_, "录像");
    record_file_.clear();
    // 录完自动恢复推流
    usleep(500000);
    Start();
    return ok;
}

CameraStatus CameraManager::GetStatus() const {
    CameraStatus st;
    st.streaming = IsAlive(stream_pid_);
    st.recording = IsAlive(record_pid_);
    st.frame_ready = HasFrame();
    st.record_file = record_file_;
    st.last_snapshot = last_snapshot_;
    return st;
}

std::string CameraManager::FramePath() const {
    return runtime_dir_ + "/latest.jpg";
}

std::string CameraManager::LastSnapshotPath() const {
    return runtime_dir_ + "/latest_snap.jpg";
}

bool CameraManager::HasFrame() const {
    struct stat st {};
    return stat(FramePath().c_str(), &st) == 0 && st.st_size > 0;
}

void CameraManager::Shutdown() {
    StopRecord();
    StopProcess(&stream_pid_, "推流");
    logger::Logger::Info("[camera] 已关闭");
}

// base64 编码 (拍照数据内嵌 JSON 响应用)
std::string Base64Encode(const std::string& data) {
    static const char* table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        unsigned int n = static_cast<unsigned char>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<unsigned char>(data[i + 1]) << 8;
        if (i + 2 < data.size()) n |= static_cast<unsigned char>(data[i + 2]);
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += (i + 1 < data.size()) ? table[(n >> 6) & 0x3F] : '=';
        out += (i + 2 < data.size()) ? table[n & 0x3F] : '=';
    }
    return out;
}

}  // namespace camera
// base64 编码 (拍照数据内嵌 JSON 响应用)
std::string Base64Encode(const std::string& data) {
    static const char* table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        unsigned int n = static_cast<unsigned char>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<unsigned char>(data[i + 1]) << 8;
        if (i + 2 < data.size()) n |= static_cast<unsigned char>(data[i + 2]);
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += (i + 1 < data.size()) ? table[(n >> 6) & 0x3F] : '=';
        out += (i + 2 < data.size()) ? table[n & 0x3F] : '=';
    }
    return out;
}

}  // namespace edgegw
