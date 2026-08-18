# RK3568 Edge Gateway

> 面向 RK3568 的多协议物联网边缘网关：连接 MQTT 与 Zigbee 设备，提供 Web/Qt 控制端、实时状态同步、摄像头推流与本地数据持久化。

## 项目简介

RK3568 Edge Gateway 是运行于 RK3568 Linux/Buildroot 平台的 C++14 边缘网关。系统接收下位机通过 MQTT 或 DL-30 Zigbee 透传串口上报的传感器与执行器状态，将遥测数据写入 SQLite，并通过 HTTP API、WebSocket、浏览器控制台和 Qt 上位机统一展示与控制。

目前围绕 `mcu01` 完成了温湿度、光照、红外检测、LED、舵机、蜂鸣器和 USB 摄像头的端到端联动。红外由未遮挡变为遮挡时，网关会通过当前通信通道自动开启蜂鸣器；遮挡解除时自动关闭。

## 功能特性

- MQTT 与 Zigbee 双通道接入，可由 Web/Qt 动态切换控制通道
- 温度、湿度、光照和红外数据采集与实时展示
- LED 开关/亮度、舵机角度和蜂鸣器控制
- 基于 `dev + msg_id` 的双通道传感器消息去重
- SQLite 遥测数据持久化
- WebSocket 状态广播，实现多个控制端同步
- 响应式 Web 控制台和 Qt 5 全屏上位机
- USB 摄像头 MJPEG 预览、抓拍与 H.264/MKV 录像
- 红外遮挡自动报警
- Host 本机构建与 RK3568 AArch64 交叉编译

## 系统架构

```text
                       ┌──────────────────┐
                       │ Web 控制台 :8080 │
                       └────────┬─────────┘
                                │ HTTP / MJPEG
┌─────────────┐  MQTT   ┌───────▼────────┐   WebSocket :8082   ┌─────────────┐
│ ESP/STM32   ├────────►│  RK3568 edgegw ├────────────────────►│ Web / Qt    │
│ 传感器与外设│◄────────┤                │                     │ 状态同步    │
└──────┬──────┘ command └───┬───────┬────┘                     └─────────────┘
       │                     │       │
       └──── Zigbee UART ────┘       ├── SQLite 遥测数据
                                     └── USB Camera / GStreamer
```

数据流：

```text
MQTT 或 Zigbee 上报
  → JSON 解析
  → 双通道消息去重
  → 更新内存设备状态
  → 传感器数据写入 SQLite
  → 红外报警判断
  → WebSocket 广播
```

## 技术栈

| 模块 | 技术 |
|---|---|
| 网关核心 | C++14、CMake、POSIX Threads |
| HTTP / WebSocket | Mongoose 7.15 |
| 消息通信 | Mosquitto MQTT、DL-30 Zigbee 串口透传 |
| 配置 / JSON | yaml-cpp、RapidJSON |
| 存储 / 日志 | SQLite3、spdlog、fmt |
| 摄像头 | V4L2、GStreamer、Rockchip MPP |
| 客户端 | 原生 Web、Qt 5 Widgets / Network |
| 目标平台 | RK3568、AArch64、Buildroot Linux |

## 源码与交付范围

本项目由两个位置共同组成：

| 位置 | 内容 | 定位 |
|---|---|---|
| WSL `/home/ubuntu/RK3568-Gateway` | `edgegw` 网关服务、Web 页面、Qt 客户端 | 当前 Git 主仓库与主要开发版本 |
| Windows `边缘网关项目/` | STM32 固件、Buildroot 镜像、部署脚本、项目资料及网关备份 | 完整课程交付与硬件侧工程 |

Windows 中的 `RK3568-Gateway-备份` 只保存了 Qt 客户端和部署脚本，并不是 WSL Git 仓库的完整镜像；网关源码应以 WSL 仓库为准。下位机源码则以 Windows 的 `mqtt+zigbee/mqtt+zigbee` Keil 工程为准。

## 完整项目结构

