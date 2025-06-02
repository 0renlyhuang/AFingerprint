#!/usr/bin/env python3
"""
绘图辅助函数模块
包含各种辅助函数，支持主绘图模块
"""

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.patches import ConnectionPatch

from visualization.config import _ui_refresh_interval
from visualization.ui_components import create_audio_controls_layout, create_audio_text_layout
from visualization.plot_utils import detect_and_normalize_amplitude_values


def _setup_audio_controls(fig, grid, ax, audio_player, plot_type, max_time_from_data):
    """Setup audio controls for single player mode"""
    # Add a vertical line to show playback position
    audio_player.playback_line = ax.axvline(x=0, color='orange' if plot_type == 'extraction' else 'green', 
                                          linestyle='-', linewidth=2)
    
    # 使用数据中的最大时间和音频时长的较大值作为横轴范围
    final_max_time = max(max_time_from_data, audio_player.duration)
    ax.set_xlim(0, final_max_time)
    print(f"设置横轴范围: 0 到 {final_max_time:.2f}s (数据最大时间: {max_time_from_data:.2f}s, 音频时长: {audio_player.duration:.2f}s)")
    
    # Create audio control panel
    controls_ax = fig.add_subplot(grid[1])
    controls_ax.set_facecolor('lightgray')
    controls_ax.set_xticks([])
    controls_ax.set_yticks([])
    
    # 使用新的相对布局系统创建音频控件
    # 计算单个音频播放器模式下的统一最大时间
    single_unified_max_time = max(max_time_from_data, audio_player.duration)
    controls = create_audio_controls_layout(fig, controls_ax, 
                                           source_audio_player=audio_player if plot_type == 'extraction' else None,
                                           query_audio_player=audio_player if plot_type == 'matching' else None,
                                           unified_max_time=single_unified_max_time)
    texts = create_audio_text_layout(controls_ax, 
                                    source_audio_player=audio_player if plot_type == 'extraction' else None,
                                    query_audio_player=audio_player if plot_type == 'matching' else None)
    
    # 设置按钮引用
    if audio_player and plot_type == 'extraction' and 'source' in controls:
        audio_player.play_button = controls['source']['play_button']
    elif audio_player and plot_type == 'matching' and 'query' in controls:
        audio_player.play_button = controls['query']['play_button']
    
    # 创建音频控制事件处理器
    _setup_single_audio_events(audio_player, controls, plot_type)
    
    # Add click handler to seek in the main plot
    def on_plot_click(event):
        if event.inaxes == ax:
            time_pos = event.xdata
            if time_pos is not None:
                if time_pos < 0:
                    time_pos = 0
                elif audio_player and time_pos > audio_player.duration:
                    time_pos = audio_player.duration
                
                if audio_player:
                    audio_player.seek(time_pos)
    
    fig.canvas.mpl_connect('button_press_event', on_plot_click)
    
    # 设置定时器用于更新播放进度
    def update_playback_ui(frame):
        updated = False
        if audio_player and audio_player.playing:
            if audio_player.update_ui():
                updated = True
        if updated:
            fig.canvas.draw_idle()
        return []
    
    # 使用全局刷新率配置
    ani = FuncAnimation(fig, update_playback_ui, interval=_ui_refresh_interval, 
                      blit=True, cache_frame_data=False)
    # 保存动画对象的引用，防止被垃圾回收
    fig.ani = ani


