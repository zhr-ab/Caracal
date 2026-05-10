# Caracal Pro

ESP32-S3 增强型轻量 HTML 浏览器 —— 在 [Caracal](../esp32-browser/) 基础上新增 Flex 布局、Grid 布局和 HTTPS 支持。

---

## 与基础版的区别

| 特性 | Caracal | Caracal Pro |
|------|---------|-------------|
| 块级线性布局 | ✅ | ✅ |
| Flex 布局 | ❌ | ✅ |
| Grid 布局 | ❌ | ✅ |
| CSS style 属性解析 | ❌ | ✅ |
| HTTPS (TLS) | ❌ | ✅ |
| JavaScript | ❌ | ❌ |
| 图片解码 | ❌ | ❌ |
| 固件体积 | 较小 | +80 KB |
| 主任务栈 | 8 KB | 16 KB |
| PSRAM 占用 | ~1 MB | ~1.5 MB |

如果你的场景仅需 HTTP 局域网页面，推荐使用更轻量的 [Caracal 基础版](../esp32-browser/)。

---

## 功能概览

Caracal Pro 通过 HTTP/HTTPS 获取 HTML 页面，解析 DOM 树中的 CSS `style` 属性，根据 `display` 属性分发到块级、Flex 或 Grid 布局引擎，最终通过 LVGL 渲染到 SPI LCD。

**支持：**
- Wi-Fi STA 连接（WPA2）
- HTTP / HTTPS GET，支持重定向跟随
- HTML5 解析（gumbo-parser）
- CSS `style` 属性解析（display, flex-*, grid-*, gap, padding, margin, width/height, font-size, color）
- 块级线性布局，自动换行（CJK 按字断行）
- **Flex 布局**：flex-direction, justify-content, align-items, flex-wrap, flex-grow/shrink, gap
- **Grid 布局**：grid-template-columns/rows, fr/px/% 单位, gap, auto-flow
- 文本渲染：`<p>`、`<h1>`~`<h6>`、`<div>`、`<pre>`、`<blockquote>`、`<li>`
- 图片占位框：`<img>` 渲染为带边框的标签框
- 可点击超链接：`<a>` 蓝色下划线 + 触摸导航
- 水平分隔线：`<hr>`
- UTF-8 文本（ASCII 开箱即用；中文需额外字体）
- 长页面垂直滚动
- 触摸输入（兼容 FT6x36 / CST816S / GT911）

**不支持（设计如此）：**
- JavaScript
- 图片解码
- 外部 CSS 文件（仅支持内联 `style` 属性）
- 多标签页或多窗口

---

## 硬件要求

| 部件 | 规格 |
|------|------|
| SoC | ESP32-S3（双核 240 MHz） |
| PSRAM | 8 MB Octal SPI |
| Flash | 16 MB |
| LCD | ST7796S，480×320，SPI 接口 |
| 触摸 | I2C 电容触摸（FT6336 / CST816S / GT911） |

---

## 项目结构

```
caracal-pro/
├── CMakeLists.txt               # 顶层构建
├── sdkconfig.defaults            # ESP32-S3 + PSRAM + TLS 默认配置
├── partitions.csv                # 4MB app + SPIFFS
├── fetch_deps.bat                # 下载 gumbo-parser 源码
│
├── main/
│   ├── CMakeLists.txt            # 含 mbedtls 依赖
│   ├── idf_component.yml         # LVGL 8.3 组件注册表
│   ├── Kconfig.projbuild         # 引脚 + TLS 可配置项
│   ├── main.c
│   ├── wifi_sta.c / .h
│   ├── http_fetch.c / .h         # HTTP + HTTPS
│   ├── html_dom.c / .h           # gumbo → DOM (含 CSS style)
│   ├── css_style.c / .h          # ★ CSS style 属性解析器
│   ├── layout.c / .h             # 布局分发器 (block/flex/grid)
│   ├── layout_flex.c / .h        # ★ Flex 布局引擎
│   ├── layout_grid.c / .h        # ★ Grid 布局引擎
│   ├── renderer.c / .h           # 渲染器 (含 flex/grid 容器背景)
│   ├── display.c / .h
│   ├── touch.c / .h
│   ├── browser.c / .h
│   └── lv_mem_psram.c / .h
│
└── components/
    └── gumbo/
        └── CMakeLists.txt
```