```text
边缘网关项目/                         # Windows 完整交付目录
├── mqtt+zigbee/mqtt+zigbee/         # STM32F103C8 Keil 工程
│   ├── User/main.c                  # 采集、双通道通信、命令执行
│   ├── Hardware/
│   │   ├── aht30.*                  # 温湿度
│   │   ├── adc.*                    # 光敏 ADC 与红外输入
│   │   ├── esp8266.*                # Wi-Fi AT 与 TCP 收发
│   │   ├── mqtt.*                   # MQTT 报文组装
│   │   ├── servo.*                  # 舵机 PWM
│   │   └── zigbee.*                 # DL-30 串口
│   └── Project.uvprojx
├── scripts/                         # Buildroot 开机脚本
├── 固件/
│   ├── boot.img
│   └── update.img
├── RK3568-Gateway-备份/             # Qt 与脚本备份，不是完整主仓库
├── 老师所发资料/                    # 需求、DL-30 资料、进度模板
└── *.md                             # 协议、计划、协作、烧录与答辩资料

/home/ubuntu/RK3568-Gateway/          # WSL Git 主仓库
├── edgegw/
│   ├── cmake/rk3568-toolchain.cmake
│   ├── config/
│   ├── docs/
│   ├── src/
│   │   ├── camera/                  # 推流、抓拍、录像
│   │   ├── config/                  # YAML 配置
│   │   ├── database/                # SQLite
│   │   ├── device/                  # JSON 与设备状态
│   │   ├── logger/
│   │   ├── mqtt/
│   │   ├── serial/
│   │   └── web/                     # HTTP 与 WebSocket
│   ├── third_party/mongoose/
│   ├── www/
│   ├── CMakeLists.txt
│   └── PROTOCOL.md
└── qt-client/
```



## 快速开始

### 环境要求

Host Linux/WSL 调试环境：

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake pkg-config \
  libmosquitto-dev libyaml-cpp-dev libsqlite3-dev \
  libspdlog-dev libfmt-dev rapidjson-dev \
  mosquitto mosquitto-clients
```

摄像头功能还需要 V4L2、GStreamer 1.0 以及 Rockchip MPP 插件，至少应提供 `v4l2src`、`mppjpegenc`、`mpph264enc`、`h264parse` 和 `matroskamux`。构建 Qt 客户端需要 Qt 5 和 qmake。

### 配置

```bash
cd edgegw
cp config/development.example.yaml config/development.yaml
```

推荐配置：

```yaml
name: edgegw
env: development

mqtt:
  host: 127.0.0.1
  port: 1883
  client_id: edgegw-dev
  clean_session: true

web:
  host: 0.0.0.0
  port: 8080
  ws_port: 8082

serial:
  device: /dev/ttyS3
  baudrate: 115200

database:
  path: data/edgegw.db

logging:
  level: info
  file: data/logs/edgegw.log

data:
  dir: data
```

代码会读取 `database.path`、`logging.file`、`data.dir` 和 `web.ws_port`；缺省值即为上例值。

### Host 构建

```bash
cd edgegw
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host -j
./build/host/edgegw config/development.yaml
```

启动后：

- Web：`http://<gateway-ip>:8080/`
- HTTP API：`http://<gateway-ip>:8080/api/...`
- WebSocket：`ws://<gateway-ip>:8082/`

### RK3568 交叉编译

工具链文件默认使用 `/home/ubuntu/rk356x_linux/buildroot/output`。SDK 位于其他目录时，先修改 `BUILDROOT_DIR`。

```bash
cd edgegw
cmake -S . -B build/arm64 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/rk3568-toolchain.cmake
cmake --build build/arm64 -j
```

部署时需携带可执行文件、`config/` 和 `www/`。程序应从 `edgegw/` 目录运行，或保证当前工作目录中存在 `www/`。

### Qt 客户端

```bash
cd qt-client
qmake qt-client.pro
make -j

./qt-client                 # 与网关同机
./qt-client 192.168.5.80    # 从 PC 连接目标板
```

Qt 客户端固定访问 HTTP 8080 端口，当前通过 1 秒轮询同步状态，视频使用 MJPEG 流。

## STM32 下位机固件

