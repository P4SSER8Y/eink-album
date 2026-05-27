# E-Ink Album — 电子墨水屏相册

基于 ESP32 + 7.3" 7 色电子墨水屏的 DIY 相册。支持 HTTP 直接上传图片、MQTT 对接 HomeAssistant（自动发现设备状态）。配套 `show.py` 命令行工具将任意图片转换为墨水屏格式并发送。

## 硬件配置

| 组件 | 型号 |
|------|------|
| 主控 | ESP32-S3 DevKitC-1（推荐）或 ESP32-C3 Super Mini |
| 屏幕 | Waveshare 7.3" 7 色电子墨水屏 (AC073TC1)，分辨率 800×480 |
| 指示灯 | ESP32-S3：WS2812 RGB LED；ESP32-C3：普通 GPIO LED |

### 引脚定义

ESP32-S3 和 ESP32-C3 的墨水屏引脚定义相同：

| 墨水屏引脚 | ESP32 引脚 | 说明 |
|------------|------------|------|
| VCC        | 3.3V       | 电源正 |
| GND        | GND        | 电源地 |
| SCK / CLK  | GPIO5      | SPI 时钟 |
| MOSI / DIN | GPIO4      | SPI 数据（主机输出） |
| CS         | GPIO6      | 片选（低有效） |
| DC         | GPIO7      | 数据/命令选择 |
| RST        | GPIO15     | 复位（低有效） |
| BUSY       | GPIO16     | 忙信号检测（低有效） |
| PWR        | GPIO3      | 屏幕电源使能（高有效） |

> 墨水屏没有 MISO 线，`PIN_DUMMY` (GPIO12) 作为 SPI 占位，实际不连接。

**LED 引脚（两板不同）：**

| 开发板 | LED 引脚 | 类型 |
|--------|----------|------|
| ESP32-S3 | GPIO48 | WS2812 (可寻址 RGB) |
| ESP32-C3 Super Mini | GPIO9 | 普通 GPIO LED |

### 连线示意图

```
ESP32-S3 / ESP32-C3                    7.3" E-Ink HAT
┌──────────────────────┐              ┌─────────────────┐
│                      │              │                 │
│  3.3V  ───────────────────────────  VCC              │
│  GND   ───────────────────────────  GND              │
│  GPIO5 ───────────────────────────  SCK / CLK        │
│  GPIO4 ───────────────────────────  MOSI / DIN       │
│  GPIO6 ───────────────────────────  CS               │
│  GPIO7 ───────────────────────────  DC               │
│  GPIO15 ──────────────────────────  RST              │
│  GPIO16 ──────────────────────────  BUSY             │
│  GPIO3 ───────────────────────────  PWR              │
│                      │              │                 │
│  GPIO48 (S3) ──── WS2812 LED       │                 │
│  或                                │                 │
│  GPIO9  (C3) ──── 普通 LED         │                 │
│                      │              │                 │
└──────────────────────┘              └─────────────────┘
```

### 编译烧录

项目使用 PlatformIO，默认编译目标是 ESP32-S3。

```bash
# 编译并烧录 ESP32-S3（默认）
pio run -e esp32s3 -t upload

# 编译并烧录 ESP32-C3 Super Mini
pio run -e esp32-c3-super-mini -t upload

# 串口监视
pio device monitor -b 115200
```

首次烧录后，通过浏览器访问 `http://<设备IP>` 配置 WiFi 和 MQTT 参数。

> `key.hpp`（已 gitignore）可作为默认凭据模板，内容示例：
> ```cpp
> #pragma once
> #define KEY_WIFI_SSID "your_ssid"
> #define KEY_WIFI_PASSWORD "your_password"
> ```

---

## HTTP 接口

