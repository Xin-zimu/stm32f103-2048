# 增强 2048 新块反馈

日期：2026-07-30

## 修改目标

根据上板反馈“好像没看到高亮”，增强 2048 新块反馈的肉眼可见性。

## 修改前行为

新块反馈只绘制约 140 ms 的警示色细边框。由于新生成方块通常是浅色 `2/4`，在 240x240 小屏上边框不够明显，容易看不出反馈。

## 修改后行为

新生成方块高亮持续约 220 ms。高亮期间方块使用警示色亮底、黑色文字、青色外边框和黑色内边框，恢复时仍只刷新该方块。

## 逻辑变化范围

- `PAGE_GAME_NEW_FLASH_MS` 从 140 ms 调整为 220 ms。
- 新块高亮绘制从单纯边框改为亮底、黑字、青色外框和黑色内框。
- 未改变新块 mask、刷新 mask、移动规则或按键逻辑。

## 涉及文件

- `User/page_game.c`
- `docs/change-logs/2026-07-30-make-new-tile-feedback-visible.md`

## 接口与兼容性

- 未改变任何头文件接口。
- 未改变 `game_2048` 规则层。
- 仅增强 GAME 页面新块反馈的显示样式和持续时间。

## 编码与注释规范

- C/H：GB2312 兼容代码页 936，无 BOM。
- 文档及脚本：UTF-8，无 BOM。
- 函数前详细注释，函数内仅保留关键注释。
- 宏、枚举和结构字段使用对齐的行尾 // 注释。

## 验证结果

- Keil isolated build：`0 error(s), 0 warning(s)`。
  - Project：`D:\stm32f103实例\2048\spi_oled DMAui\Project\led.uvprojx`
  - Output：`D:\stm32f103实例\2048\spi_oled DMAui\Output\codex-verify-20260730-180535-674`
  - Size：`Code=16356 RO-data=2000 RW-data=48 ZI-data=6504`
- `test-stm32-text-policy.ps1`：`STM32 text policy passed for 1 path(s).`
- `test-new-function-comments.ps1`：`New function comment policy passed.`
- `git diff --check -- User\page_game.c`：通过。

## Git

- 分支：master
- 起始提交：1b6de52929556983397e057023baba4a41a91ef4
- Commit：this commit
- 提交说明：Make 2048 new tile feedback visible