★ 标记为 Pro 版新增模块。

---

## 架构

```
 ┌──────────┐    ┌────────────┐    ┌──────────┐    ┌──────────┐
 │  Wi-Fi   │───▶│ HTTP(S)    │───▶│  gumbo   │───▶│   DOM    │
 │   STA    │    │  → HTML    │    │  解析器  │    │ + style  │
 └──────────┘    └────────────┘    └──────────┘    └──────────┘
                            TLS (mbedTLS)                │
                                                         │
                          ┌──────────────────────────────▼──────────────────┐
                          │              布局分发器 (layout.c)                │
                          │  display:flex?  →  Flex 布局引擎                │
                          │  display:grid?  →  Grid 布局引擎                │
                          │  其他            →  块级线性布局                  │
                          └──────────────────────────────┬──────────────────┘
                                                         │
 ┌──────────┐    ┌───────────┐    ┌──────────┐    ┌─────▼──────┐
 │ ST7796S  │◀───│   LVGL    │◀───│  渲染器  │◀───│  布局框   │
 │   LCD    │    │ flush_cb  │    │ flex/grid│    │           │
 └──────────┘    └─────┬─────┘    │ 容器背景 │    └──────────┘
                       │          └──────────┘
                ┌──────▼──────┐
                │   触摸屏    │  ──▶  点击链接  ──▶  browser_navigate()
                └─────────────┘
```

**单次页面加载的数据流：**

1. `browser_navigate(url)` — 设置地址栏，识别 http/https
2. `http_fetch()` — GET 请求（HTTPS 自动启用 TLS），响应缓冲到 PSRAM
3. `html_parse()` — gumbo 生成 DOM 树，**同时解析每个元素的 `style` 属性**生成 `css_style_t`
4. `layout_compute()` — 遍历 DOM，**按 `display` 属性分发**：
   - `display:flex` → `layout_flex()` — 测量子项，分配主轴空间，应用 justify/align
   - `display:grid` → `layout_grid()` — 解析轨道模板，放置子项到单元格
   - 其他 → 块级线性布局（同基础版）
5. `renderer_render()` — Flex/Grid 容器用浅色背景框包裹，内部子元素相对定位
6. 触摸点击链接 → `on_link_click()` → URL 解析 → `browser_navigate()`

---

## CSS Style 属性支持

Caracal Pro 通过解析 HTML 元素的 `style=""` 属性实现样式控制。不支持外部 CSS 文件或 `<style>` 标签。

### 支持的属性

| 属性 | 值 | 说明 |
|------|-----|------|
| `display` | `block`, `flex`, `grid`, `inline`, `none` | 布局模式选择；`none` 隐藏元素 |
| `flex-direction` | `row`, `column` | Flex 主轴方向 |
| `justify-content` | `flex-start`, `center`, `flex-end`, `space-between` | Flex 主轴对齐 |
| `align-items` | `flex-start`, `center`, `flex-end`, `stretch` | Flex 交叉轴对齐 |
| `flex-wrap` | `nowrap`, `wrap` | Flex 换行 |
| `flex-grow` | 整数 | Flex 伸展因子 |
| `flex-shrink` | 整数 | Flex 收缩因子 |
| `flex-basis` | `auto`, `Npx` | Flex 基准尺寸 |
| `grid-template-columns` | `Npx`, `N%`, `1fr`, `auto` | Grid 列轨道模板 |
| `grid-template-rows` | `Npx`, `N%`, `1fr`, `auto` | Grid 行轨道模板 |
| `gap` | `Npx` | Flex/Grid 间距 |
| `padding` | `Npx` | 内边距 |
| `margin` | `Npx` | 外边距 |
| `width` | `auto`, `Npx` | 宽度 |
| `height` | `auto`, `Npx` | 高度 |
| `font-size` | `Npx` | 字号 |
| `color` | `#RRGGBB` | 文字颜色 |

