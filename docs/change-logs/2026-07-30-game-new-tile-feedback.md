# 2048 新块反馈

日期：2026-07-30

## 修改目标

为 2048 GAME 页面增加轻量新块反馈：每次有效移动后，新生成的 `2/4` 方块短暂高亮，不引入滑动动画或额外动态内存。

## 修改前行为

有效移动后只按变化格子刷新棋盘，新生成的方块与其它变化格子显示方式相同。玩家只能从棋盘结果中判断哪个格子是新块，没有短暂视觉提示。

## 修改后行为

`game_2048` 记录最近一次有效移动后随机生成的新块位置，`page_game` 读取该 mask 并在对应方块上绘制双层警示色边框。高亮持续约 140 ms，超时后只刷新该方块恢复普通边框。若下一次移动在高亮结束前发生，旧高亮格会合并进本次变化掩码一起刷新，避免额外 dirty 堆积。

## 逻辑变化范围

- `game_2048.c/h` 新增 `Game2048_GetLastNewTileMask()`，只报告最近一次有效移动后生成的新块位置。
- `Game2048_AddRandomTile()` 增加是否记录新块位置的参数；重开初始块不记录，移动后新块记录。
- `page_game.c` 新增新块高亮状态、140 ms 超时恢复、绘制双层高亮边框。
- `page_game.c` 在高亮未结束又发生新移动时，将旧高亮格与本次变化格合并为同一刷新掩码。
- `page_game.c` 在重开或超时恢复后清理高亮状态，保持输入锁直到恢复刷新排队完成。

## 涉及文件

- `User/game_2048.c`
- `User/game_2048.h`
- `User/page_game.c`
- `docs/change-logs/2026-07-30-game-new-tile-feedback.md`

## 接口与兼容性

- 新增只读接口 `Game2048_GetLastNewTileMask()`。
- 未改变 `Game2048_Move()` 返回语义、页面路由、按键映射或 Keil 工程文件。
- 新块反馈只影响 GAME 页面绘制；重开、暂停、返回主页逻辑不变。

## 编码与注释规范

- C/H：GB2312 兼容代码页 936，无 BOM。
- 文档及脚本：UTF-8，无 BOM。
- 函数前详细注释，函数内仅保留关键注释。
- 宏、枚举和结构字段使用对齐的行尾 // 注释。

## 验证结果

- Keil isolated build：`0 error(s), 0 warning(s)`。
  - Project：`D:\stm32f103实例\2048\spi_oled DMAui\Project\led.uvprojx`
  - Output：`D:\stm32f103实例\2048\spi_oled DMAui\Output\codex-verify-20260730-180210-353`
  - Size：`Code=16332 RO-data=2000 RW-data=48 ZI-data=6504`
- `test-stm32-text-policy.ps1`：`STM32 text policy passed for 3 path(s).`
- `test-new-function-comments.ps1`：`New function comment policy passed.`
- `git diff --check`：通过。

## Git

- 分支：master
- 起始提交：2026b3d6696d93a5f72b4de00a12fdcfb0bded0c
- Commit：this commit
- 提交说明：Add 2048 new tile feedback
