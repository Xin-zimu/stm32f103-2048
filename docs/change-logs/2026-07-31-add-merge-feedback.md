# 2048 合并反馈

日期：2026-07-31

## 修改目标

为 2048 GAME 页面新增合并目标格短暂高亮反馈。规则层记录最近一次有效移动
中发生合并的目标格 mask，页面层据此绘制区别于新块高亮的反馈效果。

## 修改前行为

移动后只记录棋盘变化 mask 和新生成方块 mask。GAME 页面只对新生成方块做
亮黄底高亮，合并后的目标格没有单独视觉反馈。

## 修改后行为

每次有效移动会记录合并后的目标格 mask，并通过
`Game2048_GetLastMergeMask()` 提供只读访问。GAME 页面对合并目标格显示短暂
青色/绿色强调反馈，优先级高于新块高亮；新块高亮、按键语义、暂停页和局部
刷新策略保持不变。

## 逻辑变化范围

- `game_2048` 新增最近一次合并目标格 mask 状态。
- `Game2048_ApplyLine()` 输出 line 内合并落点。
- `Game2048_WriteLine()` 把 line 内合并落点映射为 4x4 棋盘 bit。
- `Game2048_Move()` 在每次移动开始清零合并 mask，并累加各行/列合并结果。
- `page_game` 新增合并反馈状态、启动函数、绘制优先级和过期恢复逻辑。
- dirty 刷新仍使用按行合并的 `Page_Game_InvalidateChangedCells()`。

## 涉及文件

- `User/game_2048.c`
- `User/game_2048.h`
- `User/page_game.c`
- `docs/change-logs/2026-07-31-add-merge-feedback.md`

## 接口与兼容性

新增只读接口 `uint16_t Game2048_GetLastMergeMask(void);`。现有移动、计分、
最佳分数、棋盘变化 mask、新块 mask、单格读取接口保持兼容。按键语义不变：
方向键移动，OK 重开，SET 暂停，RST 返回。

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
- 起始提交：77b647da202bcdfa2921c3be9e603d5d6f6e98cf
- Commit：this commit
- 提交说明：Add 2048 merge feedback
