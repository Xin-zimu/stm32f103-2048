#!/usr/bin/env python3
"""Select one GIF, convert it, rebuild the Keil project, and flash the board."""

from __future__ import annotations

import importlib.util
import os
import shutil
import subprocess
import sys
from pathlib import Path
from tkinter import Tk, filedialog, messagebox

MAX_FRAMES = 3
INTERVAL_MS = 100
TARGET_NAME = "led"

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
CONVERTER = SCRIPT_DIR / "gif_to_oled_frames.py"
PROJECT_FILE = PROJECT_ROOT / "Project" / "led.uvprojx"
OUTPUT_C = PROJECT_ROOT / "User" / "anim_frames.c"
OUTPUT_DIR = PROJECT_ROOT / "Output"
BUILD_LOG = OUTPUT_DIR / "one_click_build.log"
FLASH_LOG = OUTPUT_DIR / "one_click_flash.log"


def hidden_process_flags() -> int:
    """Return the Windows flag used to hide command-line child windows."""
    return getattr(subprocess, "CREATE_NO_WINDOW", 0)


def show_error(title: str, detail: str) -> None:
    """Show a concise error dialog and terminate the one-click workflow."""
    messagebox.showerror(title, detail)
    raise RuntimeError(detail)


def ensure_pillow() -> None:
    """Ensure Pillow is available, offering automatic installation if needed."""
    if importlib.util.find_spec("PIL") is not None:
        return

    install = messagebox.askyesno(
        "缺少图片组件",
        "首次使用需要安装 Pillow。\n\n是否现在自动安装？",
    )
    if not install:
        show_error("无法继续", "未安装 Pillow，GIF 无法转换。")

    result = subprocess.run(
        [sys.executable, "-m", "pip", "install", "pillow"],
        cwd=PROJECT_ROOT,
        capture_output=True,
        text=True,
        errors="replace",
        creationflags=hidden_process_flags(),
    )
    if result.returncode != 0:
        show_error(
            "Pillow 安装失败",
            "请联网后重试，或手工执行：\npy -m pip install pillow\n\n"
            + result.stderr[-800:],
        )


def locate_keil() -> Path:
    """Locate uVision using the known installation path or the system PATH."""
    candidates = [
        Path(r"D:\keil5\core\UV4\UV4.exe"),
        Path(r"C:\Keil_v5\UV4\UV4.exe"),
        Path(r"C:\Keil\UV4\UV4.exe"),
    ]
    path_entry = shutil.which("UV4.exe")
    if path_entry:
        candidates.append(Path(path_entry))

    for candidate in candidates:
        if candidate.is_file():
            return candidate

    show_error(
        "找不到 Keil",
        "没有找到 UV4.exe。请确认 Keil5 已安装。\n"
        "默认检查位置：D:\\keil5\\core\\UV4\\UV4.exe",
    )
    raise AssertionError("unreachable")


def run_logged(command: list[str], log_path: Path) -> tuple[int, str]:
    """Run one hidden command and return its exit code and generated log text."""
    log_path.parent.mkdir(parents=True, exist_ok=True)
    if log_path.exists():
        log_path.unlink()

    result = subprocess.run(
        command,
        cwd=PROJECT_ROOT,
        creationflags=hidden_process_flags(),
    )
    if log_path.exists():
        log_text = log_path.read_text(encoding="gbk", errors="replace")
    else:
        log_text = ""
    return result.returncode, log_text


def convert_gif(gif_path: Path) -> int:
    """Convert the selected GIF to the fixed firmware animation array files."""
    result = subprocess.run(
        [
            sys.executable,
            "-B",
            str(CONVERTER),
            str(gif_path),
            str(OUTPUT_C),
            "--max-frames",
            str(MAX_FRAMES),
            "--interval",
            str(INTERVAL_MS),
        ],
        cwd=PROJECT_ROOT,
        capture_output=True,
        text=True,
        errors="replace",
        creationflags=hidden_process_flags(),
    )
    if result.returncode != 0:
        show_error("GIF 转换失败", result.stderr[-1000:] or result.stdout[-1000:])

    header_text = OUTPUT_C.with_suffix(".h").read_text(
        encoding="gbk", errors="strict"
    )
    marker = "#define ANIM_FRAME_COUNT"
    for line in header_text.splitlines():
        if line.startswith(marker):
            return int(line.split()[2].rstrip("U"))

    show_error("GIF 转换失败", "生成文件中没有找到动画帧数。")
    raise AssertionError("unreachable")


def rebuild_project(uv4: Path) -> None:
    """Rebuild the configured Keil target and reject any compiler errors."""
    exit_code, log_text = run_logged(
        [
            str(uv4),
            "-r",
            str(PROJECT_FILE),
            "-t",
            TARGET_NAME,
            "-j0",
            "-o",
            str(BUILD_LOG),
        ],
        BUILD_LOG,
    )
    if exit_code != 0 or "0 Error(s)" not in log_text:
        show_error(
            "Keil 编译失败",
            "没有进行烧录。请查看：\n"
            f"{BUILD_LOG}\n\n"
            + log_text[-1200:],
        )


def flash_project(uv4: Path) -> None:
    """Download the rebuilt image using the debugger configured in Keil."""
    exit_code, log_text = run_logged(
        [
            str(uv4),
            "-f",
            str(PROJECT_FILE),
            "-t",
            TARGET_NAME,
            "-j0",
            "-o",
            str(FLASH_LOG),
        ],
        FLASH_LOG,
    )
    lowered = log_text.lower()
    failed = (
        exit_code != 0
        or not log_text.strip()
        or "error:" in lowered
        or "failed" in lowered
        or "no st-link detected" in lowered
    )
    if failed:
        show_error(
            "烧录失败",
            "GIF 已转换且编译成功，但未能下载到开发板。\n\n"
            "请检查 ST-Link、SWD 接线、开发板供电，以及 Keil 的 Debug/Utilities "
            "下载器配置。首次使用时需要在 Keil 中完成一次 ST-Link 配置。\n\n日志：\n"
            f"{FLASH_LOG}\n\n"
            + log_text[-1000:],
        )


def main() -> int:
    """Run the complete select, convert, compile, and flash workflow."""
    root = Tk()
    root.withdraw()
    root.update()

    gif_name = filedialog.askopenfilename(
        title="选择要显示的 GIF",
        initialdir=str(PROJECT_ROOT),
        filetypes=[("GIF 动画", "*.gif"), ("所有文件", "*.*")],
    )
    if not gif_name:
        return 0

    try:
        ensure_pillow()
        uv4 = locate_keil()

        messagebox.showinfo(
            "开始处理",
            "将自动完成：GIF 转换 → Keil 编译 → ST-Link 烧录。\n\n"
            f"最多使用前 {MAX_FRAMES} 帧，转换为 16 色并全屏播放，"
            f"间隔 {INTERVAL_MS} ms。",
        )
        frame_count = convert_gif(Path(gif_name))
        rebuild_project(uv4)
        flash_project(uv4)
    except RuntimeError:
        return 1

    messagebox.showinfo(
        "完成",
        f"已转换 {frame_count} 帧并成功烧录。\n\n"
        "开发板复位后会自动循环播放所选 GIF。",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
