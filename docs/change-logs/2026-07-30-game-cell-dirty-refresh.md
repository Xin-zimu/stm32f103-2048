# 2048 格子级局部刷新

日期：2026-07-30

## 修改目标

把 2048 有效移动后的棋盘刷新从整棋盘区域刷新优化为格子级局部刷新，同时让分数栏和胜负提示区域按需独立刷新，降低正常游玩时的 SPI 传输量。

## 修改前行为

`Game2048_Move()` 只返回棋盘是否发生有效变化，不提供哪些格子变化。`page_game` 在每次有效移动后固定刷新顶部分数栏和整个棋盘区域，即使只有少量格子变化、或者分数没有变化，也会提交较大的 dirty 区域。

## 修改后行为

游戏逻辑在每次移动前保存 4x4 棋盘快照，移动、合并和新块生成完成后生成 16 位变化掩码。GAME 页面读取该掩码后，按每一行变化格子的最小横向范围提交 dirty，最多提交 4 个棋盘局部区域。分数或最好分变化时才刷新顶部区域；进入 WIN/GAME OVER 时单独刷新提示覆盖区域。掩码异常为 0 时保留整棋盘刷新兜底。

## 逻辑变化范围

- `game_2048.c/h` 新增 `Game2048_GetLastChangeMask()`，返回最近一次移动或重开产生的 16 位变化格子掩码。
- `Game2048_Move()` 在有效移动前保存棋盘快照，在生成新块和胜负判断后生成变化掩码。
- `Game2048_Restart()` 将变化掩码设为 `0xFFFF`，表示新棋盘全部格子可能变化。
- `page_game.c` 新增单格矩形 helper 和按行合并变化格子的 dirty 提交逻辑。
- `page_game.c` 有效移动后只在分数/最好分变化时刷新顶部区域，只刷新变化格子行跨度，并在胜负状态出现时刷新覆盖提示区域。

## 涉及文件

- `User/game_2048.c`
- `User/game_2048.h`
- `User/page_game.c`
- `docs/change-logs/2026-07-30-game-cell-dirty-refresh.md`

## 接口与兼容性

- 新增只读接口 `Game2048_GetLastChangeMask()`，不改变既有 `Game2048_Move()` 返回语义。
- 未改变页面路由、按键映射、LCD/DMA 驱动或 Keil 工程文件。
- 刷新策略变化仅影响 GAME 页 dirty 区域大小；完整页面重绘、重开、进入/退出页面仍保留原有全屏刷新路径。

## 编码与注释规范

- C/H：GB2312 兼容代码页 936，无 BOM。
- 文档及脚本：UTF-8，无 BOM。
- 函数前详细注释，函数内仅保留关键注释。
- 宏、枚举和结构字段使用对齐的行尾 // 注释。

## 验证结果

- Keil isolated build：`0 error(s), 0 warning(s)`。
  - Project：`D:\stm32f103实例\2048\spi_oled DMAui\Project\led.uvprojx`
  - Output：`D:\stm32f103实例\2048\spi_oled DMAui\Output\codex-verify-20260730-175058-774`
  - Size：`Code=15880 RO-data=2000 RW-data=48 ZI-data=6496`
- `test-stm32-text-policy.ps1`：`STM32 text policy passed for 3 path(s).`
- `test-new-function-comments.ps1`：`New function comment policy passed.`
- `git diff --check`：通过。

## Git

- 分支：master
- 起始提交：d90b2e63c6f012d1d550c0299d340a64ca4366e0
- Commit：this commit
- 提交说明：Add 2048 cell-level dirty refresh