def _setup_single_audio_events(audio_player, controls, plot_type):
    """Setup events for single audio player mode"""
    if audio_player and plot_type == 'extraction' and 'source' in controls:
        def on_play(event):
            print(f"\n===== 音频播放按钮被点击 =====")
            if audio_player.playing:
                print("停止音频播放")
                audio_player.stop()
                controls['source']['play_button'].label.set_text('Play Source')
            else:
                if audio_player.current_time >= audio_player.duration - 0.1:
                    print("音频从头开始播放")
                    audio_player.restart()
                else:
                    print(f"音频从当前位置继续播放: {audio_player.current_time:.2f}秒")
                    audio_player.play(audio_player.current_time)
                controls['source']['play_button'].label.set_text('Pause Source')
            controls['source']['play_button'].ax.figure.canvas.draw_idle()
        
        def on_stop(event):
            print("音频停止按钮被点击")
            audio_player.stop()
            controls['source']['play_button'].label.set_text('Play Source')
            controls['source']['play_button'].ax.figure.canvas.draw_idle()
        
        def on_slider_changed(val):
            print(f"音频滑块被调整: {val:.2f}")
            audio_player.seek(val)
        
        controls['source']['play_button'].on_clicked(on_play)
        controls['source']['stop_button'].on_clicked(on_stop)
        controls['source']['time_slider'].on_changed(on_slider_changed)
    
    elif audio_player and plot_type == 'matching' and 'query' in controls:
        def on_play(event):
            print(f"\n===== 音频播放按钮被点击 =====")
            if audio_player.playing:
                print("停止音频播放")
                audio_player.stop()
                controls['query']['play_button'].label.set_text('Play Query')
            else:
                if audio_player.current_time >= audio_player.duration - 0.1:
                    print("音频从头开始播放")
                    audio_player.restart()
                else:
                    print(f"音频从当前位置继续播放: {audio_player.current_time:.2f}秒")
                    audio_player.play(audio_player.current_time)
                controls['query']['play_button'].label.set_text('Pause Query')
            controls['query']['play_button'].ax.figure.canvas.draw_idle()
        
        def on_stop(event):
            print("音频停止按钮被点击")
            audio_player.stop()
            controls['query']['play_button'].label.set_text('Play Query')
            controls['query']['play_button'].ax.figure.canvas.draw_idle()
        
        def on_slider_changed(val):
            print(f"音频滑块被调整: {val:.2f}")
            audio_player.seek(val)
        
        controls['query']['play_button'].on_clicked(on_play)
        controls['query']['stop_button'].on_clicked(on_stop)
        controls['query']['time_slider'].on_changed(on_slider_changed)


def _setup_comparison_audio_controls(fig, grid, ax1, ax2, source_audio_player, 
                                   query_audio_player, source_max_time, query_max_time):
    """Setup audio controls for comparison mode"""
    print("\n===== 添加音频播放控制 =====")
    
    # Add audio control panel at the bottom
    controls_ax = fig.add_subplot(grid[2])
    controls_ax.set_facecolor('lightgray')
    controls_ax.set_xticks([])
    controls_ax.set_yticks([])
    
    # 计算统一的横轴最大值：取两个音频时长的较大者，并与数据最大时间比较
    time_candidates = [source_max_time, query_max_time]
    
    if source_audio_player and source_audio_player.data is not None:
        time_candidates.append(source_audio_player.duration)
        print(f"源音频时长: {source_audio_player.duration:.2f}s")
    
    if query_audio_player and query_audio_player.data is not None:
        time_candidates.append(query_audio_player.duration)
        print(f"查询音频时长: {query_audio_player.duration:.2f}s")
    
    # 取所有时间值中的最大值作为统一的横轴范围
    unified_max_time = max(time_candidates)
    print(f"统一横轴最大值: {unified_max_time:.2f}s (候选值: {[f'{t:.2f}s' for t in time_candidates]})")
    
    # 使用新的相对布局系统创建音频控件
    controls = create_audio_controls_layout(fig, controls_ax, source_audio_player, query_audio_player, unified_max_time)
    texts = create_audio_text_layout(controls_ax, source_audio_player, query_audio_player)
    
    # Add playback position lines and set axis ranges
    if source_audio_player and source_audio_player.data is not None:
        source_audio_player.playback_line = ax1.axvline(x=0, color='orange', linestyle='-', linewidth=2)
        ax1.set_xlim(0, unified_max_time)
        print(f"设置源图横轴范围: 0 到 {unified_max_time:.2f}s (统一范围)")
    else:
        # 如果没有源音频播放器，使用统一的最大时间
        ax1.set_xlim(0, unified_max_time)
        print(f"设置源图横轴范围: 0 到 {unified_max_time:.2f}s (统一范围，无源音频)")
    
    if query_audio_player and query_audio_player.data is not None:
        query_audio_player.playback_line = ax2.axvline(x=0, color='green', linestyle='-', linewidth=2)
        ax2.set_xlim(0, unified_max_time)
        print(f"设置查询图横轴范围: 0 到 {unified_max_time:.2f}s (统一范围)")
    else:
        # 如果没有查询音频播放器，使用统一的最大时间
        ax2.set_xlim(0, unified_max_time)
        print(f"设置查询图横轴范围: 0 到 {unified_max_time:.2f}s (统一范围，无查询音频)")
    
    # Setup events
    _setup_comparison_audio_events(fig, ax1, ax2, source_audio_player, query_audio_player, controls)


