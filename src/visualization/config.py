#!/usr/bin/env python3
"""
音频指纹可视化工具的配置模块
包含全局配置、常量和共享变量
"""

import matplotlib.pyplot as plt

# 设置matplotlib选项以防止窗口冻结和改善窗口管理
plt.rcParams['figure.max_open_warning'] = 10
plt.rcParams['figure.raise_window'] = True
plt.rcParams['figure.autolayout'] = True
plt.rcParams['tk.window_focus'] = True  # 帮助解决Tkinter焦点问题

# 音频播放支持检测
try:
    import soundfile as sf
    import sounddevice as sd
    AUDIO_SUPPORT = True
except ImportError:
    AUDIO_SUPPORT = False
    print("Warning: soundfile or sounddevice not found. Audio playback disabled.")
    print("To enable audio playback, install: pip install soundfile sounddevice")

# PCM文件格式常量 (基于convert_to_pcm.sh)
PCM_SAMPLE_RATE = 44100
PCM_CHANNELS = 1
PCM_FORMAT = 'int16'  # 对应pcm_s16le
PCM_ENDIAN = 'little'  # 小端序
PCM_SAMPLE_WIDTH = 2  # 16位 = 2字节

# 全局tkinter实例管理，避免多次创建和销毁
_tk_root = None
# 全局音频播放器引用，用于在窗口关闭时停止播放
_current_audio_player = None

# 全局刷新率配置
REFRESH_RATE_30FPS = 33  # 30fps = 33ms间隔
REFRESH_RATE_60FPS = 16  # 60fps = 16ms间隔
_ui_refresh_interval = REFRESH_RATE_30FPS  # 默认30fps
_playback_update_interval = 0.033  # 默认33ms更新间隔

def get_screen_size():
    """获取屏幕尺寸，优先使用tkinter方法"""
    global _tk_root
    
    try:
        # 如果已有tkinter实例，直接使用
        if _tk_root is None:
            import tkinter as tk
            _tk_root = tk.Tk()
            _tk_root.withdraw()  # 隐藏主窗口
        
        # 获取屏幕尺寸
        screen_width = _tk_root.winfo_screenwidth()
        screen_height = _tk_root.winfo_screenheight()
        
        return screen_width, screen_height
        
    except Exception as e:
        print(f"tkinter获取屏幕尺寸失败: {e}")
        # 备用方法：使用matplotlib
        try:
            figure = plt.figure()
            mngr = figure.canvas.manager
            if hasattr(mngr, 'window'):
                if hasattr(mngr.window, 'wm_maxsize'):
                    screen_width, screen_height = mngr.window.wm_maxsize()
                    plt.close(figure)
                    return screen_width, screen_height
            plt.close(figure)
        except:
            pass
        
        # 默认值
        print("使用默认屏幕尺寸: 1920x1080")
        return 1920, 1080

def clean_up():
    """清理全局资源"""
    global _tk_root, _current_audio_player
    
    # 停止音频播放
    if _current_audio_player:
        try:
            _current_audio_player.stop()
        except:
            pass
        _current_audio_player = None
    
    # 销毁tkinter实例
    if _tk_root:
        try:
            _tk_root.destroy()
        except:
            pass
        _tk_root = None
    
    # 关闭所有matplotlib图形
    try:
        plt.close('all')
    except:
        pass 