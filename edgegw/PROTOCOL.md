# 边缘网关 MQTT 与 JSON 通信协议

## 1. 文档信息

| 项目 | 内容 |
|---|---|
| 协议名称 | IoT Edge Gateway Device Protocol |
| 协议版本 | `v1.0` |
| 适用设备 | RK3568 网关、ESP8266 单片机、Web、Qt |
| 传输协议 | MQTT over TCP |
| 字符编码 | UTF-8 |
| JSON 数字格式 | 温度、湿度等使用 JSON number，不要传数字字符串 |

> 本文档是网关和 ESP8266 单片机的共同接口约定。单片机、网关、Web、Qt 必须使用相同的字段名称和取值范围。

## 2. MQTT Topic 约定

Topic 统一使用 `iotgw/v1` 前缀，便于将来升级协议版本。

```text
iotgw/v1/dev/{device_id}/report   # 单片机 -> 网关：传感器数据、执行器状态
iotgw/v1/dev/{device_id}/cmd      # 网关 -> 单片机：控制命令
iotgw/v1/dev/{device_id}/ack      # 单片机 -> 网关：命令执行回执
iotgw/v1/dev/{device_id}/status   # 单片机 -> 网关：在线/离线状态
```

当前设备 ID：

```text
mcu01
```

网关订阅：

```text
iotgw/v1/dev/+/report
```

网关向指定设备下发命令时使用：

```text
iotgw/v1/dev/mcu01/cmd
```

Topic 中的 `{device_id}` 必须与 JSON 中的 `dev` 字段一致；不一致时网关应记录错误并丢弃消息。

## 3. 通用 JSON 外壳

除在线状态外，所有消息统一使用以下外壳：

```json
{
  "type": "sensor",
  "dev": "mcu01",
  "msg_id": "msg-1001",
  "req_id": "",
  "ts": 1754557200,
  "body": {}
}
```

字段说明：

| 字段 | 必填 | 类型 | 说明 |
|---|---:|---|---|
| `type` | 是 | string | `sensor`、`status`、`cmd`、`ack` |
| `dev` | 是 | string | 设备 ID，例如 `mcu01` |
| `msg_id` | 是 | string | 当前消息唯一 ID，不能重复 |
| `req_id` | 否 | string | ACK 对应的原命令 `msg_id` |
| `ts` | 是 | number | Unix 时间戳，单位为秒；没有 RTC 时可先传 `0` |
| `body` | 是 | object | 消息具体内容 |

### 3.1 ID 规则

- `msg_id` 由发送方生成，例如 `mcu01-telemetry-0001`。
- 网关下发命令时生成 `cmd-时间戳-序号`。
- 单片机回复 ACK 时，`req_id` 必须等于收到的命令 `msg_id`。
- 网关可以使用 `req_id` 匹配命令是否成功。

## 4. 单片机传感器上报

### Topic

```text
iotgw/v1/dev/mcu01/report
```

### JSON

```json
{
  "type": "sensor",
  "dev": "mcu01",
  "msg_id": "mcu01-report-0001",
  "ts": 1754557200,
  "body": {
    "data": {
      "temp": 25.6,
      "humi": 60.1,
      "light": 320,
      "ir": 0
    }
  }
}
```

传感器字段：

| 字段 | 类型 | 说明 |
|---|---|---|
| `temp` | number | 温度，单位摄氏度 |
| `humi` | number | 湿度，单位百分比 |
| `light` | number | 光照值，可使用 ADC 原始值或 lux，但全组必须统一 |
| `ir` | number | 红外检测值，建议 `0` 表示无人、`1` 表示有人 |

暂时没有数据的字段可以不传，但不能传空字符串。例如：

```json
{
  "data": {
    "temp": 25.6,
    "humi": 60.1
  }
}
```

## 5. 执行器状态上报

执行器执行命令后，单片机应将最新状态通过 `report` 上报：

```json
{
  "type": "status",
  "dev": "mcu01",
  "msg_id": "mcu01-status-0001",
  "ts": 1754557201,
  "body": {
    "data": {
      "fan_on": 1,
      "fan_speed": 80,
      "fan_dir": 0,
      "led_on": 1,
      "led_brightness": 70,
      "servo_angle": 90,
      "beep_on": 0
    }
  }
}
```

字段范围：

| 字段 | 范围 | 说明 |
|---|---|---|
| `fan_on` | `0/1` | 风扇开关 |
| `fan_speed` | `0-100` | 风扇速度百分比 |
| `fan_dir` | `0/1` | 电机方向，具体正反方向由接线确定 |
| `led_on` | `0/1` | LED 开关 |
| `led_brightness` | `0-100` | LED 亮度百分比 |
| `servo_angle` | `0-180` | 舵机角度 |
| `beep_on` | `0/1` | 蜂鸣器开关 |

## 6. 网关下发控制命令

### Topic

```text
iotgw/v1/dev/mcu01/cmd
```

### 风扇命令

```json
{
  "type": "cmd",
  "dev": "mcu01",
  "msg_id": "cmd-2001",
  "ts": 1754557200,
  "body": {
    "target": "fan",
    "action": "set",
    "params": {
      "on": 1,
      "speed": 80,
      "dir": 0
    }
  }
}
```

### LED 命令

