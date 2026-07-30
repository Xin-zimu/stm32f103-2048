# STM32F103 ST7789 多游戏 UI 项目学习指南

本文用于系统学习 `spi_oled DMAui` 项目。它不是单纯的文件列表，而是按项目的设计思路展开：先理解硬件和资源约束，再理解输入、页面、渲染、游戏规则之间如何分层，最后再看 2048 和 Snake 是怎么落到这套架构里的。

项目当前包含：

- HOME 主菜单。
- 2048 游戏页。
- 2048 暂停页和目标模式。
- Snake 实时游戏页。
- 设置页。
- 信息页。
- ST7789 SPI + DMA 局部刷新 UI 框架。

## 1. 先抓住项目的核心矛盾

这个项目运行在 STM32F103C8T6 上，RAM 很紧张。240x240 的 RGB565 屏幕如果做全屏 framebuffer，需要：

```text
240 * 240 * 2 = 115200 字节
```

这远超 STM32F103C8T6 约 20 KB SRAM 的能力。因此项目不能照搬 PC、手机或 LVGL 那类“整屏画布”的思路，必须换一个思维模型：

```text
事件改变状态
状态产生脏区域
脏区域被切成小条带
页面只在条带内重画需要的内容
条带通过 SPI DMA 送给 ST7789
```

也就是说，这个项目的重点不是“怎么画一个漂亮页面”，而是“怎么在很小 RAM 下稳定地画、少画、按时画”。

项目的几个硬约束会影响几乎所有代码设计：

- 不使用全屏 framebuffer。
- 不使用 `malloc/free`。
- 不使用 LVGL。
- 所有运行时数据结构固定大小。
- C/H 文件要求 GB2312 兼容、无 BOM，并且新增或修改函数前要有详细注释。
- Keil 构建必须保持 `0 error(s), 0 warning(s)`。

这些约束解释了为什么项目中大量使用：

- 固定数组。
- 位掩码。
- 小状态机。
- 脏矩形。
- 行合并刷新。
- 页面层和规则层分离。

## 2. 推荐阅读顺序

第一次学习不要从游戏规则开始看。建议按下面顺序读：

1. `User/main.c`：理解主循环。
2. `User/app_ui.c`、`User/ui_page.c`：理解 UI 调度和页面切换。
3. `User/key_driver.c`、`User/ui_event.c`：理解按键如何变成页面事件。
4. `User/ui_dirty.c`、`User/ui_renderer.c`、`User/ui_draw.c`：理解为什么能局部刷新。
5. `User/bsp_st7789.c`、`User/lcd_dma.c`：理解屏幕和 DMA 的边界。
6. `User/game_2048.c`、`User/page_game.c`：理解一个回合制游戏如何接入 UI。
7. `User/game_snake.c`、`User/page_snake.c`：理解一个实时游戏如何接入 UI。
8. `User/page_home.c`、`User/page_pause.c`、`User/page_info.c`：理解辅助页面和调试入口。

这个顺序的好处是：你先看到系统骨架，再看具体游戏，就不会误以为“刷新一格”是驱动层自动完成的。驱动只负责把给定像素送到屏幕，真正决定“哪些区域需要重画”的是页面层和 dirty 系统。

## 3. 启动流程和主循环

入口在 `User/main.c`。

```c
int main(void)
{
    delay_init();
    Timing_Init();
    ST7789_Init();
    Key_Init();
    App_UI_Init();

    while (1)
    {
        uint32_t now;

        now = Timing_GetTick();
        Key_Task(now);
        App_UI_Task(now);
    }
}
```

这个主循环有几个重要特点：

- 没有 RTOS。
- 没有阻塞式页面逻辑。
- 按键扫描和 UI 渲染都靠循环中反复调用任务函数推进。
- `Timing_GetTick()` 提供毫秒时间戳。
- `Key_Task()` 只在到达扫描周期时真正做事。
- `App_UI_Task()` 负责事件分发、页面任务、渲染任务。

可以把主循环理解成一个合作式调度器：

```text
while forever:
    取当前毫秒
    扫描按键，可能产生 UI 事件
    处理 UI 事件
    让当前页面做一点点定时工作
    让渲染器做一点点绘制或 DMA 提交工作
```

这里的“一点点”很重要。单片机上如果某个函数一次干太久，按键就会迟钝，动画和实时游戏也会卡。

## 4. 全局配置：app_config.h

`User/app_config.h` 保存项目级参数。它像一张资源预算表。

关键配置如下：

```c
#define APP_LCD_WIDTH                    240U
#define APP_LCD_HEIGHT                   240U

#define APP_UI_STRIP_HEIGHT                4U
#define APP_UI_STRIP_BUFFER_COUNT          2U
#define APP_UI_DIRTY_RECT_MAX              8U
#define APP_UI_EVENT_QUEUE_SIZE           16U

#define APP_KEY_SCAN_INTERVAL_MS          10U
#define APP_KEY_DEBOUNCE_MS               20U
#define APP_KEY_REPEAT_DELAY_MS          350U
#define APP_KEY_REPEAT_INTERVAL_MS       100U
#define APP_KEY_RST_RESET_MS            2000U
```

这些值不是随便写的：