### 示例

```html
<!-- Flex 水平导航栏 -->
<nav style="display:flex; gap:8px; justify-content:space-between">
  <a href="/">首页</a>
  <a href="/about">关于</a>
  <a href="/contact">联系</a>
</nav>

<!-- Grid 两列卡片布局 -->
<div style="display:grid; grid-template-columns:1fr 1fr; gap:12px">
  <div style="padding:8px">卡片 1</div>
  <div style="padding:8px">卡片 2</div>
  <div style="padding:8px">卡片 3</div>
  <div style="padding:8px">卡片 4</div>
</div>

<!-- Flex 竖排，居中对齐 -->
<div style="display:flex; flex-direction:column; align-items:center; gap:4px">
  <h2>标题</h2>
  <p>正文内容</p>
</div>

<!-- 隐藏元素 -->
<div style="display:none">这段不会显示</div>
```

### 语义标签自动识别

`<nav>` 元素在未指定 `style` 时自动识别为 `display:flex; flex-direction:row; gap:8px`。

---

## HTTPS 支持

Caracal Pro 通过 mbedTLS 支持 HTTPS 连接。默认配置跳过 TLS 证书验证（适合自签名证书的本地网络），可在 menuconfig 中关闭。

### 配置

| 选项 | 位置 | 默认值 | 说明 |
|------|------|--------|------|
| Skip TLS certificate verification | Browser Configuration | 启用 | 跳过证书验证，适合开发/内网 |
| HTTP timeout | Browser Configuration | 15000 ms | HTTPS 握手需要更长时间 |

### 在生产环境中启用证书验证

1. 在 menuconfig 中关闭 `Skip TLS certificate verification`
2. 将 CA 证书 PEM 文件烧录到 SPIFFS
3. 在 `http_fetch.c` 中设置 `config.cert_pem` 指向证书数据

---

## 构建指南

### 前置条件

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) v5.1 或更高版本
- Git

### 第 1 步 — 获取源码

```bash
cd caracal-pro
```

### 第 2 步 — 下载 gumbo-parser

```batch
fetch_deps.bat
```

### 第 3 步 — 配置

```bash
idf.py menuconfig
```

进入 **Browser Configuration**，设置：

| 选项 | 说明 | 默认值 |
|------|------|--------|
| Wi-Fi SSID | 无线网络名称 | `MyWiFi` |
| Wi-Fi Password | 无线网络密码 | `MyPassword` |
| Default URL | 启动页面 | `http://example.com` |
| Skip TLS Verify | 跳过 HTTPS 证书验证 | 启用 |
| LCD / Touch 引脚 | 同基础版 | 见 Kconfig |
| Max HTML Size | 最大抓取缓冲 | `262144`（256 KB） |
| HTTP Timeout | 请求超时 | `15000` ms |

进入 **Component Config → LVGL Configuration**，确认：

- Montserrat 字体 10、12、14、16、20、24 全部启用
- 默认字体 Montserrat 14
- UTF-8 文本编码
- 内存大小至少 384 KB
- 滚动条和文件系统启用

### 第 4 步 — 编译与烧录

```bash
idf.py build
idf.py -p COMx flash monitor
```

---

## 默认引脚映射

与基础版相同，所有引脚通过 `idf.py menuconfig` 可修改。

