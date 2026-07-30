# STM32F103 ST7789 轻量 UI 工程

这是一个基于 STM32F103C8T6、ST7789 240x240 SPI 彩屏和 5D 摇杆的裸机轻量 UI 项目。

当前仓库的主线目标是：在不使用 LVGL、不使用动态内存、不使用全帧缓冲的前提下，实现可读、可操作、可继续扩展的嵌入式 UI。

## 当前状态

当前构建是 **UI-first 版本**：

- 已接入 5D 摇杆按键输入。
- 已完成 HOME / PLAYER / SETTINGS / INFO 页面框架。
- 已实现脏矩形局部刷新。
- 已实现 UI renderer 条带双缓冲，页面绘制先写入最高 240x4 RGB565 条带 buffer，再按 dirty 的 `x/w` 窄窗口经 LCD DMA 送屏。
- 已实现 16x16 中文子集字库和放大 ASCII 字体。
- 已实现 HOME / SETTINGS 焦点条动画。
- 已实现 PLAYER 程序生成动画，用扫描条、移动块、波形、轨道点、拖尾块和帧计数验证持续局部刷新。
- 已修复焦点条抽动、快速移动黄色残留和蓝条断裂问题。
- 已统一 HOME / PLAYER / SETTINGS 的短按反馈，HOME 进入页面前会显示一次 pressed 反馈。
- 已提供 renderer / dirty 轻量统计接口，INFO 页可观察条带数、发送字节、renderer 忙、DMA 忙和 dirty 积压。
- 当前 STM32F103C8 Flash/RAM 不适合内置真实 GIF；GIF 播放源码和历史文档只作为资料保留，不再作为当前主线计划。

当前最新验证构建结果：

```text
0 Error(s), 0 Warning(s)
Code=13348
RO-data=1988
RW-data=40
ZI-data=6408
```

## 硬件

### ST7789

| 功能 | STM32F103C8T6 | 说明 |
| --- | --- | --- |
| SCK | PB13 | SPI2 SCK |
| MOSI | PB15 | SPI2 MOSI |
| RES | PB10 | LCD 复位 |
| DC | PB14 | 命令/数据选择 |
| BLK | PB12 | 背光控制 |
| CS | 无 | 当前屏幕模块无外部 CS |

注意：

- SCL/SDA 在该屏幕上是 SPI 信号，不是 I2C。
- 屏幕无 MISO，STM32 只向屏幕发送数据。
- 当前驱动配置为 240x240、SPI Mode 3、无 CS。

### 5D 摇杆

5D 摇杆按 **7 个共地数字按键** 使用，不需要 SPI、ADC 或屏幕接口。

| 摇杆功能 | STM32F103C8T6 | 配置 |
| --- | --- | --- |
| COM | GND | 公共地 |
| UP | PA0 | 输入上拉 |
| DOWN | PA1 | 输入上拉 |
| LEFT | PA2 | 输入上拉 |
| RIGHT | PA3 | 输入上拉 |
| MID | PA4 | 输入上拉 |
| SET | PA5 | 输入上拉 |
| RST | PA6 | 输入上拉 |

按键电平：

```text
未按下：高电平
按下：低电平
```

保留接口：

| 功能 | 引脚 |
| --- | --- |
| USART1 TX | PA9 |
| USART1 RX | PA10 |
| SWDIO | PA13 |
| SWCLK | PA14 |

不要占用 PA13/PA14，否则会影响 ST-Link 下载和调试。

## 软件架构

```text
key_driver
    10 ms 扫描、20 ms 消抖、长按、连发、RST 复位
        ↓
ui_event
    固定环形事件队列，物理按键转 UI 事件
        ↓
app_ui
    每轮处理有限数量事件，避免连发阻塞绘制
        ↓
ui_page
    页面路由、全局事件、页面 task、脏区绘制
        ↓
page_home / page_player / page_settings / page_info
        ↓
ui_draw / ui_font
    中文子集、大号 ASCII、菜单行、状态栏、页脚、控件绘制
        ↓
ui_dirty
    脏矩形队列、裁剪、合并、FIFO 消费、积压统计
        ↓
ui_renderer
    240x4 条带双缓冲、页面 buffer 绘制、LCD DMA 提交、忙状态统计
        ↓
bsp_st7789
    ST7789 SPI/DMA 底层驱动
```

## 当前页面

### HOME

中文大字号菜单：

- 动画播放器
- 设置
- 系统信息

UP/DOWN 移动焦点，MID/RIGHT 进入。

### PLAYER

当前是轻量程序动画验证页：

- 明确显示不内置 GIF。
- 使用扫描条、移动块、波形条、轨道点、背景短线、拖尾块和帧计数生成动画，不存图片帧。
- RIGHT 从头播放。
- MID/OK 播放/暂停切换。
- LEFT 返回。
- 每帧只刷新动画画布区域，播放/暂停只刷新状态区。

### SETTINGS

当前设置项：

- 动画：开/关
- 亮度：1..5
- 主题：CYAN / GOLD / GREEN

LEFT 返回，RIGHT/MID 修改当前项。

### INFO

显示进入 INFO 时的条带提交数、累计发送 KB、renderer 忙返回数、DMA 忙返回数和 dirty 队列 max/current/overflow。OK/MID 清零 renderer/dirty 统计并显示清零快照，RIGHT 只重新抓取一次统计快照，避免 renderer 分条带绘制时同一个数字由不同计数值拼接。

