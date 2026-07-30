# 2048 合并反馈

日期：2026-07-31

## 修改目标

为 2048 GAME 页面新增合并目标格短暂高亮反馈。规则层记录最近一次有效移动中的合并目标格 mask，页面层用该 mask 绘制区别于新块反馈的合并反馈，不引入滑动动画。

## 修改前行为

有效移动后已有变化格子局部刷新和新块高亮反馈，但规则层不记录合并目标格。玩家可以看到分数变化和方块结果，但 `2+2`、`4+4` 等合并后的目标格没有单独反馈。

## 修改后行为

每次有效移动时，`game_2048` 在 line 合并阶段记录合并后的目标位置，并在写回棋盘时映射为 4x4 棋盘 mask。`page_game` 根据 merge mask 对合并目标格显示约 180 ms 的青色高亮，优先级高于新块黄色高亮。合并反馈和新块反馈超时恢复时合并 dirty mask，继续按行跨度刷新。

## 逻辑变化范围

- `game_2048.c/h` 新增只读接口 `Game2048_GetLastMergeMask()`。
- `Game2048_ApplyLine()` 新增 line 内 merge target mask 输出，bit 0..3 对应合并后的 line 位置。
- `Game2048_WriteLine()` 在写回棋盘时把 line merge mask 映射成 board mask，页面层不需要理解移动方向。
- `Game2048_Move()` 每次移动前清零 merge mask，成功移动后累加本次合并目标格。
- `Game2048_Restart()` 清零 merge mask。
- `page_game.c` 新增 merge flash 状态、180 ms 恢复、绘制优先级和恢复 dirty 合并。

## 涉及文件

- `User/game_2048.c`
- `User/game_2048.h`
- `User/page_game.c`
- `docs/change-logs/2026-07-31-game-merge-feedback.md`

## 接口与兼容性

- 新增 `uint16_t Game2048_GetLastMergeMask(void);`。
- 未改变 `Game2048_Move()`、`Game2048_GetLastChangeMask()`、`Game2048_GetLastNewTileMask()` 的既有语义。
- 未改变按键语义、页面路由、LCD/DMA 驱动或 Keil 工程文件。
- 仍使用固定静态内存，无动态分配、无全屏 framebuffer。

## 编码与注释规范

- C/H：GB2312 兼容代码页 936，无 BOM。
- 文档及脚本：UTF-8，无 BOM。
- 函数前详细注释，函数内仅保留关键注释。
- 宏、枚举和结构字段使用对齐的行尾 // 注释。

## 验证结果

- `test-stm32-text-policy.ps1`：`STM32 text policy passed for 3 path(s).`
- `test-new-function-comments.ps1`：`New function comment policy passed.`
- `git diff --check`：通过。
- Keil isolated build：`0 error(s), 0 warning(s)`。
  - Project：`D:\stm32f103实例\2048\spi_oled DMAui\Project\led.uvprojx`
  - Output：`D:\stm32f103实例\2048\spi_oled DMAui\Output\codex-verify-20260731-010956-630`
  - Size：`Code=16676 RO-data=2000 RW-data=48 ZI-data=6520`

## Git

- 分支：master
- 起始提交：853f46a4907dccfd0fde3a113035ca4bc7ba30b0
- Commit：this commit
- 提交说明：Add 2048 merge feedback