| 功能 | GPIO | 备注 |
|------|------|------|
| LCD SPI CLK | 14 | |
| LCD SPI MOSI | 13 | |
| LCD SPI MISO | 12 | 可选 |
| LCD DC | 21 | |
| LCD CS | 10 | |
| LCD RST | 11 | |
| LCD BL | 48 | 背光 |
| Touch SDA | 38 | I2C 数据 |
| Touch SCL | 39 | I2C 时钟 |
| Touch INT | 40 | 可选中断 |

---

## 中文 / CJK 字体支持

与基础版相同。使用 [LVGL Font Converter](https://lvgl.io/tools/fontconverter) 生成子集字体，烧录到 SPIFFS 后运行时加载。详见 [基础版 README](../esp32-browser/) 的"中文 / CJK 字体支持"章节。

---

## 内存预算

ESP32-S3 + 8 MB PSRAM，Pro 版默认配置：

| 缓冲区 | 大小 | 位置 |
|--------|------|------|
| HTTP(S) 响应 | 256 KB | PSRAM |
| TLS 握手 | ~40 KB（临时） | PSRAM |
| DOM 树 + CSS style | 约 HTML 大小的 3~4 倍 | PSRAM |
| 布局框 | 约 80~300 KB | PSRAM |
| LVGL 绘制缓冲 | 2 × (480×20×2) = 38.4 KB | PSRAM |
| LVGL 内部内存池 | 384+ KB | PSRAM |
| SPIFFS | 约 12 MB | Flash |

50 KB HTML 页面 PSRAM 总用量约 800 KB~1.2 MB。

---

## 局限与已知问题

- **不支持外部 CSS 文件。** 仅解析内联 `style` 属性，不支持 `<link rel="stylesheet">` 或 `<style>` 标签。
- **Flex/Grid 为简化实现。** 不支持 `order`、`align-self`、`flex-basis: auto` 的内容推断、嵌套 Grid、`grid-column/row` 定位等高级特性。
- **不支持图片解码。** `<img>` 仅渲染为占位框。
- **无表单输入。** 没有屏幕键盘。
- **单页导航。** 无前进/后退历史栈。
- **TLS 证书验证默认关闭。** 生产环境需手动启用并配置 CA 证书。
- **gumbo 源码需手动下载。** 运行 `fetch_deps.bat`。

---

## 故障排查

| 现象 | 可能原因 | 解决方法 |
|------|----------|----------|
| 编译报错 `gumbo source files not found` | 未运行 `fetch_deps.bat` | 运行后重新编译 |
| HTTPS 连接失败 | 证书验证被拒绝 | 检查 `Skip TLS Verify` 是否启用；或配置 CA 证书 |
| HTTPS 握手超时 | 网络慢或服务器问题 | 增大 `HTTP Timeout` |
| Flex/Grid 容器不显示 | style 属性拼写错误 | 检查 HTML 中 `style="display:flex"` 格式 |
| Flex 子项重叠 | 容器宽度不够 | 增大容器 width 或减少子项 |
| Grid 列宽为 0 | grid-template-columns 未设置 | 指定 `grid-template-columns:1fr 1fr` |
| 崩溃 / 看门狗复位 | TLS 握手栈溢出 | 确认 `CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384` |
| 中文显示为方块 | 未加载 CJK 字体 | 见"中文 / CJK 字体支持" |

更多通用故障排查见 [基础版 README](../esp32-browser/)。

---

## 技术决策

### 为什么 Flex/Grid 作为独立分支而不是条件编译？

条件编译（`#ifdef ENABLE_FLEX`）虽然可以合并代码，但会导致：
- `css_style_t` 结构体即使全零也会增加每个 DOM 节点的大小（约 120 字节/节点）
- 布局分发器的 `if/else` 分支增加基础路径的代码复杂度
- 测试矩阵翻倍（每个功能组合都要验证）

独立分支让两个版本各自保持最简代码路径，降低维护负担。

### 为什么只支持内联 style 属性，不支持 CSS 文件？

完整的 CSS 解析器（选择器匹配、级联、继承）在 MCU 上的代价远超 Flex/Grid 布局本身。内联 `style` 属性是零开销方案：gumbo 已经解析了属性值，只需一次 `css_style_parse()` 调用即可获得结构化样式数据，无需选择器引擎。

### 为什么 TLS 证书验证默认关闭？

ESP32-S3 没有预装 CA 证书存储。对于典型使用场景（局域网 IoT 面板、自签名证书），强制验证会导致所有 HTTPS 连接失败。开发者可以在部署时启用验证并配置 CA 证书。

### 为什么不用 `esp_lcd_panel_init()`？

同基础版。ST7789 驱动的 `init()` 与 ST7796S 冲突，Caracal Pro 使用自定义初始化序列。

---

## 许可证

MIT

---

---

# Caracal Pro (English)

Enhanced lightweight HTML browser for ESP32-S3 — adds Flex layout, Grid layout, and HTTPS on top of [Caracal](../esp32-browser/).

---

## Differences from Base Version

| Feature | Caracal | Caracal Pro |
|---------|---------|-------------|
| Block linear layout | ✅ | ✅ |
| Flex layout | ❌ | ✅ |
| Grid layout | ❌ | ✅ |
| CSS style attribute parsing | ❌ | ✅ |
| HTTPS (TLS) | ❌ | ✅ |
| JavaScript | ❌ | ❌ |
| Image decoding | ❌ | ❌ |
| Firmware size | Smaller | +80 KB |
| Main task stack | 8 KB | 16 KB |
| PSRAM usage | ~1 MB | ~1.5 MB |

If you only need HTTP LAN pages, use the lighter [Caracal base version](../esp32-browser/).

---

## What It Does

Caracal Pro fetches HTML pages over HTTP/HTTPS, parses CSS `style` attributes from the DOM, dispatches to block, Flex, or Grid layout engines based on the `display` property, and renders via LVGL.

**Supported:**
- Wi-Fi STA connection (WPA2)
- HTTP / HTTPS GET with redirect following
- HTML5 parsing via gumbo-parser
- CSS `style` attribute parsing (display, flex-*, grid-*, gap, padding, margin, width/height, font-size, color)
- Block linear layout with automatic word wrap (CJK-aware)
- **Flex layout**: flex-direction, justify-content, align-items, flex-wrap, flex-grow/shrink, gap
- **Grid layout**: grid-template-columns/rows, fr/px/% units, gap, auto-flow
- Text rendering: `<p>`, `<h1>`–`<h6>`, `<div>`, `<pre>`, `<blockquote>`, `<li>`
- Image placeholders: `<img>` rendered as labeled bordered boxes
- Clickable hyperlinks: `<a>` with blue underline + tap navigation
- Horizontal rules: `<hr>`
- UTF-8 text (ASCII out of the box; CJK requires custom font)
- Vertical scrolling for long pages
- Touch input (FT6x36 / CST816S / GT911 compatible)

**Not supported (by design):**
- JavaScript
- Image decoding
- External CSS files (inline `style` attributes only)
- Tabs or multiple windows

---

## Hardware Requirements

| Component | Specification |
|-----------|--------------|
| SoC | ESP32-S3 (dual-core 240 MHz) |
| PSRAM | 8 MB Octal SPI |
| Flash | 16 MB |
| LCD | ST7796S, 480×320, SPI interface |
| Touch | I2C capacitive (FT6336 / CST816S / GT911) |

---

## Project Structure

```
caracal-pro/
├── CMakeLists.txt               # Top-level build
├── sdkconfig.defaults            # ESP32-S3 + PSRAM + TLS defaults
├── partitions.csv                # 4MB app + SPIFFS
├── fetch_deps.bat                # Download gumbo-parser source
│
├── main/
│   ├── CMakeLists.txt            # Includes mbedtls dependency
│   ├── idf_component.yml         # LVGL 8.3 via component registry
│   ├── Kconfig.projbuild         # Pin + TLS configuration
│   ├── main.c
│   ├── wifi_sta.c / .h
│   ├── http_fetch.c / .h         # HTTP + HTTPS
│   ├── html_dom.c / .h           # gumbo → DOM (with CSS style)
│   ├── css_style.c / .h          # ★ CSS style attribute parser
│   ├── layout.c / .h             # Layout dispatcher (block/flex/grid)
│   ├── layout_flex.c / .h        # ★ Flex layout engine
│   ├── layout_grid.c / .h        # ★ Grid layout engine
│   ├── renderer.c / .h           # Renderer (with flex/grid container bg)
│   ├── display.c / .h
│   ├── touch.c / .h
│   ├── browser.c / .h
│   └── lv_mem_psram.c / .h
│
└── components/
    └── gumbo/
        └── CMakeLists.txt
```

★ = Pro-only modules.

---

## Architecture

```
 ┌──────────┐    ┌────────────┐    ┌──────────┐    ┌──────────┐
 │  Wi-Fi   │───▶│ HTTP(S)    │───▶│  gumbo   │───▶│   DOM    │
 │   STA    │    │  → HTML    │    │  parser  │    │ + style  │
 └──────────┘    └────────────┘    └──────────┘    └──────────┘
                            TLS (mbedTLS)                │
                                                         │
                          ┌──────────────────────────────▼──────────────────┐
                          │             Layout Dispatcher (layout.c)         │
                          │  display:flex?  →  Flex layout engine            │
                          │  display:grid?  →  Grid layout engine            │
                          │  otherwise      →  Block linear layout            │
                          └──────────────────────────────┬──────────────────┘
                                                         │
 ┌──────────┐    ┌───────────┐    ┌──────────┐    ┌─────▼──────┐
 │ ST7796S  │◀───│   LVGL    │◀───│ Renderer │◀───│  Layout   │
 │   LCD    │    │ flush_cb  │    │ flex/grid │    │  boxes    │
 └──────────┘    └─────┬─────┘    │ container │    └──────────┘
                       │          │ backgrounds│
                ┌──────▼──────┐   └──────────┘
                │   Touch     │  ──▶  tap link  ──▶  browser_navigate()
                └─────────────┘
```

**Data flow per page load:**

1. `browser_navigate(url)` — sets URL bar, detects http/https
2. `http_fetch()` — GET request (HTTPS auto-enables TLS), response buffered in PSRAM
3. `html_parse()` — gumbo produces DOM tree, **parses `style` attributes** into `css_style_t`
4. `layout_compute()` — walks DOM, **dispatches by `display` property**:
   - `display:flex` → `layout_flex()` — measure items, distribute main-axis space, apply justify/align
   - `display:grid` → `layout_grid()` — parse track templates, place items in cells
   - otherwise → block linear layout (same as base)
5. `renderer_render()` — Flex/Grid containers get tinted background boxes, children positioned relative
6. Link taps invoke `on_link_click()` → URL resolution → `browser_navigate()`

---

## CSS Style Attribute Support

Caracal Pro parses HTML element `style=""` attributes for styling. External CSS files and `<style>` tags are not supported.

### Supported Properties

| Property | Values | Description |
|----------|--------|-------------|
| `display` | `block`, `flex`, `grid`, `inline`, `none` | Layout mode; `none` hides element |
| `flex-direction` | `row`, `column` | Flex main axis direction |
| `justify-content` | `flex-start`, `center`, `flex-end`, `space-between` | Flex main-axis alignment |
| `align-items` | `flex-start`, `center`, `flex-end`, `stretch` | Flex cross-axis alignment |
| `flex-wrap` | `nowrap`, `wrap` | Flex wrapping |
| `flex-grow` | integer | Flex grow factor |
| `flex-shrink` | integer | Flex shrink factor |
| `flex-basis` | `auto`, `Npx` | Flex basis size |
| `grid-template-columns` | `Npx`, `N%`, `1fr`, `auto` | Grid column track template |
| `grid-template-rows` | `Npx`, `N%`, `1fr`, `auto` | Grid row track template |
| `gap` | `Npx` | Flex/Grid gap |
| `padding` | `Npx` | Inner padding |
| `margin` | `Npx` | Outer margin |
| `width` | `auto`, `Npx` | Width |
| `height` | `auto`, `Npx` | Height |
| `font-size` | `Npx` | Font size |
| `color` | `#RRGGBB` | Text color |

### Examples

```html
<!-- Flex horizontal nav bar -->
<nav style="display:flex; gap:8px; justify-content:space-between">
  <a href="/">Home</a>
  <a href="/about">About</a>
  <a href="/contact">Contact</a>
</nav>

<!-- Grid two-column card layout -->
<div style="display:grid; grid-template-columns:1fr 1fr; gap:12px">
  <div style="padding:8px">Card 1</div>
  <div style="padding:8px">Card 2</div>
  <div style="padding:8px">Card 3</div>
  <div style="padding:8px">Card 4</div>
</div>

<!-- Flex column, centered -->
<div style="display:flex; flex-direction:column; align-items:center; gap:4px">
  <h2>Title</h2>
  <p>Body text</p>
</div>

<!-- Hidden element -->
<div style="display:none">This won't render</div>
```

### Semantic Tag Auto-Detection

`<nav>` elements without an explicit `style` are automatically detected as `display:flex; flex-direction:row; gap:8px`.

---

## HTTPS Support

Caracal Pro supports HTTPS via mbedTLS. By default, TLS certificate verification is skipped (suitable for self-signed certs on local networks). This can be disabled in menuconfig.

### Configuration

| Option | Location | Default | Description |
|--------|----------|---------|-------------|
| Skip TLS certificate verification | Browser Configuration | Enabled | Skip cert verification; suitable for dev/LAN |
| HTTP timeout | Browser Configuration | 15000 ms | HTTPS handshake needs more time |

### Enabling certificate verification in production

1. Disable `Skip TLS certificate verification` in menuconfig
2. Flash CA certificate PEM to SPIFFS
3. Set `config.cert_pem` in `http_fetch.c` to point to the certificate data

---

## Build Instructions

### Prerequisites

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) v5.1 or later
- Git

