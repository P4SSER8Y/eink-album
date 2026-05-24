#!/usr/bin/env python3
"""
EPD Image Sender — 将任意图片转成 7 色墨水屏格式并发送到设备

用法:
  ./show.py photo.jpg --mode http --host 192.168.31.50
  ./show.py photo.png --dither --mode http            # 抖动（改善渐变效果）
  ./show.py photo.png --preview                       # 先预览效果
  ./show.py photo.png --mode direct --host ...        # ESPHome native API
  ./show.py photo.png --mode ha                       # Home Assistant REST API
  ./show.py photo.png --crop center                   # 中心裁剪，不留白边

依赖: pip install pillow requests
"""

import argparse
import asyncio
import base64
import logging
import os
import requests
import sys

# 抑制 aioesphomeapi disconnect 断连日志
logging.getLogger("aioesphomeapi").setLevel(logging.CRITICAL)

# ======== EPD 参数 ========
EPD_WIDTH = 800
EPD_HEIGHT = 480
EPD_BUF_SIZE = EPD_WIDTH * EPD_HEIGHT // 2  # 192000

# 7 色 EPD palette (RGB) — AC073TC1
EPD_COLORS = [
    (0, 0, 0),         # 0: 黑  BLACK
    (255, 255, 255),   # 1: 白  WHITE
    (255, 255, 0),     # 2: 黄  YELLOW
    (255, 0, 0),       # 3: 红  RED
    (255, 128, 0),     # 4: 橙  ORANGE
    (0, 0, 255),       # 5: 蓝  BLUE
    (0, 255, 0),       # 6: 绿  GREEN
]


def nearest_epd_color(r, g, b):
    """找最近的 EPD 颜色索引（欧几里得距离）"""
    best, best_dist = 0, float("inf")
    for i, (cr, cg, cb) in enumerate(EPD_COLORS):
        dr, dg, db = r - cr, g - cg, b - cb
        dist = dr * dr + dg * dg + db * db
        if dist < best_dist:
            best_dist = dist
            best = i
    return best