- `APP_UI_STRIP_HEIGHT = 4`：每个条带 4 像素高。
- 两个条带 buffer 可以让 CPU 和 DMA 分工，但总内存仍可控。
- 一个 240x4 的 RGB565 条带需要 `240 * 4 * 2 = 1920` 字节。
- 两个条带约 3840 字节，比全屏 framebuffer 小很多。
- dirty rect 最多 8 个，超过后会退化为全屏刷新，行为确定，不申请内存。
- 方向键支持长按重复，OK/SET/RST 不重复，避免重开或切页被连续触发。

学习这个项目时要养成一个习惯：看到一个数组或队列，马上问它的最大容量是多少，溢出时怎么处理，是否会导致全屏刷新或丢事件。

## 5. 输入系统：从物理按键到 UI 事件

输入链路分两层：

```text
key_driver.c  物理按键层
ui_event.c    UI 事件层
```

### 5.1 物理按键层

`User/key_driver.c` 直接读 GPIOA 上的摇杆/按键。当前 PA0 到 PA6 使用内部上拉，按下时接地，所以是低电平有效。

按键初始化时会区分是否允许重复：

```c
Key_InitOne(KEY_ID_UP, GPIO_Pin_0, 1U);
Key_InitOne(KEY_ID_DOWN, GPIO_Pin_1, 1U);
Key_InitOne(KEY_ID_LEFT, GPIO_Pin_2, 1U);
Key_InitOne(KEY_ID_RIGHT, GPIO_Pin_3, 1U);
Key_InitOne(KEY_ID_MID, GPIO_Pin_4, 0U);
Key_InitOne(KEY_ID_SET, GPIO_Pin_5, 0U);
Key_InitOne(KEY_ID_RST, GPIO_Pin_6, 0U);
```

这解释了之前遇到的一个现象：方向键可以一直按住移动，OK/MID 不应该连续重开。若 OK/MID 也允许 repeat，用户长按会不断重启游戏，引发多个视觉反馈叠加，看起来像“屏幕保留多个方块然后卡住”。

按键任务的节奏由扫描周期控制：

```c
void Key_Task(uint32_t now)
{
    uint8_t key;

    if ((uint32_t)(now - g_key_last_scan_time) < KEY_SCAN_INTERVAL_MS)
    {
        return;
    }
    g_key_last_scan_time = now;

    for (key = 0U; key < KEY_ID_COUNT; key++)
    {
        Key_UpdateOne((KeyId)key, now);
    }
}
```

这里没有中断式按键，而是轮询。轮询的好处是逻辑简单，消抖可控，不需要为每个 GPIO 配 EXTI，也不会让中断逻辑和 UI 队列互相纠缠。

### 5.2 UI 事件层

`User/ui_event.c` 把物理事件转换成页面能理解的逻辑事件：

```text
UP/DOWN/LEFT/RIGHT -> UI_EVENT_UP/DOWN/LEFT/RIGHT
MID                -> UI_EVENT_OK
SET                -> UI_EVENT_SETTINGS
RST press          -> UI_EVENT_BACK
RST long press     -> UI_EVENT_HOME
RST very long      -> UI_EVENT_SYSTEM_RESET
```

这层的意义是让页面不用关心 GPIO、消抖、长按、重复。页面只需要处理“上、下、确认、返回、设置”这些语义。

事件队列是固定环形队列。它不会动态扩容，因此必须考虑满队列时怎么办。项目里的策略是：高优先级事件可以替换低优先级的方向重复事件。这样快速长按方向键时，不会把 BACK、HOME、SYSTEM_RESET 这类关键事件挤丢。

## 6. 页面系统：所有 UI 都是 Page

页面枚举在 `User/ui_types.h`：

```c
typedef enum
{
    UI_PAGE_HOME = 0,
    UI_PAGE_GAME,
    UI_PAGE_SNAKE,
    UI_PAGE_PAUSE,
    UI_PAGE_SETTINGS,
    UI_PAGE_INFO,
    UI_PAGE_COUNT
} UI_PageId;
```

每个页面都通过 `UI_PageOps` 接入。页面需要提供：

- 进入页面时做什么。
- 收到事件时做什么。
- 定时任务做什么。
- 被要求绘制时怎么画。

`User/ui_page.c` 中注册页面：

```c
static const UI_PageOps * const UI_PAGES[UI_PAGE_COUNT] =
{
    &PAGE_HOME_OPS,
    &PAGE_GAME_OPS,
    &PAGE_SNAKE_OPS,
    &PAGE_PAUSE_OPS,
    &PAGE_SETTINGS_OPS,
    &PAGE_INFO_OPS
};
```

### 6.1 页面分发为什么有全局事件

页面并不是完全独立处理所有事件。`UI_PageDispatchEvent()` 会先处理一些全局语义：

- `SYSTEM_RESET`：系统复位。
- `HOME`：回 HOME。
- `BACK`：大多数页面回 HOME，PAUSE 回 GAME。
- `SETTINGS`：在 2048 中打开 PAUSE，在 Snake 中交给 Snake 做暂停切换，在其他页面打开 SETTINGS。

这样做的原因是：用户不应该在不同页面里学不同的返回逻辑。RST、SET 等系统按键需要保持一致。