Windows 目录中的 `mqtt+zigbee/mqtt+zigbee` 是 STM32F103C8（Cortex-M3）的 Keil MDK 工程，使用 STM32 标准外设库。固件每 3 秒采集一次 AHT30 温湿度、光敏 ADC 和红外输入，并将同一 JSON 同时经 ESP8266/MQTT 与 DL-30/Zigbee 发送。

### 固件行为

- 启动时复位 ESP8266，连接 Wi-Fi 与 MQTT Broker
- 发布在线状态，并设置 MQTT Last Will 离线消息
- 订阅 `iotgw/v1/dev/mcu01/cmd`
- MQTT 与 Zigbee 双通道上报传感器数据
- 同时轮询 MQTT `+IPD` 和 Zigbee 串口命令
- 执行 LED、舵机和蜂鸣器命令，随后双通道返回 ACK 与最新状态
- MQTT 连续发送失败 3 次后重连；连续重连失败 3 次后软复位 ESP8266
- MQTT 不可用时不阻塞主循环，Zigbee 通道仍可工作

当前 ACK 中的 `req_id` 仍为空字符串，尚未把原命令 `msg_id` 回填，因此调用端不能依赖 `req_id` 做严格请求匹配。

### 引脚与外设

| 功能 | STM32 资源 | 说明 |
|---|---|---|
| ESP8266 | USART1：PA9 TX、PA10 RX | 115200 baud |
| DL-30 Zigbee | USART3：PB10 TX、PB11 RX | 115200 baud、按行 JSON |
| AHT30 | PB6 SCL、PB7 SDA | 软件 I²C，地址 0x38 |
| 光敏传感器 | PA1 / ADC1 Channel 1 | 12 位 ADC |
| 红外传感器 | PA2 | 数字输入，高电平记为 `ir=1` |
| LED | PA3 / TIM2 CH4 | 1 kHz PWM，0-100% |
| 蜂鸣器 | PB5 | 低电平触发 |
| 舵机 | PA6 / TIM3 CH1 | 0-180° PWM |

工程目录仍有 `motor.c/.h` 残留，但 `Project.uvprojx` 已不再编译电机模块，`main.c` 也没有调用电机逻辑；这与当前移除直流电机功能的决策一致。公开仓库前建议删除这些残留文件或移到 `archive/`，避免误导。

### 编译与烧录

1. 使用 Keil MDK 打开：

```text
mqtt+zigbee/mqtt+zigbee/Project.uvprojx
```

2. 选择 `STM32F103C8` Target 并编译。
3. 使用 ST-Link 将生成的固件烧录到 STM32F103C8。
4. 烧录前先将 `main.c` 中的 Wi-Fi 和 Broker 参数改为部署环境值，且不要把真实凭据提交到公开仓库。

## RK3568 部署与开机启动

Windows `scripts/` 提供了完整的 Buildroot SysV 启动链：

| 脚本 | 作用 |
|---|---|
| `S49rotate` | 写入 Rockchip Weston 270° 屏幕旋转配置 |
| `S50launcher` | 启动 Weston/Wayland 桌面 |
| `S98wifi` | 等待 RTL8723DU `wlan0`，连接热点并保持 eth0 默认路由 |
| `S99edgegw` | 先启动 Mosquitto，再从 `/opt/edgegw` 启动网关 |
| `S99qtclient` | 配置 Wayland 环境并全屏启动 Qt 客户端 |

建议部署布局：

```text
/opt/edgegw/
├── edgegw
├── qt-client
├── development.yaml
├── www/
└── data/
```

Mosquitto 配置由 `/etc/mosquitto/edgegw.conf` 提供。启动脚本应复制到目标板 `/etc/init.d/` 并赋予执行权限：

```bash
chmod +x /etc/init.d/S49rotate /etc/init.d/S50launcher \
  /etc/init.d/S98wifi /etc/init.d/S99edgegw /etc/init.d/S99qtclient
```

Windows `固件/` 中还保存了 RK3568 镜像：

| 文件 | 大小（约） | 用途 |
|---|---:|---|
| `boot.img` | 24.8 MiB | 内核/启动分区镜像 |
| `update.img` | 1.60 GiB | Rockchip 完整升级镜像 |

