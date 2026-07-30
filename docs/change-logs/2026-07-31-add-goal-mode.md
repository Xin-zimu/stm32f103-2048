# 2048 Goal 模式

日期：2026-07-31

## 修改目标

新增 2048 Goal 模式，让玩家可以在 PAUSE 页面切换胜利目标。默认仍保持
Classic 2048，低目标用于更快体验胜利流程。

## 修改前行为

胜利条件固定为 2048。PAUSE 页面只有 RESUME、NEW GAME、HOME 三项，无法在
游戏内选择更短目标。

## 修改后行为

规则层保存当前目标方块指数，支持 128、256、512、1024、2048 五档目标。
GAME 顶部标题显示当前目标，例如 `G512`。PAUSE 页面新增 `GOAL` 行，按 OK
或右键循环目标值，页面保持暂停；返回 GAME 后按当前目标判断 WIN。

## 逻辑变化范围

- `game_2048` 新增目标指数状态，默认 2048。
- 胜利判断从固定 2048 改为当前目标。
- 新增目标设置、目标指数读取、目标数值读取接口。
- 调整目标时立即复查当前棋盘状态，支持降低目标后立即 WIN，也支持提高目标后继续玩。
- `page_game` 顶部状态栏显示当前目标。
- `page_pause` 新增 GOAL 菜单项，OK/RIGHT 循环目标，不直接重开棋盘。

## 涉及文件

- `User/game_2048.c`
- `User/game_2048.h`
- `User/page_game.c`
- `User/page_pause.c`
- `docs/change-logs/2026-07-31-add-goal-mode.md`

## 接口与兼容性

新增接口：

- `void Game2048_SetGoalExp(uint8_t goal_exp);`
- `uint8_t Game2048_GetGoalExp(void);`
- `uint32_t Game2048_GetGoalValue(void);`

现有移动、重开、计分、新块反馈、合并反馈和按键语义保持兼容。方向键仍移动，
OK 在 GAME 中仍重开，SET 仍进入 PAUSE，RST 仍返回。

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
- 起始提交：ac84da267517e29a2de8a7faad95b891b9a0349e
- Commit：this commit
- 提交说明：Add 2048 goal mode