Snake 是一个特例。SET 在 2048 里表示暂停菜单，在 Snake 里表示暂停/继续。这个差异通过全局分发控制，而不是让按键层知道“当前是什么游戏”。

## 7. Dirty 系统：为什么不是驱动自动优化

你之前问过：“Snake 每走一步刷新整个 16x16 棋盘区域，这个不是驱动层决定的吗，要每个都单独优化吗？”

答案是：驱动层不理解游戏状态，它只能执行“把这块像素写到屏幕”。是否刷新整个棋盘、刷新一行、还是只刷新几个格子，是页面层和 dirty 系统决定的。

驱动层看到的是：

```text
坐标窗口 x/y/w/h
一段 RGB565 像素数据
```

它不知道：

- 哪个格子是蛇头。
- 哪个格子是尾巴。
- 哪个格子刚合并。
- 哪个格子只是颜色反馈到期。

所以优化刷新必须由懂业务状态的上层完成。

### 7.1 dirty rect 的职责

`User/ui_dirty.c` 维护一个固定大小的脏矩形队列。页面告诉它“这里需要重画”，渲染器从里面取区域。

简化后的逻辑是：

```c
void UI_DirtyAdd(const UI_Rect *rect)
{
    UI_Rect clipped;
    uint8_t index;

    if (g_ui_dirty.full_screen != 0U)
    {
        return;
    }

    if (UI_DirtyClipRect(rect, &clipped) == 0U)
    {
        return;
    }

    for (index = 0U; index < g_ui_dirty.count; index++)
    {
        if (UI_DirtyShouldMerge(&g_ui_dirty.rects[index], &clipped) != 0U)
        {
            UI_DirtyUnionInto(&g_ui_dirty.rects[index], &clipped);
            UI_DirtyUpdateMaxPending();
            return;
        }
    }

    if (g_ui_dirty.count >= UI_DIRTY_MAX_RECTS)
    {
        g_ui_dirty_stats.overflow_count++;
        UI_DirtyFullScreen();
        return;
    }

    g_ui_dirty.rects[g_ui_dirty.count] = clipped;
    g_ui_dirty.count++;
    UI_DirtyUpdateMaxPending();
}
```

这里有三个设计点：

- 所有矩形先裁剪到屏幕内。
- 接近或重叠的矩形会合并，减少 ST7789 开窗次数。
- 队列满了就全屏刷新，并记录 overflow，避免静默丢画面。

这也是 INFO 页里 `DIRTY overflow` 指标有价值的原因：如果它增长，说明页面层提交了太多碎片化刷新请求。

### 7.2 dirty 优化应该在哪里做

以 Snake 为例，最粗的策略是每一步刷新整个棋盘。这样一定正确，但 SPI 传输量大：

```text
176 * 176 * 2 = 61952 字节
```

Snake 每 180 ms 走一步，如果每步都发约 60 KB，虽然可能还能跑，但会明显吃掉总线时间，让按键响应和 UI 流畅度变差。

优化后的策略是：规则层记录本步哪些格子变了，页面层把这些格子按行合并成 dirty rect。

通常一步只变：

- 新蛇头。
- 旧蛇头。
- 旧尾巴。
- 食物位置。

所以 dirty 面积可以从整个 16x16 棋盘降到少数几个格子或几个行内跨度。

这类优化必须懂 Snake 规则，因此放在 `game_snake.c` 和 `page_snake.c`，不是 `lcd_dma.c`。

## 8. 渲染器：没有全屏画布，只有条带

`User/ui_renderer.c` 是这个项目的关键模块之一。

它的核心思想是：

```text
从 dirty 队列取一个矩形
按 4 像素高度切成 strip
让页面在 strip buffer 内重画
把 strip 交给 LCD DMA
等待 DMA 完成
继续下一个 strip
```

条带 buffer 定义：

```c
#define UI_RENDER_STRIP_BYTES \
    (APP_LCD_WIDTH * APP_UI_STRIP_HEIGHT * 2U)

static uint8_t g_ui_strip_storage[APP_UI_STRIP_BUFFER_COUNT][UI_RENDER_STRIP_BYTES];
```

如果 `APP_UI_STRIP_HEIGHT = 4` 且 `APP_UI_STRIP_BUFFER_COUNT = 2`：

```text
每个 buffer = 240 * 4 * 2 = 1920 字节
两个 buffer = 3840 字节
```

这就是项目能在小 RAM 上画 240x240 彩屏 UI 的根本原因。

### 8.1 条带为什么保留 dirty 的 x/w

渲染器准备条带时，不是每次都画满 240 宽，而是保留 dirty rect 的 `x/w`：

```c
g_ui_renderer.strip.x = g_ui_renderer.dirty.x;
g_ui_renderer.strip.y = g_ui_renderer.next_y;
g_ui_renderer.strip.w = g_ui_renderer.dirty.w;
g_ui_renderer.strip.h = strip_h;
```

这样刷新一个 10x10 的 Snake 格子时，不会每条 strip 都传 240 像素宽，而是只传那一小段窗口。

### 8.2 页面 draw 为什么看起来画整页

渲染器调用页面绘制时传入 clip：