```json
{
  "type": "cmd",
  "dev": "mcu01",
  "msg_id": "cmd-2002",
  "ts": 1754557200,
  "body": {
    "target": "led",
    "action": "set",
    "params": {
      "on": 1,
      "brightness": 70
    }
  }
}
```

### 舵机命令

```json
{
  "type": "cmd",
  "dev": "mcu01",
  "msg_id": "cmd-2003",
  "ts": 1754557200,
  "body": {
    "target": "servo",
    "action": "set",
    "params": {
      "angle": 90
    }
  }
}
```

### 蜂鸣器命令

```json
{
  "type": "cmd",
  "dev": "mcu01",
  "msg_id": "cmd-2004",
  "ts": 1754557200,
  "body": {
    "target": "beep",
    "action": "set",
    "params": {
      "on": 1
    }
  }
}
```

当前命令对象：

| `target` | `action` | 参数 |
|---|---|---|
| `fan` | `set` | `on`、`speed`、`dir` |
| `led` | `set` | `on`、`brightness` |
| `servo` | `set` | `angle` |
| `beep` | `set` | `on` |

单片机必须校验参数范围，非法参数不能直接执行。

## 7. 命令 ACK

### 成功 ACK

```json
{
  "type": "ack",
  "dev": "mcu01",
  "msg_id": "ack-3001",
  "req_id": "cmd-2001",
  "ts": 1754557201,
  "body": {
    "ok": true,
    "code": 0,
    "message": "ok"
  }
}
```

### 失败 ACK

```json
{
  "type": "ack",
  "dev": "mcu01",
  "msg_id": "ack-3002",
  "req_id": "cmd-2001",
  "ts": 1754557201,
  "body": {
    "ok": false,
    "code": 1001,
    "message": "speed_out_of_range"
  }
}
```

错误码：

| 错误码 | 含义 |
|---:|---|
| `0` | 成功 |
| `1001` | 参数超出范围 |
| `1002` | 不支持的设备目标 |
| `1003` | 不支持的操作 |
| `1004` | 外设执行失败 |
| `1005` | JSON 格式错误 |

## 8. 在线状态与遗嘱消息

### 在线状态

单片机成功连接 MQTT 后，发布以下保留消息：

```text
Topic: iotgw/v1/dev/mcu01/status
QoS: 1
Retain: true
```

```json
{
  "type": "status",
  "dev": "mcu01",
  "msg_id": "mcu01-online-0001",
  "ts": 1754557200,
  "body": {
    "online": true
  }
}
```

### 遗嘱消息

建立 MQTT 连接时设置遗嘱：

```text
Topic: iotgw/v1/dev/mcu01/status
QoS: 1
Retain: true
```

```json
{
  "type": "status",
  "dev": "mcu01",
  "msg_id": "mcu01-offline",
  "ts": 0,
  "body": {
    "online": false
  }
}
```

## 9. QoS 和 Retain 约定

| 消息 | QoS | Retain | 原因 |
|---|---:|---:|---|
| `report` 传感器数据 | `0` | `false` | 周期数据，丢一条不影响最新状态 |
| `cmd` 控制命令 | `1` | `false` | 命令至少送达一次，不能保留旧命令 |
| `ack` 命令回执 | `1` | `false` | 网关需要可靠收到结果 |
| `status` 在线状态 | `1` | `true` | 新订阅者能立即知道设备状态 |

## 10. 双方实现职责

### ESP8266 单片机端

- 连接 RK3568 上的 Mosquitto Broker。
- 订阅 `iotgw/v1/dev/mcu01/cmd`。
- 周期发布 `report` 传感器数据。
- 解析 `cmd`，校验参数并控制外设。
- 每条命令发布一个 `ack`。
- 命令执行后发布最新的执行器 `status`。
- 连接 MQTT 时发布 online，并设置 offline 遗嘱。
- MQTT 断线后自动重连。

### RK3568 网关端

- 启动本地 Mosquitto Broker。
- 订阅设备 `report`、`ack`、`status`。
- 解析并校验 JSON，检查 Topic 中的设备 ID 与 `dev` 是否一致。
- 将最新设备状态保存到内存设备表。
- 将传感器历史数据写入 SQLite。
- Web/Qt 下发控制命令时生成 `msg_id` 并发布 `cmd`。
- 根据 `req_id` 匹配 ACK，并通过 WebSocket 推送给 Web/Qt。

## 11. 本机模拟测试

### 查看所有消息

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 \
  -t 'iotgw/v1/dev/#' -v
```

### 模拟 ESP8266 上报温湿度

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 \
  -t 'iotgw/v1/dev/mcu01/report' \
  -m '{"type":"sensor","dev":"mcu01","msg_id":"mock-001","ts":1754557200,"body":{"data":{"temp":25.6,"humi":60.1,"light":320,"ir":0}}}'
```

### 模拟网关下发风扇命令

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 \
  -t 'iotgw/v1/dev/mcu01/cmd' \
  -m '{"type":"cmd","dev":"mcu01","msg_id":"cmd-2001","ts":1754557200,"body":{"target":"fan","action":"set","params":{"on":1,"speed":80,"dir":0}}}'
```

## 12. 当前暂不实现

以下字段或功能暂时保留，不进入 v1.0 主链路：

- `ai`
- `query`
- OTA 升级
- 鉴权
- 规则引擎

如需增加功能，先更新本文档版本，再修改单片机和网关代码，避免双方各自扩展导致格式不一致。
