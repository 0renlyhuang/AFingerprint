#!/usr/bin/env python3
"""
主要绘图模块
包含交互式绘图和比较绘图功能
"""

import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from matplotlib.patches import ConnectionPatch
from matplotlib.animation import FuncAnimation

from visualization.config import get_screen_size, _ui_refresh_interval, _current_audio_player
from visualization.plot_utils import detect_and_normalize_amplitude_values
from visualization.ui_components import create_audio_controls_layout, create_audio_text_layout

# Import all helper functions from plotting_helpers
from visualization.plotting_helpers import (
    _setup_audio_controls,
    _setup_comparison_audio_controls,
    _create_hover_callback,
    _setup_comparison_interactions,
    _draw_connection_lines,
    _add_window_event_handlers
)


def create_interactive_plot(data, plot_type='extraction', audio_player=None):
    """Create interactive plot with hover information and audio controls"""
    global _current_audio_player
    # Store reference to audio player for cleanup
    _current_audio_player = audio_player
    
    # 获取屏幕尺寸（使用改进的方法）
    screen_width, screen_height = get_screen_size()
    
    # 设置为屏幕宽度的2/3，高度的2/3
    fig_width = screen_width * 2 / 3 / 100  # 转换为英寸 (假设DPI=100)
    fig_height = screen_height * 2 / 3 / 100
    print(f"调整窗口大小: {fig_width:.1f}x{fig_height:.1f} inches (屏幕: {screen_width}x{screen_height})")
    
    # Create figure with room for audio controls at the bottom
    grid = gridspec.GridSpec(2, 1, height_ratios=[4, 1] if audio_player and audio_player.data is not None else [1, 0])
    fig = plt.figure(figsize=(fig_width, fig_height))
    ax = fig.add_subplot(grid[0])
    
    # Store annotation objects
    annot = ax.annotate("", xy=(0, 0), xytext=(20, 20),
                        textcoords="offset points",
                        bbox=dict(boxstyle="round", fc="w"),
                        arrowprops=dict(arrowstyle="->"))
    annot.set_visible(False)
    
    # Plot data based on type
    if plot_type == 'extraction':
        peaks_scatter, fp_scatter, matched_scatter, session_scatters = _plot_extraction_data(ax, data)
    elif plot_type == 'matching':
        peaks_scatter, fp_scatter, matched_scatter, session_scatters = _plot_matching_data(ax, data)
    
    # Set common properties
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Frequency (Hz)')
    ax.set_ylim(0, 5000)  # Limit frequency display range
    ax.grid(True, alpha=0.3)
    # 将图例移动到图表右侧外部，使用更紧凑的样式
    ax.legend(bbox_to_anchor=(1.05, 1), loc='upper left', fontsize=8, 
              frameon=True, fancybox=True, shadow=True, ncol=1)
    
    # Add a colorbar for amplitude visualization
    amplitude_info = detect_and_normalize_amplitude_values(data['allPeaks'])
    amplitude_label = "Amplitude (dB)" if amplitude_info['is_absolute_log_scale'] else "Amplitude"
    cbar = fig.colorbar(peaks_scatter, ax=ax, label=amplitude_label, pad=0.01)
    cbar.set_label(amplitude_label)
    
    # Calculate max time and set up audio controls
    max_time_from_data = _calculate_max_time(data)
    
    # Add playback position line if audio player is provided
    if audio_player and audio_player.data is not None:
        _setup_audio_controls(fig, grid, ax, audio_player, plot_type, max_time_from_data)
    else:
        # 如果没有任何音频播放器，根据数据设置横轴范围
        print("\n===== 没有音频播放器，根据数据设置横轴范围 =====")
        ax.set_xlim(0, max_time_from_data)
    
    # Create hover callback
    hover_callback = _create_hover_callback(ax, annot, amplitude_info, data, peaks_scatter, 
                                          fp_scatter, matched_scatter, session_scatters, fig)
    
    # Connect hover event
    fig.canvas.mpl_connect("motion_notify_event", hover_callback)
    
    # Add window event handlers
    _add_window_event_handlers(fig, audio_player)
    
    # 调整布局，为右侧图例留出空间（右边界从1.0调整为0.85）
    plt.tight_layout(rect=[0, 0, 0.85, 1])
    
    return fig, ax