镜像烧录具有设备和分区风险，必须确认开发板型号、Loader 模式及镜像来源后再操作。详细流程见 `RK3568系统编译打包烧录实战记录.md`。



## HTTP API

### 基本约定

- Base URL：`http://<gateway-ip>:8080`
- 普通接口使用 JSON；MJPEG/JPEG 接口除外
- 当前没有身份认证或 TLS，仅建议在可信局域网使用
- 控制命令固定发送至 `mcu01`
- `POST /api/control` 按当前 transport 通过 MQTT 或 Zigbee 下发

### 接口总览

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/health` | 健康检查 |
| GET | `/api/status` | 获取全部设备最新状态 |
| POST | `/api/control` | 下发控制命令 |
| GET | `/api/transport` | 查询通信通道 |
| POST | `/api/transport` | 切换 MQTT/Zigbee |
| GET | `/api/camera/status` | 摄像头状态 |
| POST | `/api/camera/start` | 启动推流 |
| POST | `/api/camera/stop` | 停止推流 |
| GET | `/api/camera/stream` | MJPEG 实时视频流 |
| GET | `/api/camera/last_photo` | 最新 JPEG 帧 |
| POST | `/api/camera/snapshot` | 抓拍并返回 Base64 JPEG |
| POST | `/api/camera/record/start` | 开始 H.264/MKV 录像 |
| POST | `/api/camera/record/stop` | 停止录像并恢复推流 |

### 健康检查

```bash
curl http://192.168.5.80:8080/api/health
```

```json
{"status":"ok"}
```

### 查询设备状态

```http
GET /api/status
```

```json
{
  "devices": [{
    "dev_id": "mcu01",
    "online": true,
    "temp": 25.6,
    "humi": 60.1,
    "light": 320,
    "ir": 0,
    "led_on": true,
    "led_brightness": 70,
    "fan_on": false,
    "fan_speed": 0,
    "fan_dir": 0,
    "servo_angle": 90,
    "beep_on": false
  }]
}
```

状态表会输出全部字段；未上报字段可能仍为默认值。`fan_*` 是后端兼容字段，当前 Web/Qt 和现行硬件已移除直流电机。

### 下发控制命令

```http
POST /api/control
Content-Type: application/json
```

通用结构：

```json
{
  "type": "cmd",
  "dev": "mcu01",
  "msg_id": "cmd-1754557200-1",
  "ts": 1754557200,
  "body": {
    "target": "led",
    "action": "set",
    "params": {}
  }
}
```

成功返回 `{"ok":true}`。发送成功后，网关会乐观更新内存状态并广播；设备真实执行结果应以后续 `ack` 或 `status` 为准。

#### LED

```bash
curl -X POST http://192.168.5.80:8080/api/control \
  -H 'Content-Type: application/json' \
  -d '{"type":"cmd","dev":"mcu01","msg_id":"cmd-led-001","ts":1754557200,"body":{"target":"led","action":"set","params":{"on":1,"brightness":70}}}'
```

| 参数 | 类型 | 范围 | 说明 |
|---|---|---|---|
| `on` | integer | 0/1 | LED 开关 |
| `brightness` | integer | 0-100 | 亮度百分比 |

#### 舵机

```json
{
  "type":"cmd",
  "dev":"mcu01",
  "msg_id":"cmd-servo-001",
  "ts":1754557200,
  "body":{"target":"servo","action":"set","params":{"angle":90}}
}
```

`angle` 范围为 0-180 度。

#### 蜂鸣器

```json
{
  "type":"cmd",
  "dev":"mcu01",
  "msg_id":"cmd-beep-001",
  "ts":1754557200,
  "body":{"target":"beep","action":"set","params":{"on":1}}
}
```

`on` 为 1 时开启，为 0 时关闭。

#### 风扇兼容命令

后端仍兼容 `target=fan`，参数为 `on`、`speed`、`dir`。当前项目因直流电机电气干扰已移除相关硬件和 Web/Qt UI，不建议在现行硬件上使用。若恢复，应先完善独立供电、续流/抑制和接地设计。

### 查询与切换通信通道

```bash
curl http://192.168.5.80:8080/api/transport