### Step 1 — Get the source

```bash
cd caracal-pro
```

### Step 2 — Fetch gumbo-parser

```batch
fetch_deps.bat
```

### Step 3 — Configure

```bash
idf.py menuconfig
```

In **Browser Configuration**, set:

| Option | Description | Default |
|--------|-------------|---------|
| Wi-Fi SSID | Access point name | `MyWiFi` |
| Wi-Fi Password | Access point password | `MyPassword` |
| Default URL | Startup page | `http://example.com` |
| Skip TLS Verify | Skip HTTPS cert verification | Enabled |
| LCD / Touch pins | Same as base | See Kconfig |
| Max HTML Size | Maximum fetch buffer | `262144` (256 KB) |
| HTTP Timeout | Request timeout | `15000` ms |

In **Component Config → LVGL Configuration**, verify:

- Montserrat fonts 10, 12, 14, 16, 20, 24 — all enabled
- Default font: Montserrat 14
- UTF-8 text encoding
- Memory size: at least 384 KB
- Scrollbar and filesystem enabled

### Step 4 — Build and flash

```bash
idf.py build
idf.py -p COMx flash monitor
```

---

## Default Pin Mapping

Same as base version. All pins configurable via `idf.py menuconfig`.

| Function | GPIO | Notes |
|----------|------|-------|
| LCD SPI CLK | 14 | |
| LCD SPI MOSI | 13 | |
| LCD SPI MISO | 12 | Optional |
| LCD DC | 21 | |
| LCD CS | 10 | |
| LCD RST | 11 | |
| LCD BL | 48 | Backlight |
| Touch SDA | 38 | I2C data |
| Touch SCL | 39 | I2C clock |
| Touch INT | 40 | Optional interrupt |