def create_comparison_plot(source_data, query_data, top_sessions=None, source_audio_player=None, query_audio_player=None):
    """Create comparison plot showing source and query fingerprints side by side"""
    global _current_audio_player
    
    # 获取屏幕尺寸
    screen_width, screen_height = get_screen_size()
    
    # 设置为屏幕宽度的2/3，高度的2/3
    fig_width = screen_width * 2 / 3 / 100  # 转换为英寸 (假设DPI=100)
    fig_height = screen_height * 2 / 3 / 100
    print(f"调整窗口大小: {fig_width:.1f}x{fig_height:.1f} inches (屏幕: {screen_width}x{screen_height})")
    
    # 检查是否有任何音频播放器
    has_any_audio = (source_audio_player and source_audio_player.data is not None) or \
                    (query_audio_player and query_audio_player.data is not None)
    
    if has_any_audio:
        # Create a figure with space for audio controls at the bottom
        # 增加控制面板的高度比例，减少底部空白
        grid = gridspec.GridSpec(3, 1, height_ratios=[3.5, 3.5, 2.5])
        fig = plt.figure(figsize=(fig_width, fig_height))
        ax1 = fig.add_subplot(grid[0])  # Source plot
        ax2 = fig.add_subplot(grid[1])  # Query plot (removed sharex=ax1)
        # The grid[2] will be used for audio controls
    else:
        # Standard layout without audio
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(fig_width, fig_height))  # removed sharex=True
    
    # Plot source and query data
    source_scatter_objs = _plot_source_data(ax1, source_data)
    query_scatter_objs = _plot_query_data(ax2, query_data)
    
    # Calculate unified time range and setup audio if needed
    source_max_time, query_max_time = _calculate_comparison_time_ranges(source_data, query_data)
    
    if has_any_audio:
        _setup_comparison_audio_controls(fig, grid, ax1, ax2, source_audio_player, 
                                       query_audio_player, source_max_time, query_max_time)
    else:
        # 如果没有任何音频播放器，根据数据设置横轴范围
        print("\n===== 没有音频播放器，根据数据设置横轴范围 =====")
        ax1.set_xlim(0, source_max_time)
        ax2.set_xlim(0, query_max_time)
        print(f"设置源图横轴范围: 0 到 {source_max_time:.2f}s")
        print(f"设置查询图横轴范围: 0 到 {query_max_time:.2f}s")
    
    # 调整布局，为右侧图例留出空间（右边界从1.0调整为0.85），同时保持底部空间
    plt.tight_layout(rect=[0, 0.15, 0.85, 1])  # 减少底部边距并为右侧图例留空间
    
    # Add hover event handling and connection lines
    _setup_comparison_interactions(fig, ax1, ax2, source_data, query_data, source_scatter_objs, query_scatter_objs)
    
    # Draw connection lines between matched points
    _draw_connection_lines(fig, ax1, ax2, source_data, query_data)
    
    # Add window event handlers
    _add_window_event_handlers(fig, source_audio_player, query_audio_player)
    
    return fig, (ax1, ax2)


# Helper functions for plotting data
def _plot_extraction_data(ax, data):
    """Plot data for extraction mode"""
    # 检测和处理幅度值
    amplitude_info = detect_and_normalize_amplitude_values(data['allPeaks'])
    
    # Plot all peaks
    peaks_scatter = ax.scatter([peak[1] for peak in data['allPeaks']], 
                              [peak[0] for peak in data['allPeaks']], 
                              c=amplitude_info['amplitudes'], 
                              cmap='viridis', alpha=0.8, 
                              s=amplitude_info['sizes'],
                              vmin=0, vmax=100,
                              label='All Peaks')
    
    # Plot fingerprint points - 使用空心三角形以增强区分度
    fp_scatter = ax.scatter([point[1] for point in data['fingerprintPoints']], 
                           [point[0] for point in data['fingerprintPoints']], 
                           facecolors='none', edgecolors='red', s=20, marker='^', 
                           linewidth=2, label='Fingerprint Points')
    
    # Initialize matched_scatter to None for extraction mode
    matched_scatter = None
    session_scatters = {}
    
    # Set title and labels
    ax.set_title(f"Audio Fingerprint Extraction: {data['title']}")
    
    return peaks_scatter, fp_scatter, matched_scatter, session_scatters


