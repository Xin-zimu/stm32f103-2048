# 2048 Goal 提示优化

日期：2026-07-31

## 修改目标

优化 Goal 模式后的 GAME 页面提示，让玩家在达到目标或游戏结束时更容易理解
当前状态和可用操作。

## 修改前行为

WIN 覆盖层只显示 `WIN`，不会说明是哪一档目标达成。GAME 页脚始终显示同一
条提示，结束状态下仍显示移动提示，容易让玩家误以为方向键还能继续移动。

## 修改后行为

WIN 覆盖层显示当前目标，例如 `WIN 512`。GAME 页脚按状态切换：游玩中提示
方向移动、OK 新局、SET 暂停；WIN/GAME OVER 时提示 OK 新局、SET 菜单、RST
返回。

## 逻辑变化范围

- `page_game` 根据游戏状态选择页脚文案。
- `page_game` 在 WIN 覆盖层中追加当前 Goal 数值。
- 不改变规则层、按键语义、局部刷新策略或 PAUSE 菜单行为。

## 涉及文件

- `User/page_game.c`
- `docs/change-logs/2026-07-31-improve-goal-prompts.md`

## 接口与兼容性

无新增或删除接口。方向键、OK、SET、RST 的行为保持不变。

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
- 起始提交：bb67ddb2189bbc457a2574f27a214628b163e515
- Commit：this commit
- 提交说明：Improve 2048 goal prompts
