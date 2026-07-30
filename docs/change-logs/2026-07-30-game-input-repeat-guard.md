# 2048 输入重复保护

日期：2026-07-30

## 修改目标

修复 2048 GAME 页面在长按 OK/MID 重开、长按无效方向时可能反复提交输入和重绘请求，导致屏幕残留多个方块或表现为卡住的问题。

## 修改前行为

GAME 页面中 OK 事件每次到达都会立即重开并请求整页重绘。方向键只在有效移动后更新节流时间；如果长按一个无法移动或无法合并的方向，无效移动尝试不会被节流，重复事件会持续进入游戏逻辑。有效移动或重开请求重绘后，页面也没有等待当前渲染完成再接受下一次游戏输入。

## 修改后行为

进入 GAME 页面、执行有效移动或 OK 重开后，会锁定游戏输入，直到页面任务确认渲染已经空闲再解锁。OK 重开只接受首次按下事件，并增加 300 ms 重开保护。方向键的 90 ms 节流改为对所有方向尝试生效，包括无效移动，避免长按无法移动方向持续冲击事件队列。

## 逻辑变化范围

- `page_game.c` 引入 `UI_RendererIsBusy()` 判断，渲染忙时不处理 GAME 输入。
- 新增 GAME 页面输入锁，在首屏绘制、有效移动绘制、重开绘制完成前丢弃新的游戏输入。
- OK/MID 重开只接受 `KEY_EVENT_PRESS`，忽略非首次按下事件，并增加 300 ms 重开间隔保护。
- 方向输入节流从“有效移动后更新”改为“方向尝试后更新”，无效移动也受 90 ms 节流。

## 涉及文件

- `User/page_game.c`
- `docs/change-logs/2026-07-30-game-input-repeat-guard.md`

## 接口与兼容性

- 未改变 `game_2048` 规则接口。
- 未改变按键驱动、事件队列或页面路由接口。
- GAME 页面内部行为变化：长按 OK/MID 不再连续重开，长按无效方向不再连续尝试移动。

## 编码与注释规范

- C/H：GB2312 兼容代码页 936，无 BOM。
- 文档及脚本：UTF-8，无 BOM。
- 函数前详细注释，函数内仅保留关键注释。
- 宏、枚举和结构字段使用对齐的行尾 // 注释。

## 验证结果

- Keil isolated build：`0 error(s), 0 warning(s)`。
  - Project：`D:\stm32f103实例\2048\spi_oled DMAui\Project\led.uvprojx`
  - Output：`D:\stm32f103实例\2048\spi_oled DMAui\Output\codex-verify-20260730-172450-556`
  - Size：`Code=15012 RO-data=1980 RW-data=48 ZI-data=6472`
- `test-stm32-text-policy.ps1`：`STM32 text policy passed for 1 path(s).`
- `test-new-function-comments.ps1`：`New function comment policy passed.`
- `git diff --check -- User\page_game.c`：通过。

## Git

- 分支：master
- 起始提交：01eff03f904924392434e55f1aa3f9023b688729
- Commit：this commit
- 提交说明：Guard 2048 repeated game input
