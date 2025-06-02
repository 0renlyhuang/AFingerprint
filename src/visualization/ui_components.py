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
    创建响应式的音频控制面板布局
    使用相对位置和自适应间距，确保元素不会重叠
    
    Args:
        unified_max_time: 统一的横轴最大时间，用于设置滑块范围
    """
    # 获取控制面板的位置和大小
    pos = controls_ax.get_position()
    left, bottom, width, height = pos.x0, pos.y0, pos.width, pos.height
    
    # 定义布局参数（相对于控制面板）- 调整为更合理的比例
    button_height = 0.25  # 减小按钮高度占比
    slider_height = 0.20  # 减小滑块高度占比
    vertical_margin = 0  # 增加垂直边距
    horizontal_margin = 0.015  # 水平边距，减小以给文本更多空间
    
    # 计算垂直位置（从下往上）- 重新分布垂直空间
    slider_y = bottom + vertical_margin  # 滑块在底部
    button_y = bottom + height * 0.6   # 按钮在中上部，给文本留更多空间
    
    controls = {}  # 存储控件引用
    
    # 如果只有一个音频播放器，居中布局
    if (source_audio_player is not None) != (query_audio_player is not None):
        # 单个音频播放器的布局
        audio_player = source_audio_player or query_audio_player
        label_prefix = "Source" if source_audio_player else "Query"
        
        # 计算居中位置
        button_width = width * 0.12
        slider_width = width * 0.5
        center_x = left + width * 0.5
        
        # Play button (居中偏左)
        play_x = center_x - button_width * 1.2
        play_ax = plt.axes([play_x, button_y, button_width, height * button_height])
        play_button = Button(play_ax, f'Play {label_prefix}', 
                           color='lightgreen' if source_audio_player else 'lightblue',
                           hovercolor='green' if source_audio_player else 'blue')
        
        # Stop button (居中偏右)
        stop_x = center_x + button_width * 0.2
        stop_ax = plt.axes([stop_x, button_y, button_width, height * button_height])
        stop_button = Button(stop_ax, f'Stop {label_prefix}', 
                           color='lightcoral', hovercolor='red')
        
        # Time slider (居中)
        slider_x = center_x - slider_width / 2
        slider_ax = plt.axes([slider_x, slider_y, slider_width, height * slider_height])
        # 使用统一的最大时间，如果没有提供则使用音频播放器的时长
        slider_max_time = unified_max_time if unified_max_time is not None else audio_player.duration
        time_slider = Slider(slider_ax, f'{label_prefix} Time', 0, slider_max_time, valinit=0)
        
        controls[label_prefix.lower()] = {
            'play_button': play_button,
            'stop_button': stop_button,
            'time_slider': time_slider
        }
        
    else:
        # 双音频播放器的布局 - 重新设计以避免重叠
        # 将整个控制面板分为两个相等的区域，中间留有间隙
        panel_width = width * 0.48  # 每个面板占总宽度的48%
        middle_gap = width * 0.04   # 中间间隙占4%
        
        button_width = panel_width * 0.25  # 按钮宽度
        slider_width = panel_width * 0.6   # 滑块宽度
        
        if source_audio_player is not None:
            # Source controls (左侧面板)
            source_panel_left = left + horizontal_margin
            source_center_x = source_panel_left + panel_width / 2
            
            # Source play button
            source_play_x = source_center_x - button_width * 1.1
            source_play_ax = plt.axes([source_play_x, button_y, button_width, height * button_height])
            source_play_button = Button(source_play_ax, 'Play Source', color='lightblue', hovercolor='blue')
            
            # Source stop button
            source_stop_x = source_center_x + button_width * 0.1
            source_stop_ax = plt.axes([source_stop_x, button_y, button_width, height * button_height])
            source_stop_button = Button(source_stop_ax, 'Stop Source', color='lightcoral', hovercolor='red')
            
            # Source time slider
            source_slider_x = source_panel_left + (panel_width - slider_width) / 2
            source_slider_ax = plt.axes([source_slider_x, slider_y, slider_width, height * slider_height])
            # 使用统一的最大时间，如果没有提供则使用源音频播放器的时长
            source_slider_max_time = unified_max_time if unified_max_time is not None else source_audio_player.duration
            source_time_slider = Slider(source_slider_ax, 'Source Time', 0, source_slider_max_time, valinit=0)
            
            controls['source'] = {
                'play_button': source_play_button,
                'stop_button': source_stop_button,
                'time_slider': source_time_slider
            }
        
        if query_audio_player is not None:
            # Query controls (右侧面板)
            query_panel_left = left + width * 0.52  # 右侧面板开始位置
            query_center_x = query_panel_left + panel_width / 2
            
            # Query play button
            query_play_x = query_center_x - button_width * 1.1
            query_play_ax = plt.axes([query_play_x, button_y, button_width, height * button_height])
            query_play_button = Button(query_play_ax, 'Play Query', color='lightgreen', hovercolor='green')
            
            # Query stop button
            query_stop_x = query_center_x + button_width * 0.1
            query_stop_ax = plt.axes([query_stop_x, button_y, button_width, height * button_height])
            query_stop_button = Button(query_stop_ax, 'Stop Query', color='lightcoral', hovercolor='red')
            
            # Query time slider
            query_slider_x = query_panel_left + (panel_width - slider_width) / 2
            query_slider_ax = plt.axes([query_slider_x, slider_y, slider_width, height * slider_height])
            # 使用统一的最大时间，如果没有提供则使用查询音频播放器的时长
            query_slider_max_time = unified_max_time if unified_max_time is not None else query_audio_player.duration
            query_time_slider = Slider(query_slider_ax, 'Query Time', 0, query_slider_max_time, valinit=0)
            
            controls['query'] = {
                'play_button': query_play_button,
                'stop_button': query_stop_button,
                'time_slider': query_time_slider
            }
    
    return controls


def create_audio_text_layout(controls_ax, source_audio_player=None, query_audio_player=None):
    """
    创建响应式的音频文本布局
    使用层次化布局和相对位置确保文本不会重叠
    """
    texts = {}
    
    # 如果只有一个音频播放器，居中布局
    if (source_audio_player is not None) != (query_audio_player is not None):
        audio_player = source_audio_player or query_audio_player
        label_prefix = "Source" if source_audio_player else "Query"
        
        # 居中布局的文本位置 - 垂直分层
        time_text = controls_ax.text(0.5, 0.95, f"{label_prefix}: 00:00", 
                                   fontsize=11, ha='center', va='top', weight='bold')
        status_text = controls_ax.text(0.5, 0.82, "Ready", 
                                     fontsize=10, ha='center', va='center', style='italic')
        
        # 文件信息 - 智能截断长文件名
        duration_mins = int(audio_player.duration) // 60
        duration_secs = int(audio_player.duration) % 60
        filename = os.path.basename(audio_player.audio_file)
        
        # 清理文件名中的emoji和特殊字符，避免字体渲染错误
        filename = clean_filename_for_display(filename)
        
        # 截断过长的文件名
        if len(filename) > 40:
            filename = filename[:37] + "..."
            
        file_info = f"{label_prefix}: {filename}\nDuration: {duration_mins:02}:{duration_secs:02}"
        file_text = controls_ax.text(0.5, 0.15, file_info, 
                                   fontsize=9, ha='center', va='bottom')
        
        audio_player.time_display = time_text
        audio_player.status_text = status_text
        
        texts[label_prefix.lower()] = {
            'time_text': time_text,
            'status_text': status_text,
            'file_text': file_text
        }
        
    else:
        # 双音频播放器的文本布局 - 重新设计为分栏布局
        # 左栏：0-0.48，右栏：0.52-1.0，中间留4%间隙
        
        if source_audio_player is not None:
            # Source文本 (左栏) - 重新设计垂直布局
            left_center = 0.24  # 左栏中心位置
            
            # 垂直分层布局：从上到下分别是时间、状态、文件信息
            source_time_text = controls_ax.text(left_center, 0.9, "Source: 00:00", 
                                              fontsize=10, ha='center', va='center', weight='bold',
                                              color='darkblue')
            source_status_text = controls_ax.text(left_center, 0.75, "Ready", 
                                                fontsize=9, ha='center', va='center', style='italic',
                                                color='gray')
            
            # Source文件信息 - 智能处理文件名
            source_duration_mins = int(source_audio_player.duration) // 60
            source_duration_secs = int(source_audio_player.duration) % 60
            source_filename = os.path.basename(source_audio_player.audio_file)
            
            # 清理文件名中的emoji和特殊字符，避免字体渲染错误
            source_filename = clean_filename_for_display(source_filename)
            
            # 为双栏布局截断更短的文件名
            if len(source_filename) > 25:
                source_filename = source_filename[:22] + "..."
                
            source_file_info = f"{source_filename}\n{source_duration_mins:02}:{source_duration_secs:02}"
            source_file_text = controls_ax.text(0.02, 0.35, source_file_info, 
                                              fontsize=8, ha='left', va='center',
                                              color='darkblue')
            
            source_audio_player.time_display = source_time_text
            source_audio_player.status_text = source_status_text
            
            texts['source'] = {
                'time_text': source_time_text,
                'status_text': source_status_text,
                'file_text': source_file_text
            }
        
        if query_audio_player is not None:
            # Query文本 (右栏) - 重新设计垂直布局
            right_center = 0.76  # 右栏中心位置
            
            # 垂直分层布局：从上到下分别是时间、状态、文件信息
            query_time_text = controls_ax.text(right_center, 0.9, "Query: 00:00", 
                                             fontsize=10, ha='center', va='center', weight='bold',
                                             color='darkgreen')
            query_status_text = controls_ax.text(right_center, 0.75, "Ready", 
                                               fontsize=9, ha='center', va='center', style='italic',
                                               color='gray')
            
            # Query文件信息 - 智能处理文件名
            query_duration_mins = int(query_audio_player.duration) // 60
            query_duration_secs = int(query_audio_player.duration) % 60
            query_filename = os.path.basename(query_audio_player.audio_file)
            
            # 清理文件名中的emoji和特殊字符，避免字体渲染错误
            query_filename = clean_filename_for_display(query_filename)
            
            # 为双栏布局截断更短的文件名
            if len(query_filename) > 25:
                query_filename = query_filename[:22] + "..."
                
            query_file_info = f"{query_filename}\n{query_duration_mins:02}:{query_duration_secs:02}"
            query_file_text = controls_ax.text(0.52, 0.35, query_file_info, 
                                             fontsize=8, ha='left', va='center',
                                             color='darkgreen')
            
            query_audio_player.time_display = query_time_text
            query_audio_player.status_text = query_status_text
            
            texts['query'] = {
                'time_text': query_time_text,
                'status_text': query_status_text,
                'file_text': query_file_text
            }
    
    return texts 