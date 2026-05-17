# Caracal

ESP32-S3 极限轻量 HTML 浏览器。

名字取自狞猫（Caracal）——比猞猁（Lynx）更轻盈敏捷的小型猫科。没有 CSS，没有 JavaScript，没有浮动布局。只有 HTML 文本、图片占位、超链接和一块屏幕。

---

## 功能概览

Caracal 通过 HTTP 获取 HTML 页面，用 gumbo-parser 解析为 DOM 树，执行块级线性布局，再通过 LVGL 渲染到 SPI LCD 上。超链接可触摸点击，触发页面导航。

**支持：**
- Wi-Fi STA 连接（WPA2）
- HTTP GET，支持重定向跟随
- HTML5 解析（gumbo-parser）
- 块级线性布局，自动换行（支持 CJK 按字断行）
- 文本渲染：`<p>`、`<h1>`~`<h6>`、`<div>`、`<pre>`、`<blockquote>`、`<li>`
- 图片占位框：`<img>` 渲染为带边框的标签框，显示 alt 文本
- 可点击超链接：`<a>` 蓝色下划线 + 触摸导航
- 水平分隔线：`<hr>`
- UTF-8 文本（ASCII 开箱即用；中文需额外字体——见下文）
- 长页面垂直滚动
- 触摸输入（兼容 FT6x36 / CST816S / GT911）

**不支持（设计如此）：**
- CSS
- JavaScript
- Float / Flex / Grid 布局
- 图片解码
- HTTPS
- 多标签页或多窗口

> 如果需要 Flex/Grid 布局或 HTTPS 支持，请使用 [Caracal Pro](./tree/pro)。

---

## 衍生版本：Caracal Pro

Caracal Pro 是本项目的增强分支，位于同级目录 `caracal-pro/`，在保留基础版全部功能的前提下新增：

| 特性 | Caracal | Caracal Pro |
|------|---------|-------------|
| 块级线性布局 | ✅ | ✅ |
| Flex 布局 | ❌ | ✅ |
| Grid 布局 | ❌ | ✅ |
| CSS style 属性解析 | ❌ | ✅ |
| HTTPS (TLS) | ❌ | ✅ |
| 固件体积 | 较小 | +50 KB (mbedTLS) |
| 主任务栈 | 8 KB | 16 KB (TLS 握手) |

Caracal Pro 不合并到主分支的原因：Flex/Grid 布局引擎和 TLS 栈显著增加了资源消耗，不是所有 ESP32-S3 部署场景都能承受。基础版面向极简嵌入式场景（仅 HTTP 局域网页面），Pro 版面向需要现代 Web 兼容性的场景。两个版本独立维护，按需选用。

---

## 硬件要求

| 部件 | 规格 |
|------|------|
| SoC | ESP32-S3（双核 240 MHz） |
| PSRAM | 8 MB Octal SPI |
| Flash | 16 MB（缩减分区后最低 4 MB） |
| LCD | ST7796S，480×320，SPI 接口 |
| 触摸 | I2C 电容触摸（FT6336 / CST816S / GT911） |

---

## 项目结构

```
caracal/
├── CMakeLists.txt               # 顶层构建
├── sdkconfig.defaults            # ESP32-S3 + PSRAM 默认配置
├── partitions.csv                # factory 分区 + SPIFFS 字体分区
├── fetch_deps.bat                # 下载 gumbo-parser 源码
│
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml         # LVGL 8.3 组件注册表依赖
│   ├── Kconfig.projbuild         # 引脚分配、Wi-Fi、URL 可配置项
│   ├── main.c                    # 入口
│   ├── wifi_sta.c / .h           # Wi-Fi 站点连接
│   ├── http_fetch.c / .h         # HTTP GET，PSRAM 缓冲
│   ├── html_dom.c / .h           # gumbo → 简化 DOM 树
│   ├── layout.c / .h             # 块级线性布局 + 自动换行
│   ├── renderer.c / .h           # 布局框 → LVGL 对象
│   ├── display.c / .h            # ST7796S SPI 驱动 + LVGL flush
│   ├── touch.c / .h              # I2C 触摸 + LVGL 输入设备
│   ├── browser.c / .h            # 编排层：URL 解析 + 页面导航
│   └── lv_mem_psram.c / .h       # LVGL PSRAM 内存适配器
│
└── components/
    └── gumbo/
        └── CMakeLists.txt         # 编译 gumbo src/ 下的源码
```