def _plot_matching_data(ax, data):
    """Plot data for matching mode"""
    # 检测和处理幅度值
    amplitude_info = detect_and_normalize_amplitude_values(data['allPeaks'])
    
    # Plot all peaks
    peaks_scatter = ax.scatter([peak[1] for peak in data['allPeaks']], 
                              [peak[0] for peak in data['allPeaks']], 
                              c=amplitude_info['amplitudes'], 
                              cmap='viridis', alpha=0.8,
                              s=amplitude_info['sizes'],
                              vmin=0, vmax=100,
                              label='All Peaks')
    
    # Plot fingerprint points - 使用空心三角形以增强区分度
    fp_scatter = ax.scatter([point[1] for point in data['fingerprintPoints']], 
                           [point[0] for point in data['fingerprintPoints']], 
                           facecolors='none', edgecolors='red', s=20, marker='^', 
                           linewidth=0.5, label='Fingerprint Points')
    
    # Plot matched points - 支持session五角星标记
    matched_scatter = None
    session_scatters = {}  # 存储不同session的散点图对象
    if 'matchedPoints' in data and data['matchedPoints']:
        print(f"绘制匹配点: {len(data['matchedPoints'])} 个")
        
        # 检查是否有session信息
        if len(data['matchedPoints'][0]) > 3:  # 有session ID
            # 按session ID分组
            session_points = {}
            for point in data['matchedPoints']:
                session_id = point[3] if len(point) > 3 else 0
                if session_id not in session_points:
                    session_points[session_id] = []
                session_points[session_id].append(point)
            
            # 为每个session使用不同的颜色，所有都用五角星标记
            session_colors = ['red', 'orange', 'purple', 'brown', 'pink', 'gray', 'olive', 'cyan']
            
            for i, (session_id, points) in enumerate(session_points.items()):
                color = session_colors[i % len(session_colors)]
                
                # 绘制五角星标记的匹配点
                scatter = ax.scatter([point[1] for point in points], 
                                    [point[0] for point in points], 
                                    color=color, s=150, alpha=1.0, marker='*',  # 统一使用五角星
                                    edgecolors='black', linewidth=2,
                                    label=f'Session {session_id} Matches')
                session_scatters[session_id] = scatter
                
                print(f"Session {session_id}: {len(points)} 个匹配点，颜色: {color}")
        else:
            # 没有session信息，使用单一颜色的五角星
            matched_scatter = ax.scatter([point[1] for point in data['matchedPoints']], 
                                        [point[0] for point in data['matchedPoints']], 
                                        color='orange', s=150, alpha=1.0, marker='*',  # 统一使用五角星
                                        edgecolors='black', linewidth=2,
                                        label='Matched Points')
    
    # Set title and labels
    ax.set_title(f"Audio Fingerprint Matching: {data['title']}")
    
    return peaks_scatter, fp_scatter, matched_scatter, session_scatters


