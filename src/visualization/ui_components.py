#!/usr/bin/env python3
"""
UI组件模块
包含音频控制面板和文本布局相关函数
"""

import os
import matplotlib.pyplot as plt
from matplotlib.widgets import Button, Slider
from visualization.plot_utils import clean_filename_for_display


def create_audio_controls_layout(fig, controls_ax, source_audio_player=None, query_audio_player=None, unified_max_time=None):
    """
    创建现代化的音频控制面板布局
    使用清晰的垂直层次结构和响应式设计
    
    Args:
        unified_max_time: 统一的横轴最大时间，用于设置滑块范围
    """
    # 获取控制面板的位置和大小
    pos = controls_ax.get_position()
    left, bottom, width, height = pos.x0, pos.y0, pos.width, pos.height
    
    # 优化的布局参数 - 充分利用垂直空间
    button_row_height = 0.28        # 按钮行高度占比 (减少)
    slider_row_height = 0.22        # 滑块行高度占比 (增加)
    
    vertical_padding = 0.02         # 减少垂直间距，充分利用空间
    horizontal_padding = 0.02       # 水平间距
    
    # 优化的垂直位置分配 - 减少空白区域
    button_y = bottom + height * 0.52       # 顶层：按钮 (72%处，向上移动)
    slider_y = bottom + height * 0.32       # 中层：滑块 (42%处，向上移动)
    # 底层：文本信息（0-40%区域，增加文本区域高度）
    
    controls = {}  # 存储控件引用
    
    # 如果只有一个音频播放器，居中布局
    if (source_audio_player is not None) != (query_audio_player is not None):
        # 单个音频播放器的现代化布局
        audio_player = source_audio_player or query_audio_player
        label_prefix = "Source" if source_audio_player else "Query"
        
        # 计算居中位置和组件尺寸
        button_width = width * 0.12     # 增加按钮宽度以容纳文字
        button_gap = width * 0.02       # 按钮间距
        slider_width = width * 0.4      # 滑块宽度
        center_x = left + width * 0.5
        
        # Play button (左侧)
        play_x = center_x - button_width - button_gap/2
        play_ax = plt.axes([play_x, button_y, button_width, height * button_row_height])
        play_button = Button(play_ax, f'▶ Play', 
                           color='#4CAF50' if source_audio_player else '#2196F3',  # 绿色/蓝色
                           hovercolor='#45a049' if source_audio_player else '#1976D2')
        
        # Stop button (右侧)
        stop_x = center_x + button_gap/2
        stop_ax = plt.axes([stop_x, button_y, button_width, height * button_row_height])
        stop_button = Button(stop_ax, f'⏹ Stop', 
                           color='#f44336', hovercolor='#d32f2f')  # 红色
        
        # Time slider (居中，跨越整个宽度)
        slider_x = center_x - slider_width / 2
        slider_ax = plt.axes([slider_x, slider_y, slider_width, height * slider_row_height])
        slider_max_time = unified_max_time if unified_max_time is not None else audio_player.duration
        time_slider = Slider(slider_ax, f'{label_prefix} Time', 0, slider_max_time, valinit=0,
                           facecolor='#2196F3' if source_audio_player else '#4CAF50')
        
        controls[label_prefix.lower()] = {
            'play_button': play_button,
            'stop_button': stop_button,
            'time_slider': time_slider
        }
        
    else:
        # 双音频播放器的现代化对称布局
        panel_width = width * 0.45      # 每个面板占总宽度的45%
        center_gap = width * 0.10       # 中间间隙占10%
        
        button_width = panel_width * 0.25   # 增加按钮宽度
        button_gap = panel_width * 0.04     # 按钮间距
        slider_width = panel_width * 0.4    # 滑块宽度
        
        if source_audio_player is not None:
            # Source controls (左侧面板) - 现代化布局
            source_panel_left = left + horizontal_padding
            source_center_x = source_panel_left + panel_width / 2
            
            # Source play button
            source_play_x = source_center_x - button_width - button_gap/2
            source_play_ax = plt.axes([source_play_x, button_y, button_width, height * button_row_height])
            source_play_button = Button(source_play_ax, '▶ Play', color='#2196F3', hovercolor='#1976D2')
            
            # Source stop button
            source_stop_x = source_center_x + button_gap/2
            source_stop_ax = plt.axes([source_stop_x, button_y, button_width, height * button_row_height])
            source_stop_button = Button(source_stop_ax, '⏹ Stop', color='#f44336', hovercolor='#d32f2f')
            
            # Source time slider
            source_slider_x = source_panel_left + (panel_width - slider_width) / 2
            source_slider_ax = plt.axes([source_slider_x, slider_y, slider_width, height * slider_row_height])
            source_slider_max_time = unified_max_time if unified_max_time is not None else source_audio_player.duration
            source_time_slider = Slider(source_slider_ax, 'Source Time', 0, source_slider_max_time, valinit=0,
                                      facecolor='#2196F3')
            
            controls['source'] = {
                'play_button': source_play_button,
                'stop_button': source_stop_button,
                'time_slider': source_time_slider
            }
        
        if query_audio_player is not None:
            # Query controls (右侧面板) - 现代化布局
            query_panel_left = left + width - panel_width - horizontal_padding
            query_center_x = query_panel_left + panel_width / 2
            
            # Query play button
            query_play_x = query_center_x - button_width - button_gap/2
            query_play_ax = plt.axes([query_play_x, button_y, button_width, height * button_row_height])
            query_play_button = Button(query_play_ax, '▶ Play', color='#4CAF50', hovercolor='#45a049')
            
            # Query stop button
            query_stop_x = query_center_x + button_gap/2
            query_stop_ax = plt.axes([query_stop_x, button_y, button_width, height * button_row_height])
            query_stop_button = Button(query_stop_ax, '⏹ Stop', color='#f44336', hovercolor='#d32f2f')
            
            # Query time slider
            query_slider_x = query_panel_left + (panel_width - slider_width) / 2
            query_slider_ax = plt.axes([query_slider_x, slider_y, slider_width, height * slider_row_height])
            query_slider_max_time = unified_max_time if unified_max_time is not None else query_audio_player.duration
            query_time_slider = Slider(query_slider_ax, 'Query Time', 0, query_slider_max_time, valinit=0,
                                     facecolor='#4CAF50')
            
            controls['query'] = {
                'play_button': query_play_button,
                'stop_button': query_stop_button,
                'time_slider': query_time_slider
            }
    
    return controls


