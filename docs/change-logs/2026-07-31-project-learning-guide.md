# 项目学习文档

日期：2026-07-31

## 修改目标

新增一份面向学习和后续扩展的项目级 Markdown 文档，系统说明 STM32F103 + ST7789 多游戏 UI 项目的设计思想、模块边界、关键实现方式和上板测试思路。

## 修改前行为

项目已有多次功能迭代记录，但缺少一份从整体架构到具体模块的学习文档。后续学习者需要分别阅读 `main.c`、UI 框架、2048、Snake、dirty 刷新和 LCD DMA 模块才能拼出完整设计脉络。

## 修改后行为

新增 `docs/project-learning-guide.md`，按“资源约束 -> 主循环 -> 输入 -> 页面 -> dirty -> strip renderer -> ST7789/DMA -> 2048 -> Snake -> 扩展新游戏 -> 测试流程”的顺序讲解项目，并在关键位置附带短代码片段。

## 逻辑变化范围

代码逻辑与原提交完全一致。本次仅新增学习文档并更新本变更日志。

## 涉及文件

- `docs/project-learning-guide.md`
- `docs/change-logs/2026-07-31-project-learning-guide.md`

## 接口与兼容性

无 C/H 接口变化，无固件行为变化，无按键语义变化，无资源占用变化。

## 编码与注释规范

- C/H：GB2312 兼容代码页 936，无 BOM。
- 文档及脚本：UTF-8，无 BOM。
- 函数前详细注释，函数内仅保留关键注释。
- 宏、枚举和结构字段使用对齐的行尾 // 注释。

## 验证结果

- `test-stm32-text-policy.ps1 -Paths @('docs\project-learning-guide.md','docs\change-logs\2026-07-31-project-learning-guide.md')`：通过，`STM32 text policy passed for 2 path(s).`
- `test-new-function-comments.ps1 -Paths @('docs\project-learning-guide.md','docs\change-logs\2026-07-31-project-learning-guide.md')`：通过，`New function comment policy passed.`
- `git diff --check -- docs\project-learning-guide.md docs\change-logs\2026-07-31-project-learning-guide.md`：通过，无输出。
- Keil isolated build：通过，`0 error(s), 0 warning(s)`；大小 `Code=19944 RO-data=2048 RW-data=56 ZI-data=7096`。

## Git

- 分支：master
- 起始提交：0c37f232033dba86bf728b9d367adbe15b6dd38e
- Commit：this commit
- 提交说明：Add project learning guide