---

## 架构

```
 ┌──────────┐    ┌───────────┐    ┌──────────┐    ┌──────────┐
 │  Wi-Fi   │───▶│ HTTP GET  │───▶│  gumbo   │───▶│   DOM    │
 │   STA    │    │  → HTML   │    │  解析器  │    │   树     │
 └──────────┘    └───────────┘    └──────────┘    └──────────┘
                                                        │
                                          ┌─────────────▼──────────────┐
                                          │      块级布局引擎           │
                                          │  • 垂直堆叠                 │
                                          │  • 自动换行（CJK 按字断行） │
                                          │  • 标题 / 段落间距          │
                                          │  • <img> 占位框             │
                                          └─────────────┬──────────────┘
                                                        │
 ┌──────────┐    ┌───────────┐    ┌──────────┐    ┌─────▼──────┐
 │ ST7796S  │◀───│   LVGL    │◀───│  渲染器  │◀───│  布局框   │
 │   LCD    │    │ flush_cb  │    │ lv_label │    │           │
 └──────────┘    └─────┬─────┘    │ lv_obj   │    └──────────┘
                       │          └──────────┘
                ┌──────▼──────┐
                │   触摸屏    │
                │ FT6x36 /    │  ──▶  点击链接  ──▶  browser_navigate()
                │ CST816S     │
                └─────────────┘
```

**单次页面加载的数据流：**

1. `browser_navigate(url)` — 设置地址栏，显示加载中
2. `http_fetch()` — 发起 GET 请求，响应缓冲到 PSRAM（最大 256 KB）
3. `html_parse()` — gumbo 生成完整 DOM，我们提取简化树（跳过 `<script>`、`<style>`、`<head>`）
4. `layout_compute()` — 遍历 DOM，累积内联文本，在块级边界生成布局框，CJK 字符级自动换行
5. `renderer_render()` — 每个布局框创建 LVGL 对象：文本用 `lv_label`，图片用带边框 `lv_obj`，分隔线用 `lv_obj`
6. 触摸点击链接 → `on_link_click()` → URL 解析 → 再次调用 `browser_navigate()`

---

## 构建指南

### 前置条件

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) v5.1 或更高版本，已安装并导出环境变量
- Git

### 第 1 步 — 获取源码

```bash
git clone https://github.com/zhr-ab/Caracal.git caracal
cd caracal
```

### 第 2 步 — 下载 gumbo-parser

```batch
fetch_deps.bat
```

此脚本将 gumbo-parser 源码克隆到 `components/gumbo/src/`，CMake 构建会自动识别。

### 第 3 步 — 配置

```bash
idf.py menuconfig
```

进入 **Browser Configuration**，设置以下选项：

| 选项 | 说明 | 默认值 |
|------|------|--------|
| Wi-Fi SSID | 你的无线网络名称 | `MyWiFi` |
| Wi-Fi Password | 无线网络密码 | `MyPassword` |
| Default URL | 启动时加载的页面 | `http://example.com` |
| LCD SPI Host | 1 = SPI2，2 = SPI3 | `2` |
| LCD SPI CLK / MOSI / MISO / DC / CS / RST / BL | 开发板对应的 GPIO 编号 | 见 Kconfig |
| LCD Width / Height | 屏幕分辨率 | `480` / `320` |
| LCD Rotation | 0、90、180、270 | `0` |
| Touch SDA / SCL / INT | I2C 引脚编号 | `38` / `39` / `40` |
| Touch I2C Address | `0x38`（FT6336）、`0x15`（CST816S）、`0x5D`（GT911） | `0x38` |
| Max HTML Size | 最大抓取缓冲区大小 | `262144`（256 KB） |
| HTTP Timeout | 请求超时（毫秒） | `10000` |

然后进入 **Component Config → LVGL Configuration**，确认以下设置：

- **字体启用：** Montserrat 10、12、14、16、20、24 — 全部启用
- **默认字体：** Montserrat 14
- **文本编码：** UTF-8
- **内存大小：** 至少 256 KB（LVGL 通过 `malloc` 使用 PSRAM）
- **滚动条：** 启用
- **文件系统：** 启用（SPIFFS 字体加载所需）

### 第 4 步 — 编译与烧录

```bash
idf.py build
idf.py -p COMx flash monitor
```