def create_audio_text_layout(controls_ax, source_audio_player=None, query_audio_player=None):
    """
    创建层次清晰的音频文本布局
    建立明确的信息层次结构，充分利用垂直空间
    
    双音频播放器布局结构 (0-40%垂直空间):
    ┌─────────────────────────────────────────────────────┐
    │ Source区域 (0-50%)          │ Query区域 (50-100%)    │
    │ ┌─────────────────────────┐ │ ┌─────────────────────┐ │
    │ │第一行(28%): 时间 + 状态 │ │ │第一行(28%): 时间+状态│ │
    │ │ Source:00:00 │ Ready   │ │ │ Query:00:00 │ Ready │ │
    │ └─────────────────────────┘ │ └─────────────────────┘ │
    │ ┌─────────────────────────┐ │ ┌─────────────────────┐ │
    │ │第二行(12%): 文件+时长   │ │ │第二行(12%): 文件+时长│ │
    │ │ ♪file.wav    │ ⏱02:30 │ │ │ ♪query.wav │⏱01:45 │ │
    │ └─────────────────────────┘ │ └─────────────────────┘ │
    └─────────────────────────────────────────────────────┘
    """
    texts = {}
    
    # 如果只有一个音频播放器，居中布局
    if (source_audio_player is not None) != (query_audio_player is not None):
        audio_player = source_audio_player or query_audio_player
        label_prefix = "Source" if source_audio_player else "Query"
        
        # 层次化的文本布局 - 充分利用0-40%的垂直空间
        
        # 第一层：主要时间显示 (最突出)
        time_text = controls_ax.text(0.5, 0.32, f"{label_prefix}: 00:00", 
                                   fontsize=16, ha='center', va='center', weight='bold',
                                   color='#1976D2' if source_audio_player else '#388E3C')
        
        # 第二层：播放状态 (中等重要性)
        status_text = controls_ax.text(0.5, 0.22, "Ready", 
                                     fontsize=11, ha='center', va='center', weight='bold',
                                     color='#FF9800')  # 橙色，更醒目的状态指示
        
        # 第三层：文件信息标题 (次要信息)
        duration_mins = int(audio_player.duration) // 60
        duration_secs = int(audio_player.duration) % 60
        filename = os.path.basename(audio_player.audio_file)
        filename = clean_filename_for_display(filename)
        
        if len(filename) > 45:
            filename = filename[:42] + "..."
            
        # 文件名 - 分层显示
        file_name_text = controls_ax.text(0.5, 0.12, f"♪ {filename}", 
                                        fontsize=9, ha='center', va='center', weight='bold',
                                        color='#424242')
        
        # 时长信息 - 最底层
        duration_text = controls_ax.text(0.5, 0.04, f"Duration: {duration_mins:02}:{duration_secs:02}", 
                                       fontsize=8, ha='center', va='center',
                                       color='#757575')
        
        audio_player.time_display = time_text
        audio_player.status_text = status_text
        
        texts[label_prefix.lower()] = {
            'time_text': time_text,
            'status_text': status_text,
            'file_text': file_name_text,
            'duration_text': duration_text
        }
        
    else:
        # 双音频播放器的优化水平布局
        # 充分利用0-40%的垂直空间，左右分区，每区两行水平排列
        
        if source_audio_player is not None:
            # Source文本区域 (左侧) - 两行水平布局
            
            # === 第一行：时间显示 + 播放状态 (水平排列) ===
            # 时间显示 (左侧)
            source_time_text = controls_ax.text(0.12, 0.28, "Source: 00:00", 
                                              fontsize=12, ha='center', va='center', weight='bold',
                                              color='#1976D2')
            
            # 播放状态 (右侧)
            source_status_text = controls_ax.text(0.38, 0.28, "Ready", 
                                                fontsize=9, ha='center', va='center', weight='bold',
                                                color='#FF9800')
            
            # === 第二行：文件信息 + 时长 (水平排列) ===
            source_duration_mins = int(source_audio_player.duration) // 60
            source_duration_secs = int(source_audio_player.duration) % 60
            source_filename = os.path.basename(source_audio_player.audio_file)
            source_filename = clean_filename_for_display(source_filename)
            
            # 调整文件名长度适应水平布局
            if len(source_filename) > 18:
                source_filename = source_filename[:15] + "..."
            
            # 文件名 (左侧)
            source_file_text = controls_ax.text(0.02, 0.12, f"♪ {source_filename}", 
                                              fontsize=8, ha='left', va='center', weight='bold',
                                              color='#424242')
            
            # 时长 (右侧)
            source_duration_text = controls_ax.text(0.48, 0.12, f"⏱ {source_duration_mins:02}:{source_duration_secs:02}", 
                                                  fontsize=8, ha='right', va='center',
                                                  color='#757575')
            
            source_audio_player.time_display = source_time_text
            source_audio_player.status_text = source_status_text
            
            texts['source'] = {
                'time_text': source_time_text,
                'status_text': source_status_text,
                'file_text': source_file_text,
                'duration_text': source_duration_text
            }
        
        if query_audio_player is not None:
            # Query文本区域 (右侧) - 两行水平布局
            
            # === 第一行：时间显示 + 播放状态 (水平排列) ===
            # 时间显示 (左侧)
            query_time_text = controls_ax.text(0.62, 0.28, "Query: 00:00", 
                                             fontsize=12, ha='center', va='center', weight='bold',
                                             color='#388E3C')
            
            # 播放状态 (右侧)
            query_status_text = controls_ax.text(0.88, 0.28, "Ready", 
                                               fontsize=9, ha='center', va='center', weight='bold',
                                               color='#FF9800')
            
            # === 第二行：文件信息 + 时长 (水平排列) ===
            query_duration_mins = int(query_audio_player.duration) // 60
            query_duration_secs = int(query_audio_player.duration) % 60
            query_filename = os.path.basename(query_audio_player.audio_file)
            query_filename = clean_filename_for_display(query_filename)
            
            # 调整文件名长度适应水平布局
            if len(query_filename) > 18:
                query_filename = query_filename[:15] + "..."
            
            # 文件名 (左侧)
            query_file_text = controls_ax.text(0.52, 0.12, f"♪ {query_filename}", 
                                             fontsize=8, ha='left', va='center', weight='bold',
                                             color='#424242')
            
            # 时长 (右侧)
            query_duration_text = controls_ax.text(0.98, 0.12, f"⏱ {query_duration_mins:02}:{query_duration_secs:02}", 
                                                 fontsize=8, ha='right', va='center',
                                                 color='#757575')
            
            query_audio_player.time_display = query_time_text
            query_audio_player.status_text = query_status_text
            
            texts['query'] = {
                'time_text': query_time_text,
                'status_text': query_status_text,
                'file_text': query_file_text,
                'duration_text': query_duration_text
            }
    
    return texts 