def _setup_comparison_audio_events(fig, ax1, ax2, source_audio_player, query_audio_player, controls):
    """Setup events for comparison audio players"""
    # 创建音频控制事件处理器
    if source_audio_player and 'source' in controls:
        def on_source_play(event):
            print(f"\n===== 源音频播放按钮被点击 =====")
            if source_audio_player.playing:
                print("停止源音频播放")
                source_audio_player.stop()
                controls['source']['play_button'].label.set_text('Play Source')
            else:
                if source_audio_player.current_time >= source_audio_player.duration - 0.1:
                    print("源音频从头开始播放")
                    source_audio_player.restart()
                else:
                    print(f"源音频从当前位置继续播放: {source_audio_player.current_time:.2f}秒")
                    source_audio_player.play(source_audio_player.current_time)
                controls['source']['play_button'].label.set_text('Pause Source')
            controls['source']['play_button'].ax.figure.canvas.draw_idle()
        
        def on_source_stop(event):
            print("源音频停止按钮被点击")
            source_audio_player.stop()
            controls['source']['play_button'].label.set_text('Play Source')
            controls['source']['play_button'].ax.figure.canvas.draw_idle()
        
        def on_source_slider_changed(val):
            print(f"源音频滑块被调整: {val:.2f}")
            source_audio_player.seek(val)
        
        controls['source']['play_button'].on_clicked(on_source_play)
        controls['source']['stop_button'].on_clicked(on_source_stop)
        controls['source']['time_slider'].on_changed(on_source_slider_changed)
    
    if query_audio_player and 'query' in controls:
        def on_query_play(event):
            print(f"\n===== 查询音频播放按钮被点击 =====")
            if query_audio_player.playing:
                print("停止查询音频播放")
                query_audio_player.stop()
                controls['query']['play_button'].label.set_text('Play Query')
            else:
                if query_audio_player.current_time >= query_audio_player.duration - 0.1:
                    print("查询音频从头开始播放")
                    query_audio_player.restart()
                else:
                    print(f"查询音频从当前位置继续播放: {query_audio_player.current_time:.2f}秒")
                    query_audio_player.play(query_audio_player.current_time)
                controls['query']['play_button'].label.set_text('Pause Query')
            controls['query']['play_button'].ax.figure.canvas.draw_idle()
        
        def on_query_stop(event):
            print("查询音频停止按钮被点击")
            query_audio_player.stop()
            controls['query']['play_button'].label.set_text('Play Query')
            controls['query']['play_button'].ax.figure.canvas.draw_idle()
        
        def on_query_slider_changed(val):
            print(f"查询音频滑块被调整: {val:.2f}")
            query_audio_player.seek(val)
        
        controls['query']['play_button'].on_clicked(on_query_play)
        controls['query']['stop_button'].on_clicked(on_query_stop)
        controls['query']['time_slider'].on_changed(on_query_slider_changed)
    
    # Add click handlers to seek in the plots
    def on_plot_click(event):
        if event.inaxes == ax1 and source_audio_player:  # 在源图上点击
            time_pos = event.xdata
            if time_pos is not None:
                if time_pos < 0:
                    time_pos = 0
                elif time_pos > source_audio_player.duration:
                    # 限制在源音频的实际时长内，但允许在统一横轴范围内点击
                    time_pos = source_audio_player.duration
                
                source_audio_player.seek(time_pos)
                if 'source' in controls:
                    controls['source']['time_slider'].set_val(time_pos)
        elif event.inaxes == ax2 and query_audio_player:  # 在查询图上点击
            time_pos = event.xdata
            if time_pos is not None:
                if time_pos < 0:
                    time_pos = 0
                elif time_pos > query_audio_player.duration:
                    # 限制在查询音频的实际时长内，但允许在统一横轴范围内点击
                    time_pos = query_audio_player.duration
                
                query_audio_player.seek(time_pos)
                if 'query' in controls:
                    controls['query']['time_slider'].set_val(time_pos)
    
    fig.canvas.mpl_connect('button_press_event', on_plot_click)
    
    # 设置定时器用于更新播放进度
    def update_playback_ui(frame):
        updated = False
        if source_audio_player and source_audio_player.playing:
            if source_audio_player.update_ui():
                updated = True
        if query_audio_player and query_audio_player.playing:
            if query_audio_player.update_ui():
                updated = True
        if updated:
            fig.canvas.draw_idle()
        return []
    
    # 使用全局刷新率配置
    ani = FuncAnimation(fig, update_playback_ui, interval=_ui_refresh_interval, 
                      blit=True, cache_frame_data=False)
    # 保存动画对象的引用，防止被垃圾回收
    fig.ani = ani