```c
UI_DrawBeginBuffer(buffer->data, &buffer->area, &buffer->area);
UI_DrawClearClip(UI_COLOR_BG);
if ((page != 0) && (page->draw != 0))
{
    page->draw(&buffer->area);
}
UI_DrawEndBuffer();
```

很多页面的 `draw()` 函数看起来会画标题、棋盘、文字、按钮。但真正写入 buffer 的只有当前 strip 和 clip 范围内的像素。`ui_draw.c` 的绘图原语会做裁剪。

这是一种很实用的嵌入式 UI 写法：

- 页面代码可以按照“整页怎么画”来写，逻辑简单。
- renderer 和 draw primitive 负责裁剪到当前小条带。
- dirty 系统决定哪些区域需要重新调用 draw。

因此，当你新增页面时，不需要手动判断每个像素是否在 DMA 条带里；你只需要正确提交 dirty 区域，并让 draw 函数在任意 clip 下都能画出正确结果。

## 9. ST7789 和 LCD DMA 边界

`User/bsp_st7789.c` 负责 ST7789 初始化、开窗、写命令和写数据。`User/lcd_dma.c` 负责 SPI2 TX DMA。

这个项目使用的是写屏模型：

```text
设置 ST7789 地址窗口
进入数据写模式
通过 SPI2 + DMA 连续发送 RGB565
```

重点是：ST7789 这里不能读回屏幕内容。项目没有 MISO 读屏能力，也没有全屏 framebuffer 保存屏幕当前内容。因此任何局部刷新都必须能“重新画出这个区域的最终样子”，不能依赖读取旧像素再修补。

这解释了为什么页面的 draw 函数必须是确定性的：

- 给定当前页面状态。
- 给定一个 clip 区域。
- 它必须能从背景开始重新绘制出该区域应有画面。

如果某个 UI 效果只是在旧画面上叠加，却没有到期后的恢复 dirty，就会残影。

## 10. 绘图层：ui_draw.c 的角色

`User/ui_draw.c` 是轻量绘图 API，负责：

- 填充矩形。
- 绘制边框。
- 绘制 ASCII 文本。
- 绘制中文文本。
- 绘制菜单行、状态栏、底部提示等通用组件。

它不保存控件树，也没有布局引擎。页面层直接计算坐标，然后调用绘图函数。

这种方式看起来“原始”，但非常适合本项目：

- 内存可控。
- 调用开销小。
- 每个页面知道自己的布局，不需要通用复杂框架。
- 与 strip renderer 的 clip 模型容易配合。

缺点是：新增复杂 UI 时需要自己管坐标、文本长度和 dirty 区域。因此本项目的页面应保持简洁、固定尺寸、少状态。

## 11. App UI 调度

`User/app_ui.c` 是 UI 系统的总入口。它通常会做三类事情：

```text
处理 UI 事件队列
调用当前页面 task
调用 renderer task
```

这里有一个关键设计：页面定时任务通常要避免在渲染器忙时继续大量提交 dirty。实时游戏 Snake 就依赖这一点，防止上一帧还没刷完，下一步又把新的格子 dirty 加进来，导致队列膨胀。

对于 STM32 上的 UI，流畅不是靠“帧率尽量高”，而是靠：

- 每次变化尽量少画。
- 每次任务尽量快返回。
- DMA 忙时不堆积无意义刷新。
- 用户输入优先级高于装饰动画。

## 12. HOME 页面

`User/page_home.c` 是入口页面。当前菜单包括：

- `2048`
- `SNAKE`
- 设置
- 信息

HOME 不只是跳转列表，还承担“告诉用户当前选中项是什么类型”的轻提示：

```text
选中 2048  -> GOAL PUZZLE
选中 SNAKE -> REALTIME
选中设置/信息 -> 原有导航提示
```

这个小改动背后的设计思想是：主菜单应该在不新增页面、不占用大内存的前提下，让用户知道即将进入的是回合制解谜，还是实时游戏。

HOME 通常只需要在选中项变化时刷新旧行、新行和底部说明，不需要全屏刷新。这和后面的 2048/Snake dirty 思路一致。

## 13. 2048 规则层：game_2048.c

2048 的规则层不画 UI，只维护游戏状态。

核心数据：

```c
static uint8_t g_game2048_board[4][4];
static uint32_t g_game2048_score = 0U;
static uint32_t g_game2048_best_score = 0U;
static uint32_t g_game2048_move_count = 0U;
static uint16_t g_game2048_last_change_mask = 0U;
static uint16_t g_game2048_last_new_tile_mask = 0U;
static uint16_t g_game2048_last_merge_mask = 0U;
static uint8_t g_game2048_goal_exp = GAME2048_DEFAULT_GOAL_EXP;
static Game2048_State g_game2048_state = GAME2048_STATE_PLAYING;
```

棋盘不是存 2、4、8、16，而是存指数：

```text
0 表示空
1 表示 2
2 表示 4
3 表示 8
...
11 表示 2048
```

这样合并时只要指数加一，分数可以由指数换算。它比直接存 tile 数值更小，也更适合 `uint8_t`。

### 13.1 为什么用 mask 记录变化

4x4 棋盘正好 16 格，因此可以用一个 `uint16_t` 表示哪些格子变化：