curl -X POST http://192.168.5.80:8080/api/transport \
  -H 'Content-Type: application/json' \
  -d '{"transport":"zigbee"}'
```

响应：

```json
{"ok":true,"transport":"zigbee"}
```

可选值为 `mqtt`、`zigbee`，结果会通过 WebSocket 广播。当前实现不会在切换时验证 Zigbee 串口；若串口不可用，后续控制会失败。

### 摄像头

状态：

```bash
curl http://192.168.5.80:8080/api/camera/status
```

```json
{
  "streaming": true,
  "recording": false,
  "frame_ready": true,
  "record_file": "",
  "last_snapshot": "data/media/snapshot_20260818_120000.jpg"
}
```

推流：

```bash
curl -X POST http://192.168.5.80:8080/api/camera/start
curl -X POST http://192.168.5.80:8080/api/camera/stop
```

浏览器或播放器打开 `http://192.168.5.80:8080/api/camera/stream`，返回 `multipart/x-mixed-replace`。访问时会自动启动摄像头；最后一个客户端断开后自动停流。

`GET /api/camera/last_photo` 返回最新 JPEG；没有帧时返回 404。

抓拍：

```bash
curl -X POST http://192.168.5.80:8080/api/camera/snapshot
```

```json
{"ok":true,"data":"<base64-encoded-jpeg>"}
```

前端可拼成 `data:image/jpeg;base64,<data>`。文件同时保存至 `data/media/`。

录像：

```bash
curl -X POST http://192.168.5.80:8080/api/camera/record/start
curl -X POST http://192.168.5.80:8080/api/camera/record/stop
```

开始响应示例：

```json
{"ok":true,"path":"data/media/record_20260818_120000.mkv"}
```

录像需要独占 RKISP 主通道：开始录像会暂停 MJPEG，停止录像后恢复推流。录像采用 MPP H.264 并封装为 MKV。

## WebSocket API

- 地址：`ws://<gateway-ip>:8082/`
- 网关单向推送 UTF-8 JSON 文本帧
- 客户端上行消息当前被忽略

状态变化时推送内容与 `GET /api/status` 相同；通信通道改变时推送：

```json
{"transport":"zigbee"}
```

待发队列最多 256 条；生产快于消费时丢弃最旧消息，防止内存增长。

## MQTT / Zigbee 设备协议

| 方向 | Topic | 用途 |
|---|---|---|
| 设备 → 网关 | `iotgw/v1/dev/{device_id}/report` | 传感器/执行器状态 |
| 网关 → 设备 | `iotgw/v1/dev/{device_id}/cmd` | 控制命令 |
| 设备 → 网关 | `iotgw/v1/dev/{device_id}/ack` | 命令回执 |
| 设备 → 网关 | `iotgw/v1/dev/{device_id}/status` | 在线状态 |

网关订阅 `report`、`ack`、`status` 的通配设备 Topic，但 HTTP 控制固定发往 `mcu01`。Zigbee 使用相同 JSON，每条消息以 `\r\n` 结束，默认 `/dev/ttyS3`、115200 8N1。

传感器上报：

```json
{
  "type":"sensor",
  "dev":"mcu01",
  "msg_id":"mcu01-report-0001",
  "ts":1754557200,
  "body":{"data":{"temp":25.6,"humi":60.1,"light":320,"ir":0}}
}
```

| 字段 | 说明 |
|---|---|
| `temp` | 摄氏温度 |
| `humi` | 相对湿度百分比 |
| `light` | ADC 原始值；Web/Qt 显示为 `4095 - light` |
| `ir` | 0 未遮挡，1 遮挡并触发报警 |

状态上报：

```json
{
  "type":"status",
  "dev":"mcu01",
  "msg_id":"mcu01-status-0001",
  "ts":1754557201,
  "body":{"data":{"led_on":1,"led_brightness":70,"servo_angle":90,"beep_on":0}}
}
```

ACK：

```json
{
  "type":"ack",
  "dev":"mcu01",
  "msg_id":"mcu01-ack-0001",
  "req_id":"cmd-led-001",
  "ts":1754557202,
  "body":{"ok":true,"code":0,"message":"success"}
}
```