def _plot_source_data(ax1, source_data):
    """Plot source data for comparison"""
    print(f"绘制源数据: {len(source_data.get('allPeaks', []))} 个峰值")
    source_amplitude_info = detect_and_normalize_amplitude_values(source_data['allPeaks'])
    
    # Source peaks
    source_peaks_scatter = ax1.scatter([peak[1] for peak in source_data['allPeaks']], 
                                      [peak[0] for peak in source_data['allPeaks']], 
                                      c=source_amplitude_info['amplitudes'], 
                                      cmap='viridis', alpha=0.8, 
                                      s=source_amplitude_info['sizes'],
                                      vmin=0, vmax=100,
                                      label='Source Peaks')
    
    # Source fingerprint points - 使用空心三角形
    source_fp_scatter = ax1.scatter([point[1] for point in source_data['fingerprintPoints']], 
                                   [point[0] for point in source_data['fingerprintPoints']], 
                                   facecolors='none', edgecolors='blue', s=3, marker='^', 
                                   linewidth=0.5, label='Source Fingerprint')
    
    # Source matched points - 根据session ID使用不同颜色的五角星
    source_matched_scatter = None
    source_session_scatters = {}  # 存储不同session的散点图对象
    if 'matchedPoints' in source_data and source_data['matchedPoints']:
        print(f"绘制源数据匹配点: {len(source_data['matchedPoints'])} 个")
        
        # 如果有session信息，按session分组绘制
        if len(source_data['matchedPoints'][0]) > 3:  # 检查是否有session ID
            # 按session ID分组
            session_points = {}
            for point in source_data['matchedPoints']:
                session_id = point[3] if len(point) > 3 else 0
                if session_id not in session_points:
                    session_points[session_id] = []
                session_points[session_id].append(point)
            
            # 为每个session使用不同的颜色，所有都用五角星标记
            session_colors = ['red', 'orange', 'purple', 'brown', 'pink', 'gray', 'olive', 'cyan']
            
            for i, (session_id, points) in enumerate(session_points.items()):
                color = session_colors[i % len(session_colors)]
                
                # 绘制五角星标记的匹配点
                scatter = ax1.scatter([point[1] for point in points], 
                                    [point[0] for point in points], 
                                    color=color, s=150, alpha=1.0, marker='*',  # 统一使用五角星
                                    edgecolors='black', linewidth=2,
                                    label=f'Source Session {session_id}')
                source_session_scatters[session_id] = scatter
                
                print(f"源数据Session {session_id}: {len(points)} 个匹配点，颜色: {color}")
        else:
            # 没有session信息，使用单一颜色的五角星
            source_matched_scatter = ax1.scatter([point[1] for point in source_data['matchedPoints']], 
                                               [point[0] for point in source_data['matchedPoints']], 
                                               color='red', s=150, alpha=1.0, marker='*',  # 统一使用五角星
                                               edgecolors='black', linewidth=2,
                                               label='Source Matches')
    
    ax1.set_title(f"Source: {source_data['title']}")
    ax1.set_xlabel('Time (s)')
    ax1.set_ylabel('Frequency (Hz)')
    ax1.set_ylim(0, 5000)
    ax1.grid(True, alpha=0.3)
    # 将图例移动到图表右侧外部，使用更紧凑的样式
    ax1.legend(bbox_to_anchor=(1.05, 1), loc='upper left', fontsize=8, 
              frameon=True, fancybox=True, shadow=True, ncol=1)
    
    # Add colorbar for source
    source_amplitude_label = "Amplitude (dB)" if source_amplitude_info['is_absolute_log_scale'] else "Amplitude"
    cbar1 = plt.gcf().colorbar(source_peaks_scatter, ax=ax1, label=source_amplitude_label, pad=0.01)
    
    return source_peaks_scatter, source_fp_scatter, source_matched_scatter, source_session_scatters