def _create_hover_callback(ax, annot, amplitude_info, data, peaks_scatter, 
                         fp_scatter, matched_scatter, session_scatters, fig):
    """Create hover callback function"""
    def update_annot(ind, scatter_obj, point_type):
        index = ind["ind"][0]
        if scatter_obj == peaks_scatter:
            pos = scatter_obj.get_offsets()[index]
            annot.xy = pos
            # 使用原始幅度值和适当的格式进行显示
            original_amp = amplitude_info['original_amplitudes'][index]
            text = f"Peak\nFreq: {data['allPeaks'][index][0]} Hz\nTime: {data['allPeaks'][index][1]:.2f} s\nAmplitude: {original_amp:{amplitude_info['amplitude_format']}}"
            if amplitude_info['is_absolute_log_scale']:
                text += " dB"
        elif scatter_obj == fp_scatter:
            pos = scatter_obj.get_offsets()[index]
            annot.xy = pos
            point = data['fingerprintPoints'][index]
            text = f"Fingerprint\nFreq: {point[0]} Hz\nTime: {point[1]:.2f} s\nHash: {point[2]}"
        elif matched_scatter and scatter_obj == matched_scatter:
            pos = scatter_obj.get_offsets()[index]
            annot.xy = pos
            point = data['matchedPoints'][index]
            text = f"Match\nFreq: {point[0]} Hz\nTime: {point[1]:.2f} s\nHash: {point[2]}\nSession: {point[3] if len(point) > 3 else 'N/A'}"
        else:
            # 检查是否是session散点图
            for session_id, session_scatter in session_scatters.items():
                if scatter_obj == session_scatter:
                    pos = scatter_obj.get_offsets()[index]
                    annot.xy = pos
                    # 找到对应的匹配点
                    session_points = [p for p in data['matchedPoints'] if len(p) > 3 and p[3] == session_id]
                    if index < len(session_points):
                        point = session_points[index]
                        text = f"Match\nFreq: {point[0]} Hz\nTime: {point[1]:.2f} s\nHash: {point[2]}\nSession: {session_id}"
                    else:
                        text = f"Session {session_id} Match"
                    break
        annot.set_text(text)
        annot.get_bbox_patch().set_alpha(0.9)
    
    def hover(event):
        vis = annot.get_visible()
        if event.inaxes == ax:
            # 检查所有散点图对象
            scatter_objects = [(peaks_scatter, "peak"), (fp_scatter, "fingerprint")]
            if matched_scatter:
                scatter_objects.append((matched_scatter, "match"))
            # 添加session散点图
            for session_id, session_scatter in session_scatters.items():
                scatter_objects.append((session_scatter, f"session_{session_id}"))
            
            for scatter_obj, point_type in scatter_objects:
                cont, ind = scatter_obj.contains(event)
                if cont:
                    update_annot(ind, scatter_obj, point_type)
                    annot.set_visible(True)
                    fig.canvas.draw_idle()
                    return
        
        if vis:
            annot.set_visible(False)
            fig.canvas.draw_idle()
    
    return hover