## 关键实现

### 不使用动态内存

项目不使用 `malloc()` / `free()`。事件队列、脏矩形队列、动画状态均为固定静态存储。

示例：

```c
static UI_Event items[16];
static UI_Rect rects[8];
static UI_FocusAnim g_home_focus_anim;
```

### 不使用全帧缓冲

240x240 RGB565 全屏 framebuffer 需要：

```text
240 x 240 x 2 = 115200 字节
```

STM32F103C8T6 RAM 只有约 20 KB，因此不能保存整屏图像。当前策略是：

```text
哪里变化，就刷新哪里
```

### 条带双缓冲刷新

普通 UI 变化使用 `UI_DirtyAdd()`，会裁剪并合并相近区域。`ui_renderer` 从 dirty 队列取出刷新范围后，保留 dirty 的 `x/w`，再按 4 像素高切成窄条带：

```text
dirty x/w + Y 范围
    ↓
最多 240 x 4 RGB565 strip buffer
    ↓
ST7789 address window
    ↓
SPI2 TX DMA
```

当前没有使用 240x240 全帧缓冲，只保留两个最大 240x4 条带 buffer：

```text
240 x 4 x 2 x 2 = 3840 字节
```

焦点动画在 renderer 忙时会暂停推进，避免同一个 dirty 的不同条带使用不同焦点位置绘制。dirty 队列按 FIFO 消费，避免旧行清理被快速按键产生的新 dirty 长时间压住。

### 调试统计

当前提供两组轻量统计，主要给调试器和后续 UI 动画调度读取：

- `UI_DirtyGetStats()`：dirty 溢出次数、全屏升级次数、最大 pending 数、当前 pending 数。
- `UI_RendererGetStats()`：renderer 调用次数、忙返回次数、DMA 忙次数、无空 buffer 次数、条带绘制和提交次数。

这些接口不改变屏幕显示行为，也不依赖串口输出。

### 窄条带刷新状态

已完成条带双缓冲和 `ui_draw` buffer 绘制改造。renderer 现在按 dirty 的 `x/w` 发送窄区域条带；`ui_draw` 写入 buffer 时使用相对条带坐标，避免窄窗口发送错位。上板快速上下移动验证过的黄色历史残留和蓝条断裂修复仍保留。

## 目录说明

```text
Project/
    Keil MDK 工程文件

User/
    应用层、UI、按键、ST7789 驱动

SYSTEM/
    delay、timing 等系统辅助模块

Libraries/
    STM32F10x 标准外设库

Tools/
    历史 GIF 转换等辅助脚本

docs/
    学习文档、历史排障文档、变更日志
```

重点文档：

- [UI 开发学习文档](docs/ui-development-learning-guide.md)
- [ST7789 逻辑分析仪排障指南](docs/st7789-logic-analyzer-troubleshooting-guide.md)
- [ST7789 差分刷新优化学习笔记](docs/st7789-delta-optimization-learning.md)
- [历史 GIF 动画实现说明](docs/spi_oled_gif_animation.md)

## 编译

使用 Keil MDK 打开：

```text
Project/led.uvprojx
```

步骤：

1. 打开 Keil 工程。
2. 执行 `Rebuild`。
3. 确认 `0 Error(s), 0 Warning(s)`。
4. 使用 ST-Link 下载。
5. 复位开发板。

当前主循环入口：

```c
int main(void)
{
    ...
    Key_Init();
    App_UI_Init();

    while (1)
    {
        now = Timing_GetTick();
        Key_Task(now);
        App_UI_Task(now);
    }
}
```

## 当前按键语义

| 按键 | 功能 |
| --- | --- |
| UP | 上移焦点 |
| DOWN | 下移焦点 |
| LEFT | 返回 |
| RIGHT | 进入 / 修改 |
| MID | 确认 / 修改 |
| SET | 打开设置 |
| RST 短按 | 返回 |
| RST 长按约 600 ms | 回主页 |
| RST 持续约 2 秒 | 软件复位 |

## 媒体边界

当前主线不再尝试把真实 GIF 帧数据内置进 STM32F103C8 固件。原因很直接：

```text
Flash 空间紧张
RAM 无法承受整帧缓存
真实 GIF 帧会挤压 UI、字库和后续功能空间
```

如果以后必须播放真实图片动画，应换成外部 SPI Flash、SD 卡或更大 Flash/RAM 的 MCU。当前板子上只做程序生成动画。

## 后续计划

下一阶段继续完善轻量 UI 组件：

1. 上板进入 INFO 后按 OK/MID 清零，再打开 PLAYER 播放动画；回到 INFO 后观察 `KB`、`BUSY`、`DMA`、`DIRTY`，需要刷新统计时按 RIGHT。
2. 如果 `DMA` 或 `BUSY` 增长过快，优先降低 PLAYER 动画刷新频率或继续合并小 dirty。
3. 如果 `DIRTY` 第三位 overflow 变动，说明 dirty 队列发生过全屏升级，需要减少同一轮入队数量。
4. 保持历史 GIF 工具和文档在 `Tools/`、`docs/` 中，但不纳入当前 Keil 主线。
