# 新增贪吃蛇游戏

日期：2026-07-31

## 修改目标

在当前 STM32F103 + ST7789 轻量 UI 工程中新增第二个小游戏：贪吃蛇。
实现必须继续保持固定静态内存、无 malloc/free、无 LVGL、无全屏 framebuffer。

## 修改前行为

HOME 页面只有 2048、设置和系统信息入口。页面路由中只有 2048 游戏页面和
2048 专用 PAUSE 页面，没有第二个游戏。

## 修改后行为

HOME 页面新增 `SNAKE` 入口。Snake 使用 16x16 棋盘、固定 256 段蛇身数组、
随机食物、分数/最佳分数/长度显示、撞墙或撞身体结束。方向键转向，OK 重开，
SET 在 Snake 页面内暂停/继续，RST 使用全局返回。

## 逻辑变化范围

- 新增 `game_snake` 规则层，负责蛇身、食物、得分、碰撞和暂停状态。
- 新增 `page_snake` 页面层，负责 Snake 绘制、输入、定时步进和局部刷新。
- HOME 菜单从 3 项扩展为 4 项，新增 SNAKE 入口。
- 页面枚举和路由注册 Snake 页面。
- 全局 SET 行为增加 Snake 特例：2048 仍进入 PAUSE，Snake 内部切换暂停。
- Keil 工程加入新增 C/H 文件。

## 涉及文件

- `User/game_snake.c`
- `User/game_snake.h`
- `User/page_snake.c`
- `User/page_snake.h`
- `User/ui_types.h`
- `User/ui_page.c`
- `User/page_home.c`
- `Project/led.uvprojx`
- `docs/change-logs/2026-07-31-add-snake-game.md`

## 接口与兼容性

新增 Snake 页面和规则层接口，不改变 2048 的规则层接口。2048 的方向键、OK、
SET、RST 语义保持不变。Snake 的按键语义为方向键转向、OK 重开、SET 暂停或
继续、RST 返回。

## 编码与注释规范

- C/H：GB2312 兼容代码页 936，无 BOM。
- 文档及脚本：UTF-8，无 BOM。
- 函数前详细注释，函数内仅保留关键注释。
- 宏、枚举和结构字段使用对齐的行尾 // 注释。

## 验证结果

- `test-stm32-text-policy.ps1`：通过。
- `test-new-function-comments.ps1`：通过。
- `git diff --check`：通过。
- Keil isolated build：0 error(s), 0 warning(s)。
- 构建尺寸：Code=19516, RO-data=2048, RW-data=56, ZI-data=7064。

## Git

- 分支：master
- 起始提交：fb726a8fc9a3fd97ec79962f6188362a20a00923
- Commit：this commit
- 提交说明：Add Snake game