设备启动后在 80 端口提供 HTTP 服务。

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/` | 配置管理页面（HTML） |
| GET | `/api/config` | 获取当前配置（JSON） |
| POST | `/api/config` | 修改配置，body 为 JSON |
| POST | `/api/upload` | 上传图片数据 |
| POST | `/api/reboot` | 重启设备 |
| GET | `/api/health` | 健康检查，返回 uptime |

### POST /api/upload

上传 base64 编码的 4bpp 原始图像数据。

- **Content-Type**: `text/plain`
- **Body**: base64 编码的 192000 字节二进制数据
- **成功响应**: `{"msg": "OK"}`
- **错误响应**: 400 + `{"msg": "invalid size"}` 或 `{"msg": "decode failed"}`

图像数据格式：每字节包含 2 个像素（高 4 bit 为偶像素，低 4 bit 为奇像素），按行排列，共 800×480 像素 = 192000 字节。颜色索引：

| 索引 | 颜色 | 硬件值 |
|------|------|--------|
| 0 | 黑 Black | 0x00 |
| 1 | 白 White | 0x01 |
| 2 | 黄 Yellow | 0x02 |
| 3 | 红 Red | 0x03 |
| 4 | 橙 Orange | 0x04 |
| 5 | 蓝 Blue | 0x05 |
| 6 | 绿 Green | 0x06 |

#### 示例

```bash
# 使用 show.py 一键发送（推荐）
python3 show.py photo.jpg --mode http --host 192.168.1.50 --dither

# 直接用 curl 发送已转换的 raw 数据
base64 -i image.raw | curl -X POST http://192.168.1.50/api/upload \
  -H "Content-Type: text/plain" --data-binary @-
```

### GET /api/config

```json
{
  "wifi_ssid": "MyWiFi",
  "wifi_password": "password",
  "mqtt_broker": "192.168.1.100",
  "mqtt_port": 1883,
  "mqtt_user": "",
  "mqtt_password": "",
  "ha_device_name": "eink_album"
}
```

### POST /api/config

提交与上面结构相同的 JSON，支持部分字段更新。保存后自动触发 MQTT 重连。

### GET /api/health

```json
{"uptime_us": 123456789}
```

---

## MQTT / HomeAssistant

设备启动后通过 MQTT 自动在 HA 中注册两个诊断实体（无需手动配置 YAML）：

| 实体 ID | 类型 | 说明 |
|---------|------|------|
| `sensor.<device_name>_status` | Sensor | 设备状态：idle / fetching / updating / done / error / restarted |
| `sensor.<device_name>_ip` | Sensor | 设备 IP 地址 |

设备订阅 `sensor/<device_name>/refresh/set` 主题，收到消息后触发刷新（`refresh_requested()` 标记）。

---

## show.py — 命令行工具

将任意图片转换为墨水屏 7 色 4bpp 格式并发送到设备。

```bash
# 安装依赖
pip install pillow requests

# 基本用法
python3 show.py photo.jpg --host 192.168.1.50

# 开启抖动（改善渐变效果）
python3 show.py photo.jpg --dither

# 中心裁剪为 5:3 比例，不留白边
python3 show.py photo.jpg --crop center

# 反色
python3 show.py photo.jpg --invert

# 生成预览图（不发送）
python3 show.py photo.jpg --preview

# 旋转
python3 show.py photo.jpg --cw        # 顺时针 90°
python3 show.py photo.jpg --ccw       # 逆时针 90°

# 裁剪锚点
python3 show.py photo.jpg --crop tl   # 左上角
python3 show.py photo.jpg --crop br   # 右下角
python3 show.py photo.jpg --crop n    # 不裁剪（留白边）
```

**参数说明：**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--mode` | http | 发送方式：http（直连 ESP32）/ direct（ESPHome API）/ ha（HA REST） |
| `--host` | 环境变量 EP_HOST 或 192.168.31.50 | 设备 IP |
| `--dither` | false | Floyd-Steinberg 抖动，改善照片渐变过渡 |
| `--invert` | false | 反色 |
| `--crop` | center | 裁剪锚点：tl/t/tr/l/c/r/bl/b/br/n |
| `--cw` / `--ccw` | - | 旋转 90° |
| `--preview` | false | 只生成 `_epd_preview.png` 预览图，不发送 |