---

## CJK / Chinese Font Support

Same as base version. Use [LVGL Font Converter](https://lvgl.io/tools/fontconverter) to generate a subset font, flash to SPIFFS, and load at runtime. See the [base README](../esp32-browser/) for details.

---

## Memory Budget

ESP32-S3 + 8 MB PSRAM, Pro default configuration:

| Buffer | Size | Location |
|--------|------|----------|
| HTTP(S) response | 256 KB | PSRAM |
| TLS handshake | ~40 KB (temporary) | PSRAM |
| DOM tree + CSS style | ~3–4× HTML size | PSRAM |
| Layout boxes | ~80–300 KB | PSRAM |
| LVGL draw buffers | 2 × (480×20×2) = 38.4 KB | PSRAM |
| LVGL internal pool | 384+ KB | PSRAM (via malloc) |
| SPIFFS | ~12 MB | Flash |

Total PSRAM usage for a typical 50 KB HTML page: approximately 800 KB–1.2 MB.

---

## Limitations & Known Issues

- **No external CSS files.** Only inline `style` attributes are parsed. `<link rel="stylesheet">` and `<style>` tags are ignored.
- **Flex/Grid are simplified implementations.** No `order`, `align-self`, `flex-basis: auto` content inference, nested Grid, or `grid-column/row` placement.
- **No image decoding.** `<img>` renders as a placeholder box only.
- **No form input.** No on-screen keyboard.
- **Single-page navigation.** No back/forward history stack.
- **TLS certificate verification is off by default.** Must be manually enabled and configured with CA certs for production.
- **Gumbo source must be fetched.** Run `fetch_deps.bat` before first build.

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| Build fails: `gumbo source files not found` | Did not run `fetch_deps.bat` | Run `fetch_deps.bat` then rebuild |
| HTTPS connection fails | Certificate rejected | Check `Skip TLS Verify` is enabled; or configure CA cert |
| HTTPS handshake timeout | Slow network or server | Increase `HTTP Timeout` |
| Flex/Grid container not visible | Typo in style attribute | Verify `style="display:flex"` format in HTML |
| Flex children overlap | Container too narrow | Increase container width or reduce children |
| Grid column width is 0 | grid-template-columns not set | Specify `grid-template-columns:1fr 1fr` |
| Crash / watchdog reset | TLS handshake stack overflow | Confirm `CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384` |
| Chinese shows squares | No CJK font loaded | See CJK font section |

For more general troubleshooting, see the [base README](../esp32-browser/).

---

## Technical Decisions

### Why a separate branch instead of conditional compilation?

Conditional compilation (`#ifdef ENABLE_FLEX`) could merge the code, but it would cause:
- `css_style_t` struct (120+ bytes/node) inflating every DOM node even when unused
- Layout dispatcher `if/else` branches adding complexity to the base code path
- Doubled test matrix (every feature combination must be verified)

A separate branch keeps both versions on the simplest possible code path, reducing maintenance burden.

### Why only inline style attributes, not CSS files?

A full CSS parser (selector matching, cascade, inheritance) costs far more on an MCU than the Flex/Grid layout engines themselves. Inline `style` attributes are zero-overhead: gumbo already parses attribute values, and a single `css_style_parse()` call produces structured style data — no selector engine needed.

### Why is TLS certificate verification off by default?

ESP32-S3 has no pre-installed CA certificate store. For typical use cases (LAN IoT dashboards, self-signed certs), mandatory verification would break all HTTPS connections. Developers can enable verification and configure CA certificates at deployment time.

### Why not `esp_lcd_panel_init()`?

Same as base version. The ST7789 driver's `init()` conflicts with ST7796S. Caracal Pro uses a custom init sequence.

---

## License

MIT