```text
bit = row * 4 + col
```

例如：

```c
(uint16_t)(1U << ((row * 4U) + col))
```

这些 mask 有不同含义：

- `last_change_mask`：本次有效移动后，哪些格子最终内容变了。
- `last_new_tile_mask`：新生成的 2 或 4 在哪里。
- `last_merge_mask`：本次合并后的目标格在哪里。

这样规则层不需要知道像素坐标，页面层也不需要理解 2048 的合并细节。两层用一个 16 位 mask 对接。

### 13.2 移动算法的思想

一次移动可以拆成四步：

```text
读取一条 line
压缩非空数字
合并相邻相同数字
把结果写回棋盘
```

四个方向的差别只在“line 如何映射到棋盘坐标”。例如向左移动时，一条 line 就是一行从左到右；向右移动时，是一行从右到左；向上/向下则是列。

项目推荐在 `Game2048_WriteLine()` 做 line 位置到棋盘 bit 的映射，原因是页面层不该理解方向和 line 坐标。规则层最清楚“合并后目标位置”实际落在哪个格子。

### 13.3 添加随机块为什么不分配空格列表

添加随机块时，项目没有建立一个空格坐标数组，而是：

1. 先数空格数量。
2. 随机选一个空格序号。
3. 再扫描棋盘找到这个序号对应的空格。

代码片段：

```c
empty_count = Game2048_CountEmpty();
if (empty_count == 0U)
{
    return 0U;
}

target = (uint8_t)(Game2048_Rand() % empty_count);
value = ((Game2048_Rand() % 10U) == 0U) ? 2U : 1U;
seen = 0U;
```

这样做会多扫一遍棋盘，但 4x4 很小，CPU 成本可以忽略。好处是不用临时数组，不用动态内存，行为稳定。

### 13.4 Goal 模式

2048 目标不是写死 2048，而是 `goal_exp`：

```c
#define GAME2048_DEFAULT_GOAL_EXP 11U
#define GAME2048_MIN_GOAL_EXP     7U
#define GAME2048_MAX_GOAL_EXP    11U
```

对应关系：

```text
7  -> 128
8  -> 256
9  -> 512
10 -> 1024
11 -> 2048
```

这让 PAUSE 页面可以循环设置目标，而不需要复制一套游戏规则。

## 14. 2048 页面层：page_game.c

`User/page_game.c` 把 2048 规则状态画到屏幕上，并负责用户输入、局部刷新和视觉反馈。

关键布局：

```text
顶部：目标、步数、分数、最高分
中间：4x4 棋盘
底部：按键提示
覆盖层：WIN / GAME OVER
```

### 14.1 输入语义

2048 页面保持以下语义：

```text
方向键：移动
OK/MID：重开
SET：暂停
RST：返回
```

方向输入有节流，避免方向键 repeat 太快导致 UI 来不及刷新。OK 有重开保护，避免长按或抖动造成多次 restart。

### 14.2 2048 dirty 策略

`Page_Game_InvalidateChangedCells(mask)` 根据 16 位 mask 找出每一行最左和最右变化列，然后每行最多提交一个 dirty rect。

思想是：

```text
不要每格都提交 dirty
也不要整棋盘刷新
按行把变化格合并成小区域
```

这样一次移动最多提交 4 个棋盘区域，加上 header/footer/overlay 也不容易撑爆 dirty 队列。

### 14.3 新块反馈和合并反馈

页面层维护短暂反馈状态：

```c
static uint16_t g_page_game_new_flash_mask = 0U;
static uint16_t g_page_game_merge_flash_mask = 0U;
static uint32_t g_page_game_new_flash_until_ms = 0U;
static uint32_t g_page_game_merge_flash_until_ms = 0U;
```

新块反馈和合并反馈都不是动画对象，而是“在一段时间内改变目标格绘制样式”。到期后再把这些格子标 dirty，让它们恢复正常样式。

绘制优先级是：

```text
合并反馈 > 新块反馈 > 普通 tile
```

片段：

```c
merge_active = ((g_page_game_merge_flash_mask &
    (uint16_t)(1U << ((row * 4U) + col))) != 0U) ? 1U : 0U;
flash_active = ((g_page_game_new_flash_mask &
    (uint16_t)(1U << ((row * 4U) + col))) != 0U) ? 1U : 0U;
if (merge_active != 0U)
{
    fill = UI_COLOR_ACCENT;
    text_color = UI_COLOR_BG;
}
```

这解释了为什么“合并反馈不是驱动层功能”：它取决于 2048 规则层给出的 merge mask，页面层据此选择颜色，dirty 系统再负责让这些格子重画。

### 14.4 反馈到期恢复

视觉反馈最容易出残影的地方是“开始时画了高亮，到期后忘了恢复”。项目的做法是在 `Page_Game_Task()` 检查时间，到期就清 mask，并把旧 mask 对应格子重新 dirty。

简化逻辑：

```c
if ((g_page_game_merge_flash_mask != 0U) &&
    ((int32_t)(now - g_page_game_merge_flash_until_ms) >= 0))
{
    expired_mask |= g_page_game_merge_flash_mask;
    g_page_game_merge_flash_mask = 0U;
    g_page_game_merge_flash_until_ms = 0U;
}

if (expired_mask != 0U)
{
    Page_Game_InvalidateChangedCells(expired_mask);
    g_page_game_input_locked = 1U;
    return;
}
```