def _setup_comparison_interactions(fig, ax1, ax2, source_data, query_data, source_scatter_objs, query_scatter_objs):
    """Setup hover interactions for comparison plots"""
    # Unpack scatter objects
    source_peaks_scatter, source_fp_scatter, source_matched_scatter, source_session_scatters = source_scatter_objs
    query_peaks_scatter, query_fp_scatter, query_matched_scatter, query_session_scatters = query_scatter_objs
    
    # Get amplitude info for hover
    source_amplitude_info = detect_and_normalize_amplitude_values(source_data['allPeaks'])
    query_amplitude_info = detect_and_normalize_amplitude_values(query_data['allPeaks'])
    
    # 添加hover事件处理 - 支持session匹配点
    # 创建注释对象
    source_annot = ax1.annotate("", xy=(0, 0), xytext=(20, 20),
                               textcoords="offset points",
                               bbox=dict(boxstyle="round", fc="w"),
                               arrowprops=dict(arrowstyle="->"))
    source_annot.set_visible(False)
    
    query_annot = ax2.annotate("", xy=(0, 0), xytext=(20, 20),
                              textcoords="offset points",
                              bbox=dict(boxstyle="round", fc="w"),
                              arrowprops=dict(arrowstyle="->"))
    query_annot.set_visible(False)
    
    def update_source_annot(ind, scatter_obj, point_type):
        index = ind["ind"][0]
        if scatter_obj == source_peaks_scatter:
            pos = scatter_obj.get_offsets()[index]
            source_annot.xy = pos
            original_amp = source_amplitude_info['original_amplitudes'][index]
            text = f"Source Peak\nFreq: {source_data['allPeaks'][index][0]} Hz\nTime: {source_data['allPeaks'][index][1]:.2f} s\nAmplitude: {original_amp:{source_amplitude_info['amplitude_format']}}"
            if source_amplitude_info['is_absolute_log_scale']:
                text += " dB"
        elif scatter_obj == source_fp_scatter:
            pos = scatter_obj.get_offsets()[index]
            source_annot.xy = pos
            point = source_data['fingerprintPoints'][index]
            text = f"Source Fingerprint\nFreq: {point[0]} Hz\nTime: {point[1]:.2f} s\nHash: {point[2]}"
        elif scatter_obj == source_matched_scatter:
            pos = scatter_obj.get_offsets()[index]
            source_annot.xy = pos
            point = source_data['matchedPoints'][index]
            text = f"Source Match\nFreq: {point[0]} Hz\nTime: {point[1]:.2f} s\nHash: {point[2]}"
            if len(point) > 3:
                text += f"\nSession: {point[3]}"
        else:
            # 检查是否是session散点图
            for session_id, session_scatter in source_session_scatters.items():
                if scatter_obj == session_scatter:
                    pos = scatter_obj.get_offsets()[index]
                    source_annot.xy = pos
                    # 找到对应的匹配点
                    session_points = [p for p in source_data['matchedPoints'] if len(p) > 3 and p[3] == session_id]
                    if index < len(session_points):
                        point = session_points[index]
                        text = f"Source Match\nFreq: {point[0]} Hz\nTime: {point[1]:.2f} s\nHash: {point[2]}\nSession: {session_id}"
                    else:
                        text = f"Source Session {session_id} Match"
                    break
        source_annot.set_text(text)
        source_annot.get_bbox_patch().set_alpha(0.9)
    
    def update_query_annot(ind, scatter_obj, point_type):
        index = ind["ind"][0]
        if scatter_obj == query_peaks_scatter:
            pos = scatter_obj.get_offsets()[index]
            query_annot.xy = pos
            original_amp = query_amplitude_info['original_amplitudes'][index]
            text = f"Query Peak\nFreq: {query_data['allPeaks'][index][0]} Hz\nTime: {query_data['allPeaks'][index][1]:.2f} s\nAmplitude: {original_amp:{query_amplitude_info['amplitude_format']}}"
            if query_amplitude_info['is_absolute_log_scale']:
                text += " dB"
        elif scatter_obj == query_fp_scatter:
            pos = scatter_obj.get_offsets()[index]
            query_annot.xy = pos
            point = query_data['fingerprintPoints'][index]
            text = f"Query Fingerprint\nFreq: {point[0]} Hz\nTime: {point[1]:.2f} s\nHash: {point[2]}"
        elif scatter_obj == query_matched_scatter:
            pos = scatter_obj.get_offsets()[index]
            query_annot.xy = pos
            point = query_data['matchedPoints'][index]
            text = f"Query Match\nFreq: {point[0]} Hz\nTime: {point[1]:.2f} s\nHash: {point[2]}\nSession: {point[3] if len(point) > 3 else 'N/A'}"
        else:
            # 检查是否是session散点图
            for session_id, session_scatter in query_session_scatters.items():
                if scatter_obj == session_scatter:
                    pos = scatter_obj.get_offsets()[index]
                    query_annot.xy = pos
                    # 找到对应的匹配点
                    session_points = [p for p in query_data['matchedPoints'] if len(p) > 3 and p[3] == session_id]
                    if index < len(session_points):
                        point = session_points[index]
                        text = f"Query Match\nFreq: {point[0]} Hz\nTime: {point[1]:.2f} s\nHash: {point[2]}\nSession: {session_id}"
                    else:
                        text = f"Query Session {session_id} Match"
                    break
        query_annot.set_text(text)
        query_annot.get_bbox_patch().set_alpha(0.9)
    
    # 创建hover回调函数
    def hover(event):
        source_vis = source_annot.get_visible()
        query_vis = query_annot.get_visible()
        
        if event.inaxes == ax1:  # 源图
            # 检查所有散点图对象
            scatter_objects = [(source_peaks_scatter, "peak"), (source_fp_scatter, "fingerprint")]
            if source_matched_scatter:
                scatter_objects.append((source_matched_scatter, "match"))
            # 添加session散点图
            for session_id, session_scatter in source_session_scatters.items():
                scatter_objects.append((session_scatter, f"session_{session_id}"))
            
            for scatter_obj, point_type in scatter_objects:
                cont, ind = scatter_obj.contains(event)
                if cont:
                    update_source_annot(ind, scatter_obj, point_type)
                    source_annot.set_visible(True)
                    fig.canvas.draw_idle()
                    return
            
            if source_vis:
                source_annot.set_visible(False)
                fig.canvas.draw_idle()
                
        elif event.inaxes == ax2:  # 查询图
            # 检查所有散点图对象
            scatter_objects = [(query_peaks_scatter, "peak"), (query_fp_scatter, "fingerprint")]
            if query_matched_scatter:
                scatter_objects.append((query_matched_scatter, "match"))
            # 添加session散点图
            for session_id, session_scatter in query_session_scatters.items():
                scatter_objects.append((session_scatter, f"session_{session_id}"))
            
            for scatter_obj, point_type in scatter_objects:
                cont, ind = scatter_obj.contains(event)
                if cont:
                    update_query_annot(ind, scatter_obj, point_type)
                    query_annot.set_visible(True)
                    fig.canvas.draw_idle()
                    return
            
            if query_vis:
                query_annot.set_visible(False)
                fig.canvas.draw_idle()
    
    # 连接hover事件
    fig.canvas.mpl_connect("motion_notify_event", hover)


