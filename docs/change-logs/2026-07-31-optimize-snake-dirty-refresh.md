# Snake 局部刷新优化

日期：2026-07-31

## 修改目标

优化 Snake 自动步进时的刷新范围，把每步整棋盘刷新改为变化格局部刷新，降低
实时游戏对 ST7789 和 dirty 队列的压力。

## 修改前行为

Snake 每次定时步进后都会把整个 16x16 棋盘区域标记为 dirty。虽然不是全屏
刷新，但实时移动时仍会频繁重画完整棋盘区域。

## 修改后行为

Snake 规则层记录最近一次步进中变化的格子：旧头、新头、旧尾以及吃到食物后
的新食物。页面层按行读取 16-bit dirty mask，并把同一行的变化格合并成最小
横向刷新段。暂停、结束 overlay 和重开仍按原逻辑刷新。

## 逻辑变化范围

- `game_snake` 新增 16 行 dirty row mask。
- `Snake_Step()` 开始时清空 dirty mask，并在成功移动后记录变化格。
- `Snake_PlaceFood()` 记录新食物格，供吃食物后的局部刷新使用。
- 新增只读接口 `Snake_GetLastDirtyRowMask()`。
- `page_snake` 新增单格矩形和按行合并刷新逻辑。
- `page_snake` 步进后不再调用整棋盘 dirty，而是只刷新变化格。

## 涉及文件

- `User/game_snake.c`
- `User/game_snake.h`
- `User/page_snake.c`
- `docs/change-logs/2026-07-31-optimize-snake-dirty-refresh.md`

## 接口与兼容性

新增只读接口 `uint16_t Snake_GetLastDirtyRowMask(uint8_t row);`。Snake 的方向键、
OK 重开、SET 暂停/继续、RST 返回语义保持不变。2048 和 HOME 行为不变。

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
- 构建尺寸：Code=19944, RO-data=2048, RW-data=56, ZI-data=7096。

## Git

- 分支：master
- 起始提交：538a3adbe32242b9b988fc05a96e76a70760eba8
- Commit：this commit
- 提交说明：Optimize Snake dirty refresh