将 `COMx` 替换为你的串口号。

---

## 默认引脚映射

| 功能 | GPIO | 备注 |
|------|------|------|
| LCD SPI CLK | 14 | |
| LCD SPI MOSI | 13 | |
| LCD SPI MISO | 12 | 可选，可设为 -1 |
| LCD DC | 21 | |
| LCD CS | 10 | |
| LCD RST | 11 | |
| LCD BL | 48 | 背光 |
| Touch SDA | 38 | I2C 数据 |
| Touch SCL | 39 | I2C 时钟 |
| Touch INT | 40 | 可选中断 |

所有引脚均可通过 `idf.py menuconfig` 修改。

---

## 中文 / CJK 字体支持

LVGL 内置的 Montserrat 字体仅覆盖拉丁、西里尔字母和基本符号。要显示中文、日文或韩文：

### 方案 A — 生成子集字体（推荐）

1. 打开 [LVGL Font Converter](https://lvgl.io/tools/fontconverter)
2. 参数设置：
   - **Name：** `chinese_14`
   - **Size：** 14
   - **Bpp：** 4
   - **Font：** 上传含 CJK 的 TTF 字体（如 Noto Sans SC、思源黑体）
   - **Range：** `0x20-0x7E, 0x4E00-0x9FFF`（ASCII + CJK 统一汉字）
   - **Format：** SFNT（运行时可加载）
3. 下载生成的 `.lvf` 文件
4. 烧录到 SPIFFS：

```bash
# 生成 SPIFFS 镜像并烧录
python -m spiffsgen.py 983040 ./fonts/ spiffs.bin
esptool.py --chip esp32s3 --port COMx write_flash 0x310000 spiffs.bin
```

5. 在 `renderer.c` 中运行时加载：

```c
lv_font_t *chinese_14 = lv_font_load("S:chinese_14.lvf");
lv_obj_set_style_text_font(label, chinese_14, 0);
```

### 方案 B — 编译时嵌入字体

使用 LVGL Font Converter 选择 **LVGL** 格式（而非 SFNT），字体会直接嵌入固件二进制。适合少量字形（几百个字符），但完整 CJK 会大幅增加 flash 占用。

---

## 内存预算

ESP32-S3 + 8 MB PSRAM 默认配置下的内存分配：

| 缓冲区 | 大小 | 位置 |
|--------|------|------|
| HTTP 响应 | 256 KB | PSRAM |
| DOM 树 | 约 HTML 大小的 2~3 倍 | PSRAM |
| 布局框 | 约 50~200 KB | PSRAM |
| LVGL 绘制缓冲 | 2 × (480×20×2) = 38.4 KB | PSRAM |
| LVGL 内部内存池 | 256+ KB | PSRAM（通过 malloc） |
| SPIFFS | 约 960 KB | Flash |

对于一个典型的 50 KB HTML 页面，PSRAM 总用量约 600~800 KB，8 MB 空间绰绰有余。

---

## 局限与已知问题

- **不支持 HTTPS。** TLS 会增加约 50 KB 固件体积和可观的 RAM 开销。如需 HTTPS，请使用 [Caracal Pro](../caracal-pro/)。
- **不支持图片解码。** `<img>` 仅渲染为带 alt 文本的占位框。在 MCU 上解码 PNG/JPEG 需要额外库（如 `esp_jpeg`）和大量 RAM。
- **无表单输入。** 没有屏幕键盘。URL 输入通过 menuconfig 默认值或串口控制台。
- **单页导航。** 没有前进/后退历史栈。每次点击链接直接替换当前页面。
- **触摸芯片需手动配置。** I2C 地址必须手动设置，默认为 FT6x36 的 `0x38`。
- **MADCTL 旋转。** 旋转完全由自定义 ST7796S 初始化序列处理。初始化后不要调用 `esp_lcd_panel_mirror()`，否则会覆盖 MADCTL 寄存器。
- **gumbo 源码需手动下载。** 首次构建前需运行 `fetch_deps.bat`。

---

## 故障排查

| 现象 | 可能原因 | 解决方法 |
|------|----------|----------|
| 编译报错 `gumbo source files not found` | 未运行 `fetch_deps.bat` | 运行 `fetch_deps.bat` 后重新编译 |
| Wi-Fi 连接失败 | SSID 或密码错误 | 检查 `idf.py menuconfig` → Browser Configuration |
| 屏幕白屏/无显示 | LCD 初始化失败或引脚错误 | 核对 SPI 引脚分配；检查接线 |
| 屏幕花屏 | 旋转或 MADCTL 设置错误 | 在 menuconfig 中尝试不同的 `LCD Rotation` |
| 触摸无响应 | I2C 地址或引脚错误 | 检查 `Touch I2C Address` 和 SDA/SCL 引脚 |
| 中文显示为 `?` 或方块 | 未加载 CJK 字体 | 按上方"中文 / CJK 字体支持"章节操作 |
| 页面抓取失败 | URL 不可达或使用了 HTTPS | 确保 URL 以 `http://` 开头；如需 HTTPS 请使用 [Caracal Pro](../caracal-pro/) |
| 崩溃 / 看门狗复位 | 栈溢出或内存不足 | 增大 `CONFIG_ESP_MAIN_TASK_STACK_SIZE`；减小 `Max HTML Size` |

---

## 技术决策

### 为什么用 gumbo-parser 而不是手写解析器？

真实网页上的 HTML 极少格式规范。Gumbo 实现了完整的 HTML5 解析算法，能处理残缺标签、未闭合标签等各种边界情况——正则或手写解析器遇到这些就会崩溃。约 15 KB 的源码开销换来的是鲁棒性。

### 为什么只做块级布局？

Flex 和 Grid 需要多轮迭代约束求解——计算量大、内存消耗高。块级线性布局只需单次遍历，时间复杂度 O(n)，无需 CSS 就能产生可预测的排版结果。这符合"Lynx 式"的设计目标。

### 为什么不用 `esp_lcd_panel_init()`？

ESP-IDF 的 ST7789 panel 驱动的 `init()` 会发送 ST7789 专用的寄存器序列，与 ST7796S 冲突。Caracal 通过 `esp_lcd_panel_io_tx_param()` 直接发送自定义的 ST7796S 初始化序列，然后创建 ST7789 panel 句柄仅用于 `draw_bitmap()` 回调（CASET/RASET/RAMWR 寄存器兼容）。

### 为什么不把 HTTPS / Flex / Grid 合并到主分支？

三个功能对资源的影响是叠加的：

- **HTTPS**（mbedTLS）：+50 KB 固件体积、+40 KB 握手栈、显著连接延迟
- **Flex 布局**：额外 `css_style.c` + `layout_flex.c`，约 8 KB 代码，布局阶段需多次遍历 DOM
- **Grid 布局**：额外 `layout_grid.c`，约 6 KB 代码，需维护轨道尺寸数组

三者叠加后，固件增大约 80 KB，RAM 开销增加约 200 KB，主任务栈需从 8 KB 提升到 16 KB。对于只需要在局域网内读取简单文本页面的嵌入式场景（传感器面板、IoT 控制台等），这些代价不可接受。因此这些增强功能放在独立的 [Caracal Pro](../caracal-pro/) 分支中，按需选用。

---

## 许可证

MIT

---

---

# Caracal (English)

Ultra-lightweight HTML browser for ESP32-S3.

Named after the caracal — a smaller, lighter wild cat than the lynx. No CSS, no JavaScript, no floating layout. Just HTML text, images, links, and a screen.

---

## What It Does

Caracal fetches an HTML page over HTTP, parses it into a DOM tree, computes a block-level linear layout, and renders the result on an SPI LCD via LVGL. Hyperlinks are touch-tappable and trigger page navigation.

**Supported:**
- Wi-Fi STA connection (WPA2)
- HTTP GET with redirect following
- HTML5 parsing via gumbo-parser
- Block linear layout with automatic word wrap (CJK-aware)
- Text rendering: `<p>`, `<h1>`–`<h6>`, `<div>`, `<pre>`, `<blockquote>`, `<li>`
- Image placeholders: `<img>` rendered as labeled bordered boxes
- Clickable hyperlinks: `<a>` with blue underline + tap navigation
- Horizontal rules: `<hr>`
- UTF-8 text (ASCII out of the box; CJK requires custom font — see below)
- Vertical scrolling for long pages
- Touch input (FT6x36 / CST816S / GT911 compatible)

**Not supported (by design):**
- CSS
- JavaScript
- Float / Flex / Grid layout
- Image decoding
- HTTPS
- Tabs or multiple windows

> If you need Flex/Grid layout or HTTPS support, use [Caracal Pro](../caracal-pro/).

---

## Derivative: Caracal Pro

Caracal Pro is an enhanced fork located in the sibling directory `caracal-pro/`. It retains all base features and adds:

| Feature | Caracal | Caracal Pro |
|---------|---------|-------------|
| Block linear layout | ✅ | ✅ |
| Flex layout | ❌ | ✅ |
| Grid layout | ❌ | ✅ |
| CSS style attribute parsing | ❌ | ✅ |
| HTTPS (TLS) | ❌ | ✅ |
| Firmware size | Smaller | +50 KB (mbedTLS) |
| Main task stack | 8 KB | 16 KB (TLS handshake) |

Why Caracal Pro is not merged into the main branch: the Flex/Grid layout engines and TLS stack significantly increase resource consumption, which not all ESP32-S3 deployments can afford. The base version targets minimal embedded scenarios (HTTP-only LAN pages), while Pro targets use cases requiring modern web compatibility. Both versions are maintained independently — choose as needed.

---

## Hardware Requirements

| Component | Specification |
|-----------|--------------|
| SoC | ESP32-S3 (dual-core 240 MHz) |
| PSRAM | 8 MB Octal SPI |
| Flash | 16 MB (4 MB minimum with reduced partition) |
| LCD | ST7796S, 480×320, SPI interface |
| Touch | I2C capacitive (FT6336 / CST816S / GT911) |

---

## Project Structure

```
caracal/
├── CMakeLists.txt               # Top-level build
├── sdkconfig.defaults            # ESP32-S3 + PSRAM defaults
├── partitions.csv                # factory app + SPIFFS for fonts
├── fetch_deps.bat                # Download gumbo-parser source
│
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml         # LVGL 8.3 via component registry
│   ├── Kconfig.projbuild         # Pin assignments, Wi-Fi, URLs
│   ├── main.c                    # Entry point
│   ├── wifi_sta.c / .h           # Wi-Fi station connect
│   ├── http_fetch.c / .h         # HTTP GET, PSRAM-buffered
│   ├── html_dom.c / .h           # gumbo → simplified DOM tree
│   ├── layout.c / .h             # Block linear layout + word wrap
│   ├── renderer.c / .h           # DOM layout → LVGL objects
│   ├── display.c / .h            # ST7796S SPI driver + LVGL flush
│   ├── touch.c / .h              # I2C touch + LVGL input device
│   ├── browser.c / .h            # Orchestration: URL resolve + navigate
│   └── lv_mem_psram.c / .h       # PSRAM memory adapter for LVGL
│
└── components/
    └── gumbo/
        └── CMakeLists.txt         # Build gumbo source from src/
```

---

## Architecture

```
 ┌──────────┐    ┌───────────┐    ┌──────────┐    ┌──────────┐
 │  Wi-Fi   │───▶│ HTTP GET  │───▶│  gumbo   │───▶│   DOM    │
 │   STA    │    │  → HTML   │    │  parser  │    │  tree    │
 └──────────┘    └───────────┘    └──────────┘    └──────────┘
                                                        │
                                          ┌─────────────▼──────────────┐
                                          │     Block Layout Engine     │
                                          │  • vertical stack           │
                                          │  • word wrap (CJK-aware)    │
                                          │  • heading / paragraph gap  │
                                          │  • <img> placeholder boxes  │
                                          └─────────────┬──────────────┘
                                                        │
 ┌──────────┐    ┌───────────┐    ┌──────────┐    ┌─────▼──────┐
 │ ST7796S  │◀───│   LVGL    │◀───│ Renderer │◀───│  Layout   │
 │   LCD    │    │  flush_cb │    │ lv_label │    │  boxes    │
 └──────────┘    └─────┬─────┘    │ lv_obj   │    └──────────┘
                       │          └──────────┘
                ┌──────▼──────┐
                │   Touch     │
                │  FT6x36 /   │  ──▶  tap link  ──▶  browser_navigate()
                │  CST816S    │
                └─────────────┘
```

**Data flow per page load:**

1. `browser_navigate(url)` — sets URL bar, shows loading
2. `http_fetch()` — GET request, response buffered in PSRAM (up to 256 KB)
3. `html_parse()` — gumbo produces full DOM, we extract a simplified tree (skipping `<script>`, `<style>`, `<head>`)
4. `layout_compute()` — walk DOM, accumulate inline text, emit layout boxes on block boundaries, word-wrap with CJK character-level breaks
5. `renderer_render()` — create LVGL objects per layout box: `lv_label` for text, bordered `lv_obj` for images, line for `<hr>`
6. Link taps invoke `on_link_click()` → URL resolution → `browser_navigate()` again

---

## Build Instructions

### Prerequisites

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) v5.1 or later, installed and exported
- Git

