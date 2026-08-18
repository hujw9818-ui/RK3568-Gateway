// ======================================================================
// zigbee_serial.cpp - DL-30 透传串口实现 (Linux termios)
// ======================================================================
#include "serial/zigbee_serial.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace edgegw {
namespace serial {

bool ZigbeeSerial::Open(const std::string& device, int baudrate) {
    if (fd_ >= 0) return true;   // 已打开

    fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        std::fprintf(stderr, "[zigbee] 打开串口失败: %s (%s)\n",
                     device.c_str(), std::strerror(errno));
        return false;
    }

    struct termios tio;
    std::memset(&tio, 0, sizeof(tio));
    if (tcgetattr(fd_, &tio) != 0) {
        std::fprintf(stderr, "[zigbee] tcgetattr 失败: %s\n",
                     std::strerror(errno));
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    // 波特率
    speed_t speed = B115200;
    switch (baudrate) {
        case 9600:   speed = B9600;   break;
        case 19200:  speed = B19200;  break;
        case 38400:  speed = B38400;  break;
        case 57600:  speed = B57600;  break;
        case 115200: speed = B115200; break;
        default:     speed = B115200; break;
    }
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);

    // 8N1 raw 模式
    tio.c_cflag &= ~PARENB;   // 无校验
    tio.c_cflag &= ~CSTOPB;   // 1 停止位
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;       // 8 数据位
    tio.c_cflag &= ~CRTSCTS;  // 无硬件流控
    tio.c_cflag |= CREAD | CLOCAL;

    tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);   // 原始模式
    tio.c_iflag &= ~(IXON | IXOFF | IXANY);           // 无软件流控
    tio.c_oflag &= ~OPOST;

    tio.c_cc[VMIN] = 1;    // 至少 1 字节可读即返回
    tio.c_cc[VTIME] = 0;

    tcflush(fd_, TCIOFLUSH);
    if (tcsetattr(fd_, TCSANOW, &tio) != 0) {
        std::fprintf(stderr, "[zigbee] tcsetattr 失败: %s\n",
                     std::strerror(errno));
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    std::printf("[zigbee] 串口已打开: %s @%d\n", device.c_str(), baudrate);
    return true;
}

void ZigbeeSerial::Close() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    std::printf("[zigbee] 串口已关闭\n");
}

bool ZigbeeSerial::Send(const std::string& line) {
    if (fd_ < 0) return false;
    const std::string data = line + "\r\n";
    const ssize_t n = ::write(fd_, data.data(), data.size());
    return n == static_cast<ssize_t>(data.size());
}

void ZigbeeSerial::StartReadLoop(LineCallback cb) {
    if (running_) return;
    running_ = true;
    thread_ = std::thread([this, cb]() {
        std::string buf;
        char tmp[256];
        while (running_) {
            const ssize_t n = ::read(fd_, tmp, sizeof(tmp));
            if (n > 0) {
                buf.append(tmp, static_cast<size_t>(n));
                // 按 \n 切行, 去 \r
                size_t pos = 0;
                while ((pos = buf.find('\n')) != std::string::npos) {
                    std::string line = buf.substr(0, pos);
                    buf.erase(0, pos + 1);
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    if (!line.empty() && cb) cb(line);
                }
                // 防御: 单行过长 (>4KB 无换行) 丢弃
                if (buf.size() > 4096) buf.clear();
            } else if (n == 0) {
                // 无数据 (NONBLOCK), 稍等
                usleep(20000);
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                // 读错误 (设备拔出等), 退出循环
                std::fprintf(stderr, "[zigbee] 串口读错误: %s\n",
                             std::strerror(errno));
                break;
            } else {
                usleep(20000);
            }
        }
    });
}

}  // namespace serial
}  // namespace edgegw