def _draw_connection_lines(fig, ax1, ax2, source_data, query_data):
    """Draw connection lines between matched points in source and query"""
    # 在Source和Query之间绘制匹配连线
    if ('matchedPoints' in source_data and source_data['matchedPoints'] and 
        'matchedPoints' in query_data and query_data['matchedPoints']):
        
        print(f"\n===== 绘制Source和Query之间的匹配连线 =====")
        
        # 按session分组匹配点
        source_sessions = {}
        query_sessions = {}
        
        # 分组源数据匹配点
        for point in source_data['matchedPoints']:
            session_id = point[3] if len(point) > 3 else 0
            if session_id not in source_sessions:
                source_sessions[session_id] = []
            source_sessions[session_id].append(point)
        
        # 分组查询数据匹配点
        for point in query_data['matchedPoints']:
            session_id = point[3] if len(point) > 3 else 0
            if session_id not in query_sessions:
                query_sessions[session_id] = []
            query_sessions[session_id].append(point)
        
        # 计算每个session的匹配点数量，并选择top 3
        common_sessions = set(source_sessions.keys()) & set(query_sessions.keys())
        session_match_counts = {}
        
        for session_id in common_sessions:
            source_points = source_sessions[session_id]
            query_points = query_sessions[session_id]
            
            # 计算实际的hash匹配数量
            match_count = 0
            for source_point in source_points:
                source_hash = source_point[2] if len(source_point) > 2 else None
                for query_point in query_points:
                    query_hash = query_point[2] if len(query_point) > 2 else None
                    if source_hash == query_hash and source_hash is not None:
                        match_count += 1
                        break  # 找到匹配后跳出内层循环
            
            session_match_counts[session_id] = match_count
        
        # 按匹配数量排序，选择top 3
        top_sessions = sorted(session_match_counts.items(), key=lambda x: x[1], reverse=True)[:3]
        top_session_ids = [session_id for session_id, count in top_sessions]
        
        print(f"所有session匹配数量: {session_match_counts}")
        print(f"Top 3 sessions: {[(sid, count) for sid, count in top_sessions]}")
        
        # 为每个session绘制Source到Query的连线
        session_colors = ['red', 'orange', 'purple', 'brown', 'pink', 'gray', 'olive', 'cyan']
        
        # 只为top 3个session绘制连线
        for session_id in top_session_ids:
            color = session_colors[session_id % len(session_colors)]
            source_points = source_sessions[session_id]
            query_points = query_sessions[session_id]
            
            print(f"绘制连线 - Session {session_id}: {len(source_points)} 源点, {len(query_points)} 查询点")
            
            # 为每个源点找到对应的查询点（基于hash匹配）
            for source_point in source_points:
                source_hash = source_point[2] if len(source_point) > 2 else None
                source_time = source_point[1]
                source_freq = source_point[0]
                
                # 在查询点中找到相同hash的点
                for query_point in query_points:
                    query_hash = query_point[2] if len(query_point) > 2 else None
                    query_time = query_point[1]
                    query_freq = query_point[0]
                    
                    # 如果hash匹配，绘制连线
                    if source_hash == query_hash and source_hash is not None:
                        # 创建连接两个子图的连线
                        con = ConnectionPatch(
                            xyA=(source_time, source_freq), coordsA=ax1.transData,
                            xyB=(query_time, query_freq), coordsB=ax2.transData,
                            axesA=ax1, axesB=ax2,
                            color=color, alpha=1, linewidth=1.5, linestyle='--'
                        )
                        fig.add_artist(con)
                        print(f"  连线: Source({source_time:.2f}s, {source_freq}Hz) -> Query({query_time:.2f}s, {query_freq}Hz)")
                        break  # 找到匹配后跳出内层循环
        
        print(f"完成Source和Query之间的匹配连线绘制 (仅top 3 sessions)")


def _add_window_event_handlers(fig, *audio_players):
    """Add window event handlers"""
    # 添加窗口关闭事件处理
    def on_close(event):
        print("Window close event detected - cleaning up resources")
        for audio_player in audio_players:
            if audio_player:
                audio_player.stop()
        if plt.fignum_exists(fig.number):
            plt.close(fig)
        plt.close('all')
    
    # 添加键盘事件处理
    def on_key(event):
        if event.key == 'escape':
            print("ESC键被按下 - 关闭窗口")
            for audio_player in audio_players:
                if audio_player:
                    audio_player.stop()
            if plt.fignum_exists(fig.number):
                plt.close(fig)
    
    # 注册窗口事件
    fig.canvas.mpl_connect('close_event', on_close)
    fig.canvas.mpl_connect('key_press_event', on_key) 