这里的重点是：清状态和提交恢复 dirty 必须成对出现。

## 15. 2048 暂停页：page_pause.c

暂停页不是通用系统暂停，而是 2048 的功能页。它包含：

- RESUME
- NEW GAME
- GOAL
- HOME

GOAL 项负责循环目标值：

```text
128 -> 256 -> 512 -> 1024 -> 2048 -> 128
```

暂停页和 2048 规则层之间通过 `Game2048_SetGoalExp()` 连接。页面层只负责菜单和调用接口，不直接改棋盘内部变量。

这个分层很重要：如果以后把 2048 页面样式换掉，goal 规则仍然可以保留；如果以后 goal 模式扩展，也不需要 HOME 或按键驱动知道细节。

## 16. Snake 规则层：game_snake.c

Snake 是实时游戏，和 2048 最大区别是：即使用户不按键，游戏也会按时间自动前进。

核心数据：

```c
static uint8_t g_snake_body_row[SNAKE_MAX_CELLS];
static uint8_t g_snake_body_col[SNAKE_MAX_CELLS];
static uint16_t g_snake_length = 0U;
static uint8_t g_snake_food_row = 0U;
static uint8_t g_snake_food_col = 0U;
static Snake_Direction g_snake_dir = SNAKE_DIR_RIGHT;
static Snake_Direction g_snake_pending_dir = SNAKE_DIR_RIGHT;
static Snake_State g_snake_state = SNAKE_STATE_RUNNING;
static uint16_t g_snake_last_dirty_rows[SNAKE_GRID_SIZE];
```

Snake 棋盘是 16x16，因此最多 256 格。蛇身数组固定为最大格数，不动态申请。

### 16.1 为什么蛇身用两个数组

蛇身位置用两个平行数组保存：

```text
g_snake_body_row[index]
g_snake_body_col[index]
```

`index = 0` 是蛇头，后面依次是身体。这样移动时可以从尾到头整体后移：

```text
body[n] = body[n - 1]
...
body[1] = body[0]
body[0] = new_head
```

它简单、确定、内存固定。256 格最大长度也只是两个 256 字节数组，加起来约 512 字节。

### 16.2 方向输入为什么有 pending_dir

Snake 有当前方向 `g_snake_dir` 和待应用方向 `g_snake_pending_dir`。页面收到方向键后先设置 pending，真正走一步时再应用。

这样做有两个好处：

- 输入和移动节拍解耦。
- 防止一帧内多次方向变化造成不符合规则的转向。

同时项目会拒绝反向移动：

```c
if ((g_snake_dir == SNAKE_DIR_UP) && (dir == SNAKE_DIR_DOWN))
{
    return 1U;
}
```

直接反向会让蛇头撞到第一节身体，不符合常见 Snake 操作。

### 16.3 一步移动如何判断碰撞

Snake 每步大致流程：

```text
清空本步 dirty 记录
如果不是 RUNNING，返回
应用 pending_dir
算出新蛇头
检查墙碰撞
判断是否吃到食物
检查身体碰撞
移动身体数组
如果吃到食物，增长并放新食物
标记本步变化格子
```

身体碰撞有个细节：如果本步没有吃食物，尾巴会移走。因此检测新头是否撞身体时，可以忽略当前尾巴。否则蛇头走到“尾巴即将离开的位置”会被误判为撞自己。

这是规则层必须处理的逻辑，页面层不应该知道。

### 16.4 Snake 的 dirty 记录

优化后的 Snake 不再每步刷新整个棋盘，而是规则层记录本步变化的格子。

内部用每行一个 16 位 mask：

```c
static uint16_t g_snake_last_dirty_rows[SNAKE_GRID_SIZE];
```

标记一个格子：

```c
static void Snake_MarkDirtyCell(uint8_t row, uint8_t col)
{
    if ((row >= SNAKE_GRID_SIZE) || (col >= SNAKE_GRID_SIZE))
    {
        return;
    }

    g_snake_last_dirty_rows[row] |= (uint16_t)(1U << col);
}
```

为什么 Snake 用“每行 16 位”，2048 用“整盘 16 位”？

- 2048 是 4x4，整盘正好 16 格，一个 `uint16_t` 足够。
- Snake 是 16x16，共 256 格，整盘要 256 位。
- 用 16 行 `uint16_t` 正好表达 16x16，处理行内合并也方便。

这就是数据结构跟业务尺寸匹配的例子。

## 17. Snake 页面层：page_snake.c

Snake 页面负责：

- 每 180 ms 推进一步。
- 方向键改变 pending direction。
- OK 重开。
- SET 暂停/继续。
- 绘制 16x16 棋盘、食物、蛇身、分数和状态覆盖层。

关键配置：

```c
#define PAGE_SNAKE_BOARD_X       32
#define PAGE_SNAKE_BOARD_Y       34
#define PAGE_SNAKE_BOARD_SIZE   176
#define PAGE_SNAKE_CELL          10
#define PAGE_SNAKE_STEP          11
#define PAGE_SNAKE_TICK_MS      180U
```