def convert_image(image_path, invert=False, dither=False, preview=False, crop=None, rotation=None):
    """
    读取图片 → 旋转 → 缩放到 800×480 → 量化到 7 色 → 打包 4bpp → 返回原始字节
    Python 颜色索引 = 硬件色值 (0x00-0x06)，可直接写入 EPD buffer

    crop: "none" (保持白边填充) 或九宫格锚点:
          top-left, top, top-right, left, center, right, bottom-left, bottom, bottom-right
    rotation: None, "cw" (顺时针90°), "ccw" (逆时针90°)
    """
    from PIL import Image

    img = Image.open(image_path)

    if rotation == "cw":
        img = img.transpose(Image.ROTATE_270)
    elif rotation == "ccw":
        img = img.transpose(Image.ROTATE_90)

    src_w, src_h = img.size
    target_ratio = EPD_WIDTH / EPD_HEIGHT  # 5:3

    if crop and crop != "none":
        # 裁剪模式: 按九宫格锚点裁剪到 800:480，不留白边
        crop_w, crop_h = src_w, src_h
        src_ratio = src_w / src_h

        if src_ratio > target_ratio:
            # 源图更宽 → 高度全用，裁剪宽度
            crop_h = src_h
            crop_w = int(src_h * target_ratio)
        elif src_ratio < target_ratio:
            # 源图更高 → 宽度全用，裁剪高度
            crop_w = src_w
            crop_h = int(src_w / target_ratio)

        # 解析锚点: "top-left" → ("left", "top"), "center" → ("center", "center")
        if "-" in crop:
            v_pos, h_pos = crop.split("-")
        elif crop in ("top", "bottom"):
            v_pos, h_pos = crop, "center"
        elif crop in ("left", "right"):
            v_pos, h_pos = "center", crop
        else:  # center
            v_pos, h_pos = "center", "center"

        x = 0 if h_pos == "left" else (src_w - crop_w if h_pos == "right" else (src_w - crop_w) // 2)
        y = 0 if v_pos == "top" else (src_h - crop_h if v_pos == "bottom" else (src_h - crop_h) // 2)

        img = img.crop((x, y, x + crop_w, y + crop_h))
        img = img.resize((EPD_WIDTH, EPD_HEIGHT), Image.LANCZOS)
        canvas = img.convert("RGB")
    else:
        # 默认模式: 保持宽高比缩放 + 白边填充
        img.thumbnail((EPD_WIDTH, EPD_HEIGHT), Image.LANCZOS)
        canvas = Image.new("RGB", (EPD_WIDTH, EPD_HEIGHT), (255, 255, 255))
        x = (EPD_WIDTH - img.width) // 2
        y = (EPD_HEIGHT - img.height) // 2
        canvas.paste(img, (x, y))

    if invert:
        canvas = canvas.point(lambda p: 255 - p)

    # 抖动（Floyd-Steinberg 到自定义 7 色调色板）
    if dither:
        pal_img = Image.new("P", (1, 1))
        pal_data = b"".join(bytes(c) for c in EPD_COLORS) + b"\x00" * (768 - 7 * 3)
        pal_img.putpalette(pal_data)
        canvas = canvas.quantize(palette=pal_img, dither=Image.FLOYDSTEINBERG).convert("RGB")

    # nearest color 映射
    pixels = canvas.load()
    for py in range(EPD_HEIGHT):
        for px in range(EPD_WIDTH):
            r, g, b = pixels[px, py][:3]
            pixels[px, py] = EPD_COLORS[nearest_epd_color(r, g, b)]

    # 预览：生成转换后的 PNG
    if preview:
        preview_path = os.path.splitext(image_path)[0] + "_epd_preview.png"
        canvas.save(preview_path)
        print(f"  预览图已保存: {preview_path}")

    # 打包为 4bpp: 每字节 = 奇像素(高4bit) | 偶像素(低4bit)
    pixels = canvas.load()
    buf = bytearray(EPD_BUF_SIZE)
    for py in range(EPD_HEIGHT):
        for px in range(0, EPD_WIDTH, 2):
            c1 = nearest_epd_color(*pixels[px, py])
            c2 = nearest_epd_color(*pixels[px + 1, py]) if px + 1 < EPD_WIDTH else 1
            idx = (py * EPD_WIDTH + px) // 2
            buf[idx] = (c1 << 4) | c2

    return buf


# ======== 发送: HTTP 直传 ESP32 (base64 编码) ========

def send_via_http(host, raw_data):
    """Base64 编码后 POST 到 ESP32 /api/upload（避免二进制 0x00 截断）"""
    import base64
    b64 = base64.b64encode(raw_data).decode("ascii")
    url = f"http://{host}/api/upload"
    print(f"  ▶ 发送到 {url} ({len(raw_data)} bytes → {len(b64)} chars base64)")
    r = requests.post(url, data=b64, timeout=30,
                      headers={"Content-Type": "text/plain"},
                      proxies={"http": None, "https": None})
    if r.status_code == 200:
        print(f"  ✓ 发送成功，屏幕正在刷新")
        return True
    else:
        print(f"  ✗ 失败: {r.status_code} {r.text}")
        return False


# ======== 发送: ESPHome Native API (分块) ========

CHUNK_SIZE = 4000  # base64 chars per chunk (protobuf message fits in ESP32 memory)


async def send_direct(host, raw_data, encryption_key=None, port=6053):
    """通过 ESPHome native API 分块发送图片"""
    from aioesphomeapi import APIClient, APIConnectionError

    b64_data = base64.b64encode(raw_data).decode("ascii")

    client = APIClient(host, port, "", noise_psk=encryption_key or None)
    try:
        await client.connect(login=True)
    except APIConnectionError as e:
        print(f"  ✗ 连接失败: {e}")
        print("  提示: 检查 IP 是否正确，设备是否在线")
        return False

    try:
        _, services = await client.list_entities_services()
        svc_map = {}
        for s in services:
            svc_map[s.name] = s

        missing = [n for n in ("display_begin", "display_chunk", "display_end") if n not in svc_map]
        if missing:
            print(f"  ✗ 缺少分块服务: {missing}（需要最新固件）")
            print(f"  可用服务: {list(svc_map.keys())}")
            return False

        # Phase 1: begin
        await client.execute_service(svc_map["display_begin"], {})
        print(f"  ▶ begin")

        # Phase 2: send chunks
        total = len(b64_data)
        chunks = [b64_data[i:i + CHUNK_SIZE] for i in range(0, total, CHUNK_SIZE)]
        for idx, chunk in enumerate(chunks):
            is_last = (idx == len(chunks) - 1)
            # 只有最后一帧带 padding
            if is_last and not chunk.endswith("="):
                chunk += "=" * ((4 - len(chunk) % 4) % 4)
            elif not is_last:
                chunk = chunk.rstrip("=")

            await client.execute_service(svc_map["display_chunk"], {"data": chunk})
            if idx % 10 == 0:
                print(f"  ▶ chunk {idx + 1}/{len(chunks)}")

        # Phase 3: end (refresh + deep sleep)
        await client.execute_service(svc_map["display_end"], {})
        print(f"  ✓ 发送完成 ({len(chunks)} chunks, {total} bytes base64)")
        return True
    finally:
        try:
            await client.disconnect(force=True)
        except Exception:
            pass


# ======== 发送: Home Assistant REST API ========

def send_via_ha(ha_url, ha_token, raw_data, entity_id="epd_dashboard"):
    """通过 Home Assistant REST API 发送"""
    b64_data = base64.b64encode(raw_data).decode("ascii")
    url = f"{ha_url}/api/services/esphome/{entity_id}_display_image"
    headers = {
        "Authorization": f"Bearer {ha_token}",
        "Content-Type": "application/json",
    }
    payload = {"data": b64_data}
    r = requests.post(url, json=payload, headers=headers, timeout=60)
    r.raise_for_status()
    print(f"  ✓ HA 调用成功: {r.status_code}")


# ======== CLI ========

def main():
    parser = argparse.ArgumentParser(
        description="将图片转为 EPD 7色墨水屏格式并发送",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  ./epd_send.py photo.jpg --dither
  ./epd_send.py photo.png --preview              # 先看效果
  ./epd_send.py photo.png --dither --mode direct --host 192.168.31.50
  ./epd_send.py photo.png --invert                # 反色

环境变量:
  EP_HOST=192.168.31.50           ESPHome 设备 IP (direct 模式)
  EP_ENCRYPTION_KEY=841ocG9...    API 加密 key (direct 模式)
  HA_URL=http://homeassistant.local:8123
  HA_TOKEN=xxx
        """,
    )
    parser.add_argument("image", help="输入图片路径")
    parser.add_argument("--preview", action="store_true", help="保存预览图")
    parser.add_argument("--dither", action="store_true", help="抖动处理（改善渐变）")
    parser.add_argument("--cw", action="store_true", help="顺时针旋转 90°（先旋转再裁剪）")
    parser.add_argument("--ccw", action="store_true", help="逆时针旋转 90°（先旋转再裁剪）")
    parser.add_argument("--invert", action="store_true", help="反转颜色")
    CROP_ALIASES = {
        "tl": "top-left", "lt": "top-left",
        "t": "top",
        "tr": "top-right", "rt": "top-right",
        "l": "left",
        "c": "center",
        "r": "right",
        "bl": "bottom-left", "lb": "bottom-left",
        "b": "bottom",
        "br": "bottom-right", "rb": "bottom-right",
        "n": "none",
    }
    # 合并所有合法值
    crop_choices = list(CROP_ALIASES.keys()) + [
        v for v in CROP_ALIASES.values() if v not in CROP_ALIASES
    ]
    parser.add_argument(
        "--crop",
        choices=crop_choices,
        default="center",
        help="九宫格锚点裁剪: tl/lt, t, tr/rt, l, c, r, bl/lb, b, br/rb, n (默认 c)",
    )
    parser.add_argument(
        "--mode",
        choices=["http", "direct", "ha"],
        default="http",
        help="发送方式: http=直连ESP32 HTTP, direct=ESPHome API, ha=HA REST API (default: http)",
    )
    parser.add_argument("--host", help="ESPHome 设备 IP (direct 模式)")
    parser.add_argument("--port", type=int, default=6053)
    parser.add_argument("--encryption-key", help="API encryption key (direct 模式)")
    parser.add_argument("--ha-url", help="HA URL (ha 模式)")
    parser.add_argument("--ha-token", help="HA Long-lived Token (ha 模式)")
    args = parser.parse_args()
    # 解析 crop 缩写
    crop = CROP_ALIASES.get(args.crop, args.crop)
    rotation = "cw" if args.cw else ("ccw" if args.ccw else None)

    if not os.path.isfile(args.image):
        print(f"✗ 文件不存在: {args.image}")
        sys.exit(1)

    print(f"📷 处理图片: {args.image}")

    try:
        raw = convert_image(
            args.image, invert=args.invert, dither=args.dither, preview=args.preview,
            crop=crop, rotation=rotation
        )
    except ImportError as e:
        print(f"✗ 缺少依赖: {e}")
        print("  请安装: pip install pillow requests")
        sys.exit(1)

    print(f"  ✓ 转换完成: {len(raw)} bytes (4bpp raw)")

    if args.mode == "http":
        host = args.host or os.environ.get("EP_HOST") or "192.168.31.50"
        if not send_via_http(host, raw):
            sys.exit(1)
    elif args.mode == "ha":
        ha_url = args.ha_url or os.environ.get("HA_URL")
        ha_token = args.ha_token or os.environ.get("HA_TOKEN")
        if not ha_url or not ha_token:
            print("  ⚠ 需要 HA_URL 和 HA_TOKEN 环境变量，或通过 --ha-url/--ha-token 传入")
            sys.exit(1)
        send_via_ha(ha_url, ha_token, raw)
    else:  # direct (ESPHome)
        host = args.host or os.environ.get("EP_HOST")
        if not host:
            print("✗ 请设置 EP_HOST 环境变量或通过 --host 指定 ESPHome 设备 IP")
            sys.exit(1)
        encryption_key = args.encryption_key or os.environ.get("EP_ENCRYPTION_KEY")
        if not encryption_key:
            print("  ⚠ 未提供加密 key，将尝试非加密连接")
        success = asyncio.run(send_direct(host, raw, encryption_key, port=args.port))
        if not success:
            sys.exit(1)

    print("🎉 完成!")


if __name__ == "__main__":
    main()
