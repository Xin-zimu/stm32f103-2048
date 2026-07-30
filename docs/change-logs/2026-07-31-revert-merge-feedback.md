# 撤回 2048 合并反馈

日期：2026-07-31

## 修改目标

撤回上一版加入的 2048 合并目标格高亮反馈，恢复到仅保留新块高亮的
GAME 页面体验。撤回采用新的 Git 提交记录，不改写历史提交。

## 修改前行为

规则层在每次有效移动后记录合并目标格 mask，并通过
`Game2048_GetLastMergeMask()` 提供给页面层。GAME 页面会额外维护合并
反馈状态，对合并后的目标格绘制青色/绿色强调效果。

## 修改后行为

规则层不再记录或暴露合并目标格 mask。GAME 页面不再绘制合并反馈，只保留
已有的新块短暂高亮、格子级局部刷新、PAUSE 页面和输入防连发逻辑。

## 逻辑变化范围

- `game_2048` 移除合并 mask 状态、写回映射和只读接口。
- `page_game` 移除合并反馈状态、绘制优先级和过期恢复逻辑。
- 移动、计分、随机新块、胜负判定、新块高亮和按键语义保持不变。

## 涉及文件

- `User/game_2048.c`
- `User/game_2048.h`
- `User/page_game.c`
- `docs/change-logs/2026-07-31-revert-merge-feedback.md`

## 接口与兼容性

移除 `Game2048_GetLastMergeMask()`。其他对外接口保持不变，包括移动、分数、
最佳分数、棋盘变化 mask、新块 mask 和单格读取接口。

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
- 起始提交：712f6b8f0d884a72a0aec5ff52d7e91f916476cd
- Commit：this commit
- 提交说明：Revert 2048 merge feedback