16 个格子，每格 10 像素，中间 1 像素间隔：

```text
16 * 10 + 15 * 1 = 175
```

项目用 176 的棋盘区域，坐标和边框处理更整齐。

### 17.1 Snake 的定时任务

Snake 的自动前进在页面 task 中完成：

```c
if ((uint32_t)(now - g_page_snake_last_step_ms) < PAGE_SNAKE_TICK_MS)
{
    return;
}

g_page_snake_last_step_ms = now;
score_before = Snake_GetScore();
state_before = Snake_GetState();
if (Snake_Step() == 0U)
{
    return;
}

Page_Snake_InvalidateChangedCells();
```

这里注意两层关系：

- `Snake_Step()` 改变规则状态。
- `Page_Snake_InvalidateChangedCells()` 把规则层 dirty mask 转成屏幕 dirty rect。

规则层不知道屏幕坐标，页面层不知道蛇身数组怎么移动。这个分工很清楚。

### 17.2 Snake dirty 从整棋盘到局部刷新

最早 Snake 每走一步刷新整个 16x16 棋盘区域，容易理解，但效率不高。优化后页面读取每行 dirty mask，把连续变化列合并为一段屏幕矩形。

思路：

```text
for 每一行:
    读取该行 dirty mask
    找连续变化列
    把连续列对应的像素区域提交 dirty
```

如果本步只移动了一格，不吃食物，通常变化格子只有新头和旧尾，刷新区域非常小。

这就是“每个游戏都要按自己的规则优化”的原因：Snake 知道头尾变化，2048 知道哪些格子滑动/合并，HOME 知道选中行变化。驱动层无法替你推断这些。

## 18. INFO 页面：运行时观察窗口

`User/page_info.c` 是调试 UI 性能的重要页面。它显示类似：

- STRIP：条带绘制数量。
- KB：提交到屏幕的数据量。
- BUSY：渲染器忙状态。
- DMA：DMA 状态。
- DIRTY：dirty 队列统计。

你上板测试局部刷新优化时，INFO 页比肉眼更可靠。

例如测试 Snake dirty 优化：

1. 进入 INFO 页，OK 清统计。
2. 回 HOME，进入 SNAKE。
3. 让蛇走几十步。
4. 回 INFO 看 `KB` 和 `DIRTY overflow`。

预期是：

- `DIRTY overflow` 不增长。
- `KB` 不再接近每步整棋盘刷新水平。
- 快速转向时无明显残影和卡顿。

## 19. Settings 页面

`User/page_settings.c` 当前更多是 UI 框架验证页，包含设置项、焦点动画、数值变化等。它展示了：

- 菜单行怎么画。
- 焦点移动怎么 dirty。
- footer 和 status 怎么刷新。
- 简单参数怎么保存在 RAM 变量中。

这里的设置目前不是系统级持久配置。断电后不会保存，因为项目没有接入 Flash 参数存储。以后如果要加持久化，应该新建独立配置模块，而不是让 Settings 页面直接写 Flash。

## 20. 反馈和动画模块

`User/ui_feedback.c` 和 `User/ui_anim.c` 是轻量 UI 状态辅助模块。

它们的定位不是“做复杂动画引擎”，而是提供：

- 按下反馈时间。
- 焦点移动插值。
- 少量固定槽位的动画状态。

为什么不能随便加复杂动画？

- 动画意味着不断产生 dirty。
- dirty 多了会增加 SPI 传输和 DMA 排队。
- 实时游戏和按键响应会受影响。

因此项目中的视觉反馈都偏短、偏局部：

- 2048 新块闪一下。
- 2048 合并格闪一下。
- HOME 焦点滑动。
- 按键有短暂反馈。

这和项目资源约束是一致的。

## 21. 新增一个游戏应该怎么做

这个项目已经有 2048 和 Snake，可以把它们当成两个模板：

```text
2048 代表回合制棋盘游戏
Snake 代表实时网格游戏
```

新增游戏时建议按下面步骤。

### 21.1 先做规则层

新建类似：

```text
User/game_xxx.c
User/game_xxx.h
```

规则层只负责：

- 初始化。
- 重开。
- 处理一步规则。
- 暴露只读状态。
- 暴露本次变化 mask 或 dirty 信息。

规则层不要：

- 画像素。
- 调 UI 页面。
- 直接提交 dirty rect。
- 使用动态内存。

### 21.2 再做页面层

新建类似：

```text
User/page_xxx.c
User/page_xxx.h
```

页面层负责：

- 输入事件映射到规则操作。
- 定时推进。
- 把规则变化转成 dirty rect。
- 绘制当前页面。
- 处理 footer/header/overlay。

页面层可以知道屏幕坐标，但不要把游戏核心规则写在页面里。

### 21.3 接入页面系统

需要改：

- `User/ui_types.h`：新增 `UI_PAGE_XXX`。
- `User/ui_page.c`：把 `PAGE_XXX_OPS` 加到 `UI_PAGES[]`。
- `User/page_home.c`：HOME 菜单增加入口。
- Keil 工程文件：加入新的 `.c` 文件。