### Step 1 — Get the source

```bash
git clone <your-repo-url> caracal
cd caracal
```

### Step 2 — Fetch gumbo-parser

```batch
fetch_deps.bat
```

This clones the gumbo-parser repository into `components/gumbo/src/`. The CMake build will pick it up automatically.

### Step 3 — Configure

```bash
idf.py menuconfig
```

Navigate to **Browser Configuration** and set:

| Option | Description | Default |
|--------|-------------|---------|
| Wi-Fi SSID | Your access point name | `MyWiFi` |
| Wi-Fi Password | Your access point password | `MyPassword` |
| Default URL | Page loaded on startup | `http://example.com` |
| LCD SPI Host | 1 = SPI2, 2 = SPI3 | `2` |
| LCD SPI CLK / MOSI / MISO / DC / CS / RST / BL | GPIO numbers for your board | See Kconfig |
| LCD Width / Height | Display resolution | `480` / `320` |
| LCD Rotation | 0, 90, 180, 270 | `0` |
| Touch SDA / SCL / INT | I2C GPIO numbers | `38` / `39` / `40` |
| Touch I2C Address | `0x38` (FT6336), `0x15` (CST816S), `0x5D` (GT911) | `0x38` |
| Max HTML Size | Maximum fetch buffer | `262144` (256 KB) |
| HTTP Timeout | Request timeout in ms | `10000` |

