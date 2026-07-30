# HOME 游戏说明

日期：2026-07-31

## 修改目标

给 HOME 页面增加当前选中游戏的小状态说明，让玩家进入前能区分 2048 和
Snake 的玩法类型。

## 修改前行为

HOME 页面底部始终显示相同的导航提示。选中 2048 或 SNAKE 时，没有额外说明
它们分别是目标解谜类和实时动作类游戏。

## 修改后行为

HOME 页面底部根据当前选中项动态显示说明：选中 2048 时显示 `GOAL PUZZLE`，
选中 SNAKE 时显示 `REALTIME`，选中设置或信息时保留原有导航提示。

## 逻辑变化范围

- `page_home` 新增两个短 ASCII footer 文案。
- `page_home` 新增当前选中项到 footer 文案的只读选择函数。
- HOME 菜单项、页面路由、输入语义和其他页面行为不变。

## 涉及文件

- `User/page_home.c`
- `docs/change-logs/2026-07-31-home-game-descriptions.md`

## 接口与兼容性

无新增或删除对外接口。HOME 方向键选择、OK/RIGHT 进入、LEFT 回到第一项的
行为保持不变。

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
- 起始提交：08c6eac279b10483f578d8078a9ec502e3dbb65f
- Commit：this commit
- 提交说明：Add HOME game descriptions