如果忘了改 Keil 工程，代码在文件夹里也不会参与构建。

### 21.4 先设计 dirty 策略

新增游戏时不要等画完再想优化。应该先问：

```text
一次操作最多改变哪些格子或区域？
是否可以用 uint16_t 或行 mask 表达？
页面能不能把变化区域按行合并？
最坏情况下会提交多少 dirty rect？
会不会超过 APP_UI_DIRTY_RECT_MAX？
```

如果这几个问题答不清，游戏能跑，但很容易在板上出现卡顿、残影或 dirty overflow。

## 22. 常见坑

### 22.1 `3U` 不能写成 `3 U`

之前 ST7789 SPI mode 配置出现过：

```c
#define ST7789_SPI_MODE 3 U
```

这会导致预处理表达式失败。正确写法是：

```c
#define ST7789_SPI_MODE 3U
```

C 语言整数后缀不能和数字分开。

### 22.2 局部刷新后有残影

常见原因：

- 状态变了但没有提交 dirty。
- 反馈到期清状态后没有提交恢复 dirty。
- draw 函数依赖旧屏幕内容，而不是从当前状态重画。
- dirty rect 太小，没有覆盖旧内容完整区域。

排查时优先看页面层，而不是 LCD 驱动。

### 22.3 按住按键导致异常

方向键允许 repeat，OK/SET/RST 不允许普通 repeat。若出现长按 OK 重复触发，先看 `key_driver.c` 的 repeat 设置和页面层 guard。

实时游戏还要注意：方向键 repeat 可能比游戏 tick 更快，所以规则层需要 pending direction，而不是每个按键事件都立即移动一步。

### 22.4 dirty overflow 增长

如果 INFO 页显示 dirty overflow 增长，说明页面层提交的 dirty rect 太多或太碎。

解决思路：

- 按行合并格子。
- 合并旧位置和新位置。
- 减少动画频率。
- 在 renderer 忙时不要继续排入实时游戏下一步。
- 必要时把多个小 rect 合成一个稍大的 rect。

注意：少量变大的 rect 往往比大量碎 rect 更稳定，因为 ST7789 开窗和队列管理也有成本。

### 22.5 页面 task 不应长时间阻塞

所有页面 task 都应该快速返回。不要在页面 task 里做长延时、循环等待 DMA、循环等待按键释放。否则主循环会被卡住，按键扫描和渲染都会受影响。

## 23. 上板测试思路

学习或修改项目时，建议把测试分成三类。

### 23.1 功能测试

确认用户能完成预期操作：

- HOME 能进入 2048、Snake、设置、信息。
- 2048 方向移动、OK 重开、SET 暂停、RST 返回。
- 2048 goal 模式能切换并触发 WIN。
- Snake 能自动前进、转向、吃食物、撞墙结束、SET 暂停。

### 23.2 刷新测试

观察局部刷新是否正确：

- 2048 移动无残影。
- 2048 新块和合并反馈能恢复。
- Snake 头尾移动无残影。
- Pause/overlay 出现和消失后底图恢复。

### 23.3 性能测试

用 INFO 页确认：

- `DIRTY overflow` 不增长。
- `KB` 没有异常暴涨。
- 长按方向键时 UI 不卡死。
- 快速切页后没有白屏或残留。

## 24. 构建和变更流程

每次修改 STM32 项目都要走固定流程：

```text
start-stm32-change.ps1
修改代码或文档
更新 docs/change-logs
运行文本/注释检查
运行 git diff --check
运行 Keil isolated build
finish-stm32-change.ps1 提交
```

这套流程的目的不是形式主义，而是防止三个常见问题：

- 中文路径、编码或 BOM 导致 Keil/脚本异常。
- 新增函数缺少项目要求的详细注释。
- 本次提交混入无关文件或用户未确认的修改。

文档和脚本使用 UTF-8 无 BOM；C/H 使用 GB2312 兼容文本、无 BOM。

## 25. 一句话总结架构

这个项目可以这样理解：

```text
main.c 提供合作式循环
key_driver.c 把 GPIO 变成物理按键事件
ui_event.c 把物理按键变成 UI 语义事件
ui_page.c 决定事件交给哪个页面
各 page_xxx.c 维护页面交互、提交 dirty、绘制页面
各 game_xxx.c 维护纯游戏规则和变化 mask
ui_dirty.c 管理需要重画的区域
ui_renderer.c 把 dirty 区域切成小 strip
ui_draw.c 在 strip 内画出当前页面
bsp_st7789.c/lcd_dma.c 把 strip 像素写入屏幕
```

如果你以后想判断一个改动该放哪里，可以用这个原则：

```text
和游戏胜负、移动、碰撞有关 -> game_xxx.c
和坐标、颜色、文字、dirty 有关 -> page_xxx.c
和页面切换语义有关 -> ui_page.c
和按键消抖/长按/重复有关 -> key_driver.c 或 ui_event.c
和刷新队列有关 -> ui_dirty.c
和条带/DMA 调度有关 -> ui_renderer.c/lcd_dma.c
和 ST7789 命令有关 -> bsp_st7789.c
```

这个边界守住了，项目就会越来越容易扩展；边界混乱了，后面每加一个游戏都会互相影响。