Then navigate to **Component Config → LVGL Configuration** and verify:

- **Font enablement:** Montserrat 10, 12, 14, 16, 20, 24 — all enabled
- **Default font:** Montserrat 14
- **Text encoding:** UTF-8
- **Memory size:** at least 256 KB (LVGL will use PSRAM via `malloc`)
- **Scrollbar:** enabled
- **Filesystem:** enabled (needed for SPIFFS font loading)

### Step 4 — Build and flash

```bash
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMx` with your serial port.

---

## Default Pin Mapping

| Function | GPIO | Notes |
|----------|------|-------|
| LCD SPI CLK | 14 | |
| LCD SPI MOSI | 13 | |
| LCD SPI MISO | 12 | Optional, can set to -1 |
| LCD DC | 21 | |
| LCD CS | 10 | |
| LCD RST | 11 | |
| LCD BL | 48 | Backlight |
| Touch SDA | 38 | I2C data |
| Touch SCL | 39 | I2C clock |
| Touch INT | 40 | Optional interrupt |

All pins are configurable via `idf.py menuconfig`.

---

## CJK / Chinese Font Support

LVGL's built-in Montserrat fonts cover Latin, Cyrillic, and basic symbols only. To display Chinese, Japanese, or Korean text:

### Option A — Generate a subset font (recommended)