def _plot_query_data(ax2, query_data):
    """Plot query data for comparison"""
    print(f"绘制查询数据: {len(query_data.get('allPeaks', []))} 个峰值")
    query_amplitude_info = detect_and_normalize_amplitude_values(query_data['allPeaks'])
    
    # Query peaks
    query_peaks_scatter = ax2.scatter([peak[1] for peak in query_data['allPeaks']], 
                                     [peak[0] for peak in query_data['allPeaks']], 
                                     c=query_amplitude_info['amplitudes'], 
                                     cmap='viridis', alpha=0.8,
                                     s=query_amplitude_info['sizes'],
                                     vmin=0, vmax=100,
                                     label='Query Peaks')
    
    # Query fingerprint points - 使用空心菱形以与源指纹点区分
    query_fp_scatter = ax2.scatter([point[1] for point in query_data['fingerprintPoints']], 
                                  [point[0] for point in query_data['fingerprintPoints']], 
                                  facecolors='none', edgecolors='blue', s=3, marker='^', 
                                  linewidth=0.5, label='Query Fingerprint')
    
    # Query matched points - 根据session ID使用不同颜色的五角星
    query_matched_scatter = None
    query_session_scatters = {}  # 存储不同session的散点图对象
    if 'matchedPoints' in query_data and query_data['matchedPoints']:
        print(f"绘制查询数据匹配点: {len(query_data['matchedPoints'])} 个")
        
        # 如果有session信息，按session分组绘制
        if len(query_data['matchedPoints'][0]) > 3:  # 检查是否有session ID
            # 按session ID分组
            session_points = {}
            for point in query_data['matchedPoints']:
                session_id = point[3] if len(point) > 3 else 0
                if session_id not in session_points:
                    session_points[session_id] = []
                session_points[session_id].append(point)
            
            # 为每个session使用不同的颜色，所有都用五角星标记（与源数据保持一致）
            session_colors = ['red', 'orange', 'purple', 'brown', 'pink', 'gray', 'olive', 'cyan']
            
            for i, (session_id, points) in enumerate(session_points.items()):
                color = session_colors[i % len(session_colors)]
                
                match_count = len(points)
                # 绘制五角星标记的匹配点
                scatter = ax2.scatter([point[1] for point in points], 
                                    [point[0] for point in points], 
                                    color=color, s=150, alpha=1.0, marker='*',  # 统一使用五角星
                                    edgecolors='black', linewidth=2,
                                    label=f'Query Session {session_id}_{match_count}')
                query_session_scatters[session_id] = scatter
                
                print(f"查询数据Session {session_id}: {len(points)} 个匹配点，颜色: {color}")
        else:
            # 没有session信息，使用单一颜色的五角星
            query_matched_scatter = ax2.scatter([point[1] for point in query_data['matchedPoints']], 
                                              [point[0] for point in query_data['matchedPoints']], 
                                              color='red', s=150, alpha=1.0, marker='*',  # 统一使用五角星
                                              edgecolors='black', linewidth=2,
                                              label='Query Matches')
    
    ax2.set_title(f"Query: {query_data['title']}")
    ax2.set_xlabel('Time (s)')
    ax2.set_ylabel('Frequency (Hz)')
    ax2.set_ylim(0, 5000)
    ax2.grid(True, alpha=0.3)
    # 将图例移动到图表右侧外部，使用更紧凑的样式
    ax2.legend(bbox_to_anchor=(1.05, 1), loc='upper left', fontsize=8, 
              frameon=True, fancybox=True, shadow=True, ncol=1)
    
    # Add colorbar for query
    query_amplitude_label = "Amplitude (dB)" if query_amplitude_info['is_absolute_log_scale'] else "Amplitude"
    cbar2 = plt.gcf().colorbar(query_peaks_scatter, ax=ax2, label=query_amplitude_label, pad=0.01)
    
    return query_peaks_scatter, query_fp_scatter, query_matched_scatter, query_session_scatters


# Helper functions for calculating time ranges and setting up interactions
def _calculate_max_time(data):
    """Calculate maximum time from data"""
    max_time_from_data = 0
    if data['allPeaks']:
        max_time_from_data = max(peak[1] for peak in data['allPeaks'])
    if data['fingerprintPoints']:
        max_fp_time = max(point[1] for point in data['fingerprintPoints'])
        max_time_from_data = max(max_time_from_data, max_fp_time)
    if 'matchedPoints' in data and data['matchedPoints']:
        max_matched_time = max(point[1] for point in data['matchedPoints'])
        max_time_from_data = max(max_time_from_data, max_matched_time)
    
    # 添加一点边距
    if max_time_from_data > 0:
        max_time_from_data += max_time_from_data * 0.05  # 增加5%的边距
    else:
        max_time_from_data = 10  # 默认值
        
    print(f"数据中的最大时间: {max_time_from_data:.2f}s")
    return max_time_from_data


def _calculate_comparison_time_ranges(source_data, query_data):
    """Calculate time ranges for comparison plots"""
    source_max_time = _calculate_max_time(source_data)
    query_max_time = _calculate_max_time(query_data)
    
    print(f"源数据中的最大时间: {source_max_time:.2f}s")
    print(f"查询数据中的最大时间: {query_max_time:.2f}s")
    
    return source_max_time, query_max_time


# ... (Continue with more helper functions)
# I'll continue with the rest of the helper functions in the next part due to length constraints 