`ok` 兼容布尔值和数字 0/1。完整约定见 `edgegw/PROTOCOL.md`。

## 数据存储

只有 `type=sensor` 写入 SQLite；ACK 和状态不落库。默认数据库为 `data/edgegw.db`。

```sql
CREATE TABLE IF NOT EXISTS telemetry (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  dev_id TEXT NOT NULL,
  temp REAL,
  humi REAL,
  light REAL,
  ir REAL,
  ts INTEGER NOT NULL,
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

默认运行数据：

```text
edgegw/data/
├── edgegw.db
├── logs/edgegw.log
├── media/                 # 抓拍与 MKV 录像
└── runtime/
    ├── latest.jpg
    └── latest_snap.jpg
```

## 常见问题

### 没有设备数据

检查 Broker 地址和 Topic，可观察所有设备消息：

```bash
mosquitto_sub -h 127.0.0.1 -t 'iotgw/v1/dev/#' -v
```

### Zigbee 控制失败

检查 `/dev/ttyS3`、串口权限、115200 波特率和消息换行符。

### 摄像头失败

```bash
ls -l /dev/video0
gst-inspect-1.0 v4l2src
gst-inspect-1.0 mppjpegenc
gst-inspect-1.0 mpph264enc
```

### Qt 无法连接

参数只填写 IP，不包含协议或端口，并确认 8080 可达：

```bash
./qt-client 192.168.5.80
```

## 当前限制

- HTTP、WebSocket、MQTT 未启用认证或 TLS
- HTTP 控制目标固定为 `mcu01`
- WebSocket 只提供服务端推送
- SQLite 暂无历史查询 API
- Qt 状态同步仍使用轮询
- 尚无容器化环境、CI 或自动化测试
- 直流电机已移除，后端仅保留兼容字段

## 公开发布前的安全检查

当前 Windows 交付目录内已经发现真实 Wi-Fi 信息和现场网络地址。公开上传前必须处理：

1. **替换 `mqtt+zigbee/mqtt+zigbee/User/main.c` 中的 `WIFI_SSID`、`WIFI_PASS` 和 `BROKER_HOST`。**
2. **替换 Windows `scripts/S98wifi`、WSL `edgegw/scripts/S98wifi` 及备份脚本中的真实热点 SSID 和密码。**
3. 不要提交含真实地址、账号、令牌或现场信息的 `development.yaml`。
4. 检查 `docs/daily/`、协作指南、PPT 素材和烧录记录中的人员、IP、账号及设备信息。
5. 为 HTTP/WebSocket 控制接口增加认证，或只允许可信管理网段访问。
6. 跨不可信网络时，为 HTTP、WebSocket 和 MQTT 配置 TLS。
7. 从公开源码仓库排除 `固件/*.img`、Keil 用户文件（`*.uvguix.*`）、调试数据库、编译产物、数据库、日志和媒体文件。
8. 明确 `老师所发资料/` 的版权和再分发许可；未经授权不要随源码公开 PDF、视频和课程模板。
9. 为仓库添加正式的开源许可证，并核对 Mongoose 等第三方依赖的许可证兼容性。



## 路线图

- [ ] HTTP/WebSocket 认证与 TLS
- [ ] 多设备控制与设备发现
- [ ] 遥测历史查询、筛选和导出 API
- [ ] Qt 原生 WebSocket 同步
- [ ] 敏感配置外置与配置校验
- [ ] 单元测试、集成测试和 CI
- [ ] 完善 Buildroot 服务管理

## License

当前仓库尚未提供开源许可证。公开发布前应按实际用途选择并添加许可证（如 MIT、Apache-2.0 或 GPL-3.0）；在许可证确定前默认保留全部权利。

## 致谢

- [Mongoose](https://mongoose.ws/)
- [Eclipse Mosquitto](https://mosquitto.org/)
- [SQLite](https://www.sqlite.org/)
- [GStreamer](https://gstreamer.freedesktop.org/)
- [Qt](https://www.qt.io/)

---

下位机完整消息格式与联调流程请继续阅读 `edgegw/PROTOCOL.md` 和 `edgegw/docs/ESP8266对接指南.md`。