1. Go to [LVGL Font Converter](https://lvgl.io/tools/fontconverter)
2. Settings:
   - **Name:** `chinese_14`
   - **Size:** 14
   - **Bpp:** 4
   - **Font:** Upload a TTF with CJK coverage (e.g., Noto Sans SC, Source Han Sans)
   - **Range:** `0x20-0x7E, 0x4E00-0x9FFF` (ASCII + CJK Unified Ideographs)
   - **Format:** SFNT (runtime-loadable)
3. Download the `.lvf` file
4. Flash to SPIFFS:

```bash
# Create spiffs image and flash
python -m spiffsgen.py 983040 ./fonts/ spiffs.bin
esptool.py --chip esp32s3 --port COMx write_flash 0x310000 spiffs.bin
```

5. Load at runtime in `renderer.c`:

```c
lv_font_t *chinese_14 = lv_font_load("S:chinese_14.lvf");
lv_obj_set_style_text_font(label, chinese_14, 0);
```

### Option B — Compile-time font

Use the LVGL Font Converter in **LVGL** format instead of SFNT. This embeds the font directly in the firmware binary. Suitable for small glyph sets (e.g., a few hundred characters), but increases flash size significantly for full CJK.

---

## Memory Budget

With the default configuration on ESP32-S3 with 8 MB PSRAM:

| Buffer | Size | Location |
|--------|------|----------|
| HTTP response | 256 KB | PSRAM |
| DOM tree | ~2–3× HTML size | PSRAM |
| Layout boxes | ~50–200 KB | PSRAM |
| LVGL draw buffers | 2 × (480×20×2) = 38.4 KB | PSRAM |
| LVGL internal pool | 256+ KB | PSRAM (via malloc) |
| SPIFFS | ~960 KB | Flash |

Total PSRAM usage for a typical 50 KB HTML page is approximately 600–800 KB, leaving ample headroom within 8 MB.

---

## Limitations & Known Issues

- **No HTTPS.** TLS adds ~50 KB to the binary and significant RAM overhead. If needed, use [Caracal Pro](../caracal-pro/) which has built-in HTTPS support.
- **No image decoding.** `<img>` renders as a placeholder box with the `alt` text. PNG/JPEG decoding on MCU requires additional libraries (e.g., `esp_jpeg`) and significant RAM.
- **No form input.** There is no on-screen keyboard. URL input is via menuconfig default or serial console.
- **Single-page navigation.** No back/forward history stack. Each link tap replaces the current page.
- **Touch controller auto-detect.** The I2C address must be set manually. FT6x36 at `0x38` is the default.
- **MADCTL rotation.** Rotation is handled entirely in the custom ST7796S init sequence. Do not call `esp_lcd_panel_mirror()` after init, as it would overwrite the MADCTL register.
- **Gumbo source must be fetched.** Run `fetch_deps.bat` before first build.

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| Build fails: `gumbo source files not found` | Did not run `fetch_deps.bat` | Run `fetch_deps.bat` then rebuild |
| Wi-Fi connection fails | Wrong SSID/password | Check `idf.py menuconfig` → Browser Configuration |
| Screen is blank / white | LCD init failed or wrong pins | Verify SPI pin assignments; check wiring |
| Screen shows garbled image | Wrong rotation or MADCTL | Try different `LCD Rotation` in menuconfig |
| Touch not responding | Wrong I2C address or pins | Check `Touch I2C Address` and SDA/SCL pins |
| Chinese text shows `?` or squares | No CJK font loaded | Follow the CJK font section above |
| Page fetch fails | URL not reachable or HTTPS | Ensure URL uses `http://`, not `https://`; for HTTPS use [Caracal Pro](../caracal-pro/) |
| Crash / watchdog reset | Stack overflow or OOM | Increase `CONFIG_ESP_MAIN_TASK_STACK_SIZE`; reduce `Max HTML Size` |

---

## Technical Decisions

### Why gumbo-parser and not a hand-written parser?

HTML found on the real web is rarely well-formed. Gumbo implements the full HTML5 parsing algorithm, handling broken tags, missing closes, and edge cases that a regex or hand-written parser would choke on. The ~15 KB of source code is a worthwhile trade-off for robustness.

### Why block layout only?

Flex and Grid require multi-pass layout with constraint solving — computationally expensive and memory-intensive. Block linear layout is single-pass, O(n) in the number of DOM nodes, and produces predictable results without CSS. This matches the "Lynx-like" design goal.

### Why not `esp_lcd_panel_init()`?

The ESP-IDF ST7789 panel driver's `init()` sends ST7789-specific register sequences that conflict with ST7796S. Caracal sends its own ST7796S init sequence directly via `esp_lcd_panel_io_tx_param()`, then creates the ST7789 panel handle solely for the `draw_bitmap()` callback (CASET/RASET/RAMWR are register-compatible).

### Why not merge HTTPS / Flex / Grid into the main branch?

The resource impact of these three features is cumulative:

- **HTTPS** (mbedTLS): +50 KB firmware, +40 KB handshake stack, significant connection latency
- **Flex layout**: additional `css_style.c` + `layout_flex.c`, ~8 KB code, multi-pass DOM traversal during layout
- **Grid layout**: additional `layout_grid.c`, ~6 KB code, track sizing arrays

Combined, firmware grows by ~80 KB, RAM overhead increases by ~200 KB, and the main task stack must increase from 8 KB to 16 KB. For embedded scenarios that only need simple text pages over LAN (sensor dashboards, IoT consoles, etc.), this cost is unacceptable. These enhancements live in the separate [Caracal Pro](../caracal-pro/) branch — choose as needed.

---

## License

MIT
