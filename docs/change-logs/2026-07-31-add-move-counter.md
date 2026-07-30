# 2048 步数统计

日期：2026-07-31

## 修改目标

新增 2048 当前局有效移动步数统计，用于在不同 Goal 模式下比较完成目标所需
步数。

## 修改前行为

规则层只记录分数、最佳分数、棋盘变化和反馈 mask。GAME 顶部显示目标、分数
和最佳分数，没有当前局步数。

## 修改后行为

规则层记录当前局有效移动次数，重开后清零；只有实际改变棋盘的方向移动才会
加一，无效方向输入、暂停菜单操作和重开不会增加步数。GAME 顶部状态栏显示
`M` 步数，同时保留 `G` 目标、`S` 分数和 `B` 最佳分数。

## 逻辑变化范围

- `game_2048` 新增 `g_game2048_move_count`。
- `Game2048_ClearBoard()` 清零当前局步数。
- `Game2048_Move()` 仅在有效移动后递增步数。
- 新增只读接口 `Game2048_GetMoveCount()`。
- `page_game` 顶部状态栏增加 Move 显示，并在步数变化时刷新顶部状态区。

## 涉及文件

- `User/game_2048.c`
- `User/game_2048.h`
- `User/page_game.c`
- `docs/change-logs/2026-07-31-add-move-counter.md`

## 接口与兼容性

新增只读接口 `uint32_t Game2048_GetMoveCount(void);`。现有移动、重开、计分、
Goal、新块反馈、合并反馈、PAUSE 和按键语义保持兼容。

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

## Git

- 分支：master
- 起始提交：0239cdb5d46b53d0d5657b835dccc8e8872a2c14
- Commit：this commit
- 提交说明：Add 2048 move counter
