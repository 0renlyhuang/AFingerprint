#!/usr/bin/env python3
import json
import matplotlib.pyplot as plt
import numpy as np
import os
import sys
import argparse
from matplotlib.patches import ConnectionPatch
import matplotlib.colors as mcolors
from matplotlib.widgets import Button, Slider
import matplotlib.gridspec as gridspec
import threading
import time
import re

# Set matplotlib options to prevent window freezing and improve window management
plt.rcParams['figure.max_open_warning'] = 10
plt.rcParams['figure.raise_window'] = True
plt.rcParams['figure.autolayout'] = True
plt.rcParams['tk.window_focus'] = True  # Helps with Tkinter focus issues

# Add audio playback support
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

def get_screen_size():
    """获取屏幕尺寸，使用单例模式管理tkinter实例"""
    global _tk_root
    try:
        import tkinter as tk
        if _tk_root is None:
            _tk_root = tk.Tk()
            _tk_root.withdraw()  # 隐藏窗口但保持实例
        
        width = _tk_root.winfo_screenwidth()
        height = _tk_root.winfo_screenheight()
        return width, height
    except Exception as e:
        print(f"获取屏幕尺寸失败: {e}")
        return 1200, 800  # 默认尺寸

def detect_and_normalize_amplitude_values(peaks_data):
    """
    检测并标准化幅度值，专门针对绝对对数刻度优化
    优化版本：提供更好的颜色对比度和数值分布
    
    Args:
        peaks_data: 峰值数据列表，每个元素为 [frequency, time, amplitude]
    
    Returns:
        dict: 包含处理后的幅度值和相关信息
    """
    if not peaks_data:
        return {
            'amplitudes': [],
            'sizes': [],
            'is_absolute_log_scale': True,
            'amplitude_range': (0, 1),
            'size_multiplier': 25,
            'amplitude_format': '.1f'
        }
    
    # 提取所有幅度值
    amplitudes = [peak[2] for peak in peaks_data]
    min_amp = min(amplitudes)
    max_amp = max(amplitudes)
    
    print(f"[幅度检测] 原始幅度值范围: [{min_amp:.4f}, {max_amp:.4f}] dB")
    
    # 专门针对绝对对数刻度进行优化
    is_absolute_log_scale = True
    
    print(f"[幅度检测] 使用绝对对数刻度优化 - 改进版本")
    
    # 改进的标准化算法 - 确保更好的颜色分布
    if max_amp > min_amp:
        # 线性标准化到 [0, 1]
        linear_normalized = [(amp - min_amp) / (max_amp - min_amp) for amp in amplitudes]
        
        # **修改**: 不使用平方根压缩，而是使用分段线性映射增强对比度
        # 使用更智能的映射策略：
        # 1. 计算四分位数来理解数据分布
        sorted_linear = sorted(linear_normalized)
        n = len(sorted_linear)
        if n >= 4:
            q1 = sorted_linear[n//4]
            q2 = sorted_linear[n//2]  # 中位数
            q3 = sorted_linear[3*n//4]
            print(f"[幅度检测] 数据分布 - Q1: {q1:.3f}, Q2: {q2:.3f}, Q3: {q3:.3f}")
        else:
            q1, q2, q3 = 0.25, 0.5, 0.75
        
        # 2. 使用分段线性映射来增强对比度
        enhanced_normalized = []
        for norm_val in linear_normalized:
            if norm_val <= q2:  # 低半部分：映射到 [0, 0.5]
                # 在低值区域给予更多的颜色空间
                enhanced_val = (norm_val / q2) * 0.5
            else:  # 高半部分：映射到 [0.5, 1.0]
                # 在高值区域也保持良好的分辨率
                enhanced_val = 0.5 + ((norm_val - q2) / (1.0 - q2)) * 0.5
            # 缩放到0-100范围
            enhanced_normalized.append(enhanced_val * 100.0)
        
        normalized_amplitudes = enhanced_normalized
        
        print(f"[幅度检测] 应用分段线性映射，提升整体颜色对比度，输出范围0-100")
    else:
        # 如果所有值相同，设为中间值
        normalized_amplitudes = [50.0] * len(amplitudes)
        print(f"[幅度检测] 所有幅度值相同，使用统一中间值50.0")
    
    # 计算散点大小 - 基于0-100范围计算
    # 基础大小为8，变化范围为42，总范围 [8, 50]
    sizes = [8 + 42 * (norm_amp / 100.0) for norm_amp in normalized_amplitudes]
    
    # 输出详细统计信息帮助调试
    print(f"[幅度检测] 标准化后范围: [{min(normalized_amplitudes):.2f}, {max(normalized_amplitudes):.2f}] (0-100)")
    print(f"[幅度检测] 标准化后统计:")
    sorted_norm = sorted(normalized_amplitudes)
    n = len(sorted_norm)
    if n >= 10:
        percentiles = [10, 25, 50, 75, 90]
        for p in percentiles:
            idx = min(int(n * p / 100), n-1)
            print(f"  {p}%分位数: {sorted_norm[idx]:.2f}")
    print(f"[幅度检测] 散点大小范围: [{min(sizes):.1f}, {max(sizes):.1f}]")
    print(f"[幅度检测] 样本数量: {len(amplitudes)}")
    
    return {
        'amplitudes': normalized_amplitudes,  # 用于颜色映射的增强标准化幅度值 (0-100)
        'original_amplitudes': amplitudes,    # 原始幅度值，用于hover显示
        'sizes': sizes,                       # 散点大小
        'is_absolute_log_scale': is_absolute_log_scale,
        'amplitude_range': (min_amp, max_amp),
        'amplitude_format': '.1f',            # 对数刻度显示1位小数
        'size_multiplier': 42
    }

def load_data(filename):
    """Load fingerprint data from JSON file"""
    with open(filename, 'r') as f:
        return json.load(f)

class AudioPlayer:
    """Handles audio file playback with visualization integration"""
    def __init__(self, audio_file=None):
        print(f"初始化音频播放器: {audio_file}")
        self.audio_file = audio_file
        self.data = None
        self.samplerate = None
        self.duration = 0
        self.playing = False
        self.current_time = 0
        self.playback_thread = None
        self.playback_line = None
        self.time_display = None
        self.status_text = None
        self.update_needed = False  # 标记是否需要更新UI
        self.last_update_time = 0   # 上次更新UI的时间
        self.has_finished = False  # 新增：标记是否播放已完成
        self.play_button = None    # 新增：存储播放按钮引用
        self.playback_position = 0   # 当前播放位置（以样本为单位）
        
        # Try to load the audio file
        success = self.load_audio()
        print(f"音频加载结果: {'成功' if success else '失败'}")
        if success:
            print(f"音频信息: 采样率={self.samplerate}Hz, 长度={self.duration:.2f}秒, 形状={self.data.shape}")
    
    def load_audio(self):
        """Load audio data from file"""
        if not AUDIO_SUPPORT or not self.audio_file or not os.path.exists(self.audio_file):
            return False
            
        try:
            # 检查文件扩展名
            _, ext = os.path.splitext(self.audio_file.lower())
            
            if ext == '.pcm':
                # 处理原始PCM文件
                print(f"Loading raw PCM file: {self.audio_file}")
                print(f"Format: {PCM_FORMAT}, {PCM_SAMPLE_RATE}Hz, {PCM_CHANNELS} channels")
                
                # 读取原始PCM数据
                with open(self.audio_file, 'rb') as f:
                    pcm_data = f.read()
                
                # 计算采样数
                total_samples = len(pcm_data) // PCM_SAMPLE_WIDTH
                print(f"PCM file size: {len(pcm_data)} bytes, sample width: {PCM_SAMPLE_WIDTH}, samples: {total_samples}")
                
                # 转换为NumPy数组
                if PCM_FORMAT == 'int16':
                    data = np.frombuffer(pcm_data, dtype=np.int16)
                elif PCM_FORMAT == 'int32':
                    data = np.frombuffer(pcm_data, dtype=np.int32)
                elif PCM_FORMAT == 'float32':
                    data = np.frombuffer(pcm_data, dtype=np.float32)
                else:
                    # 默认为int16
                    data = np.frombuffer(pcm_data, dtype=np.int16)
                
                # 处理通道数量
                if PCM_CHANNELS > 1:
                    # 如果是多通道，重塑数组
                    data = data.reshape(-1, PCM_CHANNELS)
                    
                # 设置属性
                self.data = data
                self.samplerate = PCM_SAMPLE_RATE
                self.duration = len(data) / (PCM_SAMPLE_RATE * (1 if len(data.shape) <= 1 else 1))
                print(f"PCM file loaded: duration={self.duration:.2f}s, shape={data.shape}")
            else:
                # 使用soundfile处理常规音频文件
                self.data, self.samplerate = sf.read(self.audio_file)
                self.duration = len(self.data) / self.samplerate
            
            return True
        except Exception as e:
            print(f"Error loading audio file: {e}")
            print(f"File: {self.audio_file}, Size: {os.path.getsize(self.audio_file) if os.path.exists(self.audio_file) else 'unknown'}")
            # 尝试处理为原始PCM文件
            try:
                print("Attempting to load as raw PCM file...")
                with open(self.audio_file, 'rb') as f:
                    pcm_data = f.read()
                
                # 转换为NumPy数组
                data = np.frombuffer(pcm_data, dtype=np.int16)
                
                # 设置属性
                self.data = data
                self.samplerate = PCM_SAMPLE_RATE
                self.duration = len(data) / PCM_SAMPLE_RATE
                print(f"PCM file loaded as fallback: duration={self.duration:.2f}s, samples={len(data)}")
                return True
            except Exception as e2:
                print(f"Fallback loading also failed: {e2}")
                return False
    
    def play(self, start_time=0):
        """Start audio playback from the specified time"""
        print(f"\n===== 音频播放尝试 =====")
        print(f"播放参数: start_time={start_time}")
        print(f"当前状态: playing={self.playing}, data={'有数据' if self.data is not None else '无数据'}")
        
        if not AUDIO_SUPPORT:
            print("错误: 音频支持未启用，无法播放")
            return
            
        if self.data is None:
            print("错误: 没有可播放的音频数据")
            return
            
        if self.playing:
            print("警告: 已经在播放中，请先停止")
            return
            
        # 重置播放完成标志
        self.has_finished = False
        
        # Calculate start position in samples
        start_sample = int(start_time * self.samplerate)
        if start_sample >= len(self.data):
            print(f"警告: 起始位置 {start_sample} 超出音频长度 {len(self.data)}，从头开始")
            start_sample = 0
            
        # 保存开始位置，用于之后的重新播放
        self.playback_position = start_sample
            
        # Set current time
        self.current_time = start_time
        self.playing = True
        
        # Update status
        if self.status_text:
            self.status_text.set_text("Playing")
            
        print(f"开始播放: 从 {start_sample} 样本开始")
        
        # Create a thread for playback to avoid blocking the UI
        def playback_worker():
            print(f"播放线程已启动")
            try:
                # Play audio from the starting position
                remaining_data = self.data[start_sample:]
                
                # 安全检查，确保数据有效
                if len(remaining_data) == 0:
                    print("警告: 没有足够的音频数据可播放")
                    self.playing = False
                    if self.status_text:
                        self.status_text.set_text("Error: No data")
                    return
                    
                print(f"准备播放 {len(remaining_data)} 样本 ({len(remaining_data)/self.samplerate:.2f}秒)")
                print(f"数据类型: {remaining_data.dtype}, 形状: {remaining_data.shape}")
                
                # Start a stream
                try:
                    # 确定正确的通道数
                    if len(self.data.shape) <= 1:
                        channels = PCM_CHANNELS
                        print(f"使用单声道模式播放 (PCM_CHANNELS={PCM_CHANNELS})")
                    else:
                        channels = self.data.shape[1]
                        print(f"使用多声道模式播放 (channels={channels})")
                    
                    # 尝试打开声音设备
                    print(f"尝试打开音频流: 采样率={self.samplerate}, 通道数={channels}")
                    
                    with sd.OutputStream(samplerate=self.samplerate, channels=channels) as stream:
                        print("音频流已打开")
                        # Number of samples to process at once
                        block_size = int(self.samplerate * 0.1)  # 100ms blocks
                        
                        # Process audio in blocks
                        for i in range(0, len(remaining_data), block_size):
                            if not self.playing:
                                print("播放被中断")
                                break
                                
                            # Get current block
                            end = min(i + block_size, len(remaining_data))
                            block = remaining_data[i:end]
                            
                            # 每10个块打印一次播放进度
                            if i % (block_size * 10) == 0:
                                print(f"播放进度: {i/len(remaining_data)*100:.1f}% ({(start_sample + i)/self.samplerate:.2f}秒)")
                            
                            # 如果是PCM数据，确保是可以播放的格式
                            if len(self.data.shape) <= 1:
                                # 单声道PCM数据 - 需要进行格式转换
                                if block.dtype == np.int16:
                                    # 将int16转换为float32 (-1.0 到 1.0)
                                    block = block.astype(np.float32) / 32768.0
                                elif block.dtype == np.int32:
                                    # 将int32转换为float32
                                    block = block.astype(np.float32) / 2147483648.0
                                
                                # 如果需要手动将单声道转为多声道
                                if channels > 1:
                                    print("警告: 单声道数据播放为多声道")
                                    block = np.column_stack([block] * channels)
                            else:
                                # 多通道数据
                                if block.dtype != np.float32:
                                    max_val = {
                                        np.dtype('int16'): 32768.0,
                                        np.dtype('int32'): 2147483648.0
                                    }.get(block.dtype, 1.0)
                                    block = block.astype(np.float32) / max_val
                            
                            # 安全检查数据范围
                            if np.max(np.abs(block)) > 10:
                                print(f"警告: 数据值超出正常范围 (最大值={np.max(np.abs(block))})，进行归一化")
                                block = np.clip(block, -1.0, 1.0)
                            
                            try:
                                # Write to stream
                                stream.write(block)
                                
                                # Update current time
                                self.current_time = (start_sample + i) / self.samplerate
                                
                                # 标记需要更新UI，但不直接调用matplotlib函数
                                self.update_needed = True
                                
                                # 减少更新频率，避免过多的UI更新请求
                                current_time = time.time()
                                if current_time - self.last_update_time > 0.1:  # 限制每100ms更新一次
                                    self.last_update_time = current_time
                                
                                # 不再直接调用plt.pause
                                time.sleep(0.01)  # 让出CPU时间，但不调用matplotlib
                            except Exception as block_error:
                                print(f"块播放错误: {block_error}")
                                # 继续尝试播放下一块
                        
                        print("播放完成")
                        # 标记播放已完成
                        self.has_finished = True
                        self.update_needed = True
                except Exception as stream_error:
                    print(f"音频流错误: {stream_error}")
                    import traceback
                    traceback.print_exc()
                    self.playing = False
                    if self.status_text:
                        self.status_text.set_text(f"Error: {str(stream_error)[:10]}")
            except Exception as e:
                print(f"播放线程错误: {e}")
                import traceback
                traceback.print_exc()
                self.playing = False
                if self.status_text:
                    self.status_text.set_text(f"Error: {str(e)[:10]}")
        
        # Start playback thread
        self.playback_thread = threading.Thread(target=playback_worker)
        self.playback_thread.daemon = True
        self.playback_thread.start()
        print(f"播放线程已启动: {self.playback_thread.ident}")
    
    def stop(self):
        """Stop audio playback"""
        self.playing = False
        if self.status_text:
            self.status_text.set_text("Stopped")
        # 重置完成标志，以便可以重新播放
        self.has_finished = False
    
    def seek(self, time_position):
        """Seek to a specific time position"""
        if self.data is None:
            return
            
        was_playing = self.playing
        self.stop()
        self.current_time = time_position
        
        # Update playback line
        if self.playback_line:
            self.playback_line.set_xdata([self.current_time, self.current_time])
            
        # Update time display
        if self.time_display:
            mins = int(self.current_time) // 60
            secs = int(self.current_time) % 60
            time_str = f"{mins:02}:{secs:02}"
            
            # 检查当前文本是否有前缀（如"Source: "或"Query: "）
            current_text = self.time_display.get_text()
            if "Source:" in current_text:
                self.time_display.set_text(f"Source: {time_str}")
            elif "Query:" in current_text:
                self.time_display.set_text(f"Query: {time_str}")
            else:
                # 默认情况，没有前缀
                self.time_display.set_text(time_str)
            
        # If it was playing before, restart playback
        if was_playing:
            self.play(self.current_time)
        # 如果播放完成后再次seek，重置完成标志
        self.has_finished = False

    def restart(self):
        """Restart playback from the beginning"""
        print(f"\n===== 重新开始播放 =====")
        self.stop()
        self.seek(0)  # 先移动到开头
        self.play(0)  # 从开头重新播放

    def update_ui(self):
        """更新UI元素 - 从主线程调用"""
        if not self.update_needed:
            return False
            
        try:
            # Update playback line
            if self.playback_line:
                self.playback_line.set_xdata([self.current_time, self.current_time])
                
            # Update time display
            if self.time_display:
                mins = int(self.current_time) // 60
                secs = int(self.current_time) % 60
                time_str = f"{mins:02}:{secs:02}"
                
                # 检查当前文本是否有前缀（如"Source: "或"Query: "）
                current_text = self.time_display.get_text()
                if "Source:" in current_text:
                    self.time_display.set_text(f"Source: {time_str}")
                elif "Query:" in current_text:
                    self.time_display.set_text(f"Query: {time_str}")
                else:
                    # 默认情况，没有前缀
                    self.time_display.set_text(time_str)
                
            # 检查播放是否已完成，如果完成则更新按钮状态
            if self.has_finished and self.play_button is not None:
                print("播放已完成，更新按钮状态为Play")
                # 根据按钮当前文本确定正确的标签
                current_label = self.play_button.label.get_text()
                if "Source" in current_label:
                    self.play_button.label.set_text('Play Source')
                elif "Query" in current_label:
                    self.play_button.label.set_text('Play Query')
                else:
                    self.play_button.label.set_text('Play')
                
            self.update_needed = False
            return True  # 返回True表示UI已更新
        except Exception as e:
            print(f"UI更新错误: {e}")
            return False

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
        
        # Set title and labels
        ax.set_title(f"Audio Fingerprint Extraction: {data['title']}")
        
        # Create hover function
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
            else:  # fingerprint points
                pos = scatter_obj.get_offsets()[index]
                annot.xy = pos
                point = data['fingerprintPoints'][index]
                text = f"Fingerprint\nFreq: {point[0]} Hz\nTime: {point[1]:.2f} s\nHash: {point[2]}"
            annot.set_text(text)
            annot.get_bbox_patch().set_alpha(0.9)
    
    elif plot_type == 'matching':
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
        
        # Plot matched points
        matched_scatter = None
        if 'matchedPoints' in data and data['matchedPoints']:
            matched_scatter = ax.scatter([point[1] for point in data['matchedPoints']], 
                                        [point[0] for point in data['matchedPoints']], 
                                        color='orange', s=150, alpha=0.8, marker='*', 
                                        label='Matched Points')
        
        # Set title and labels
        ax.set_title(f"Audio Fingerprint Matching: {data['title']}")
        
        # Create hover function
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
            annot.set_text(text)
            annot.get_bbox_patch().set_alpha(0.9)
    
    # Set common properties
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Frequency (Hz)')
    ax.set_ylim(0, 5000)  # Limit frequency display range
    ax.grid(True, alpha=0.3)
    ax.legend()
    
    # Add a colorbar for amplitude visualization
    amplitude_label = "Amplitude (dB)" if amplitude_info['is_absolute_log_scale'] else "Amplitude"
    cbar = fig.colorbar(peaks_scatter, ax=ax, label=amplitude_label, pad=0.01)
    cbar.set_label(amplitude_label)
    
    # 计算数据中峰值的最大时间
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
    
    # Add playback position line if audio player is provided
    if audio_player and audio_player.data is not None:
        # Add a vertical line to show playback position
        audio_player.playback_line = ax.axvline(x=0, color='green', linestyle='-', linewidth=2)
        
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
        controls = create_audio_controls_layout(fig, controls_ax, source_audio_player=audio_player if plot_type == 'extraction' else None,
                                               query_audio_player=audio_player if plot_type == 'matching' else None)
        texts = create_audio_text_layout(controls_ax, 
                                        source_audio_player=audio_player if plot_type == 'extraction' else None,
                                        query_audio_player=audio_player if plot_type == 'matching' else None)
        
        # Add playback position lines and set axis ranges
        if source_audio_player and source_audio_player.data is not None:
            source_audio_player.playback_line = ax.axvline(x=0, color='orange', linestyle='-', linewidth=2)
            # 使用数据中的最大时间和音频时长的较大值
            source_final_max_time = max(max_time_from_data, source_audio_player.duration)
            ax.set_xlim(0, source_final_max_time)
            print(f"设置源图横轴范围: 0 到 {source_final_max_time:.2f}s (数据最大时间: {max_time_from_data:.2f}s, 音频时长: {source_audio_player.duration:.2f}s)")
        else:
            # 如果没有源音频播放器，只使用数据中的最大时间
            ax.set_xlim(0, max_time_from_data)
            print(f"设置源图横轴范围: 0 到 {max_time_from_data:.2f}s (仅基于数据)")
        
        if query_audio_player and query_audio_player.data is not None:
            query_audio_player.playback_line = ax.axvline(x=0, color='green', linestyle='-', linewidth=2)
            # 使用数据中的最大时间和音频时长的较大值
            query_final_max_time = max(max_time_from_data, query_audio_player.duration)
            ax.set_xlim(0, query_final_max_time)
            print(f"设置查询图横轴范围: 0 到 {query_final_max_time:.2f}s (数据最大时间: {max_time_from_data:.2f}s, 音频时长: {query_audio_player.duration:.2f}s)")
        else:
            # 如果没有查询音频播放器，只使用数据中的最大时间
            ax.set_xlim(0, max_time_from_data)
            print(f"设置查询图横轴范围: 0 到 {max_time_from_data:.2f}s (仅基于数据)")
        
        # 设置按钮引用
        if source_audio_player and 'source' in controls:
            source_audio_player.play_button = controls['source']['play_button']
        if query_audio_player and 'query' in controls:
            query_audio_player.play_button = controls['query']['play_button']
        
        # 创建源音频控制事件处理器
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
        
        # 创建查询音频控制事件处理器
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
        
        # 每100ms更新一次UI
        from matplotlib.animation import FuncAnimation
        ani = FuncAnimation(fig, update_playback_ui, interval=100, 
                          blit=True, cache_frame_data=False)
        # 保存动画对象的引用，防止被垃圾回收
        fig.ani = ani
    else:
        # 如果没有音频播放器，根据数据设置横轴范围
        ax.set_xlim(0, max_time_from_data)
    
    # Create hover callback
    def hover(event):
        vis = annot.get_visible()
        if event.inaxes == ax:
            for scatter_obj, point_type in [(peaks_scatter, "peak"), 
                                           (fp_scatter, "fingerprint")]:
                cont, ind = scatter_obj.contains(event)
                if cont:
                    update_annot(ind, scatter_obj, point_type)
                    annot.set_visible(True)
                    fig.canvas.draw_idle()
                    return
            
            # Check matched points if available
            if 'matchedPoints' in data and data['matchedPoints']:
                if matched_scatter:
                    cont, ind = matched_scatter.contains(event)
                    if cont:
                        update_annot(ind, matched_scatter, "match")
                        annot.set_visible(True)
                        fig.canvas.draw_idle()
                        return
        
        if vis:
            annot.set_visible(False)
            fig.canvas.draw_idle()
    
    # Connect hover event
    fig.canvas.mpl_connect("motion_notify_event", hover)
    
    # 添加窗口关闭事件处理
    def on_close(event):
        print("Window close event detected - cleaning up resources")
        if audio_player:
            audio_player.stop()
        if plt.fignum_exists(fig.number):
            plt.close(fig)
        plt.close('all')
    
    # 添加键盘事件处理
    def on_key(event):
        if event.key == 'escape':
            print("ESC键被按下 - 关闭窗口")
            if audio_player:
                audio_player.stop()
            if plt.fignum_exists(fig.number):
                plt.close(fig)
    
    # 注册窗口事件
    fig.canvas.mpl_connect('close_event', on_close)
    fig.canvas.mpl_connect('key_press_event', on_key)
    
    plt.tight_layout(rect=[0, 0, 0, 0])
    
    return fig, ax

def create_audio_controls_layout(fig, controls_ax, source_audio_player=None, query_audio_player=None):
    """
    创建响应式的音频控制面板布局
    使用相对位置和自适应间距，确保元素不会重叠
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
        time_slider = Slider(slider_ax, f'{label_prefix} Time', 0, audio_player.duration, valinit=0)
        
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
            source_time_slider = Slider(source_slider_ax, 'Source Time', 0, source_audio_player.duration, valinit=0)
            
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
            query_time_slider = Slider(query_slider_ax, 'Query Time', 0, query_audio_player.duration, valinit=0)
            
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

def clean_filename_for_display(filename):
    """
    清理文件名，移除emoji和其他可能导致字体渲染问题的Unicode字符
    保留基本的ASCII字符、数字、基本标点符号和常见Unicode字符
    """
    if not filename:
        return filename
    
    # 记录原始文件名用于调试
    original_filename = filename
    
    # 常见emoji的文本替换映射
    emoji_replacements = {
        '🥴': '[dizzy]',
        '😍': '[heart_eyes]',
        '😀': '[smile]',
        '😂': '[laugh]',
        '😊': '[happy]',
        '👍': '[thumbs_up]',
        '❤️': '[heart]',
        '🔥': '[fire]',
        '💯': '[100]',
        '🎵': '[music]',
        '🎶': '[notes]',
        '🎮': '[game]',
        '🏆': '[trophy]',
        '⭐': '[star]',
        '✨': '[sparkle]',
    }
    
    # 先替换常见emoji为友好文本
    for emoji, replacement in emoji_replacements.items():
        filename = filename.replace(emoji, replacement)
    
    # 定义允许的字符范围（基本拉丁字符、数字、常见标点符号）
    # 保留中文字符范围 (0x4e00-0x9fff)
    def is_safe_char(char):
        code = ord(char)
        return (
            # 基本ASCII字符 (包括英文字母、数字、标点符号)
            (0x20 <= code <= 0x7E) or
            # 拉丁-1补充 (包括重音字符等)
            (0xA0 <= code <= 0xFF) or
            # 中文字符 (CJK统一汉字)
            (0x4E00 <= code <= 0x9FFF) or
            # 中文标点符号
            (0x3000 <= code <= 0x303F) or
            # 其他常见符号
            char in '。，、；：？！""''（）【】《》'
        )
    
    # 过滤字符并替换未知的特殊字符
    cleaned_chars = []
    i = 0
    while i < len(filename):
        char = filename[i]
        if is_safe_char(char):
            cleaned_chars.append(char)
        else:
            # 检测连续的特殊字符并替换为占位符
            emoji_start = i
            while i < len(filename) and not is_safe_char(filename[i]):
                i += 1
            # 如果跳过了字符，添加一个占位符
            if i > emoji_start:
                cleaned_chars.append('[?]')
                i -= 1  # 因为循环会自增，所以这里减1
        i += 1
    
    result = ''.join(cleaned_chars)
    
    # 清理多余的占位符和空格
    result = re.sub(r'\[?\?\]+', '[?]', result)  # 合并多个占位符
    result = re.sub(r'\s+', ' ', result)  # 合并多个空格
    result = result.strip()
    
    # 如果结果为空或只有占位符，返回一个友好的名称
    if not result or result.replace('[?]', '').strip() == '':
        result = "audio_file"
    
    # 调试信息：如果文件名被大幅改变，输出信息
    if len(original_filename) > 50 and len(result) < len(original_filename) * 0.7:
        print(f"[文件名清理] 原始: {original_filename[:50]}...")
        print(f"[文件名清理] 清理后: {result}")
    
    return result

def main():
    global PCM_SAMPLE_RATE, PCM_CHANNELS, PCM_FORMAT
    
    parser = argparse.ArgumentParser(description='Visualize audio fingerprints with audio playback')
    parser.add_argument('--source', type=str, help='Source audio fingerprint JSON file')
    parser.add_argument('--query', type=str, help='Query audio fingerprint JSON file')
    parser.add_argument('--output', type=str, help='Output image file')
    parser.add_argument('--sessions', type=str, help='JSON file containing top sessions data')
    parser.add_argument('--source-audio', type=str, help='Source audio file (WAV/MP3/PCM) for playback (overrides path in JSON)')
    parser.add_argument('--query-audio', type=str, help='Query audio file (WAV/MP3/PCM) for playback (overrides path in JSON)')
    # PCM格式参数
    parser.add_argument('--pcm-rate', type=int, default=PCM_SAMPLE_RATE, help='PCM sample rate (default: 44100)')
    parser.add_argument('--pcm-channels', type=int, default=PCM_CHANNELS, help='PCM channels (default: 1)')
    parser.add_argument('--pcm-format', type=str, default=PCM_FORMAT, help='PCM format (default: int16)')
    # 诊断参数
    parser.add_argument('--debug-comparison', action='store_true', help='Run comparison visualization in debug mode')
    parser.add_argument('--force-backend', type=str, help='Force specific matplotlib backend (e.g., TkAgg, Qt5Agg)')
    args = parser.parse_args()
    
    # 强制切换后端（如果指定）
    if args.force_backend:
        try:
            print(f"尝试强制切换Matplotlib后端到: {args.force_backend}")
            plt.switch_backend(args.force_backend)
            print(f"后端切换成功，当前后端: {plt.get_backend()}")
        except Exception as e:
            print(f"警告: 无法切换到指定后端: {e}")
    
    # 设置PCM格式参数
    PCM_SAMPLE_RATE = args.pcm_rate
    PCM_CHANNELS = args.pcm_channels
    PCM_FORMAT = args.pcm_format
    
    # Check for audio playback capability
    if (args.source_audio or args.query_audio) and not AUDIO_SUPPORT:
        print("Warning: Audio playback requested but not available.")
        print("Please install required packages: pip install soundfile sounddevice")
    
    # If only source is provided, create extraction plot
    if args.source and not args.query:
        data = load_data(args.source)
        
        # Create audio player if audio file provided
        audio_player = None
        
        # Check for audio file from command line first, then from JSON
        audio_file_path = args.source_audio
        if not audio_file_path and 'audioFilePath' in data and os.path.exists(data['audioFilePath']):
            audio_file_path = data['audioFilePath']
            print(f"Using audio file from JSON: {audio_file_path}")
            
        if audio_file_path and AUDIO_SUPPORT:
            audio_player = AudioPlayer(audio_file_path)
            
        fig, ax = create_interactive_plot(data, 'extraction', audio_player)
        
        # Save to file if output is specified
        if args.output:
            fig.savefig(args.output)
            print(f"Saved extraction plot to {args.output}")
        else:
            plt.show()
    
    # If only query is provided, create matching plot
    elif args.query and not args.source:
        data = load_data(args.query)
        
        # Create audio player if audio file provided
        audio_player = None
        
        # Check for audio file from command line first, then from JSON
        audio_file_path = args.query_audio
        if not audio_file_path and 'audioFilePath' in data and os.path.exists(data['audioFilePath']):
            audio_file_path = data['audioFilePath']
            print(f"Using audio file from JSON: {audio_file_path}")
            
        if audio_file_path and AUDIO_SUPPORT:
            print(f"\n===== 创建音频播放器 =====")
            print(f"音频文件: {audio_file_path}")
            audio_player = AudioPlayer(audio_file_path)
            if audio_player.data is None:
                print("警告: 无法加载音频数据，禁用音频播放")
                audio_player = None
            else:
                print(f"音频播放器创建成功: 长度 {audio_player.duration:.2f}秒")
                
        print(f"\n===== 创建可视化图表 =====")
        print(f"图表类型: {'matching' if 'matchedPoints' in data else 'extraction'}")
        print(f"音频播放: {'启用' if audio_player else '禁用'}")
        fig, ax = create_interactive_plot(data, 'matching', audio_player)
        
        # Save to file if output is specified
        if args.output:
            fig.savefig(args.output)
            print(f"Saved matching plot to {args.output}")
        else:
            print(f"\n===== 显示图形 =====")
            plt.show()
            print("图形已关闭")
    
    # If both source and query are provided, create comparison plot
    elif args.source and args.query:
        print(f"\n===== 加载比较可视化数据文件 =====")
        print(f"源数据文件: {args.source}")
        print(f"查询数据文件: {args.query}")
        print(f"会话数据文件: {args.sessions if args.sessions else '无'}")
        print(f"源音频文件: {args.source_audio if args.source_audio else '无'}")
        print(f"查询音频文件: {args.query_audio if args.query_audio else '无'}")
        
        try:
            source_data = load_data(args.source)
            print(f"源数据加载成功: {len(source_data.get('fingerprintPoints', []))} 个指纹点")
        except Exception as e:
            print(f"错误: 加载源数据文件失败: {e}")
            import traceback
            traceback.print_exc()
            sys.exit(1)
            
        try:
            query_data = load_data(args.query)
            print(f"查询数据加载成功: {len(query_data.get('fingerprintPoints', []))} 个指纹点")
        except Exception as e:
            print(f"错误: 加载查询数据文件失败: {e}")
            import traceback
            traceback.print_exc()
            sys.exit(1)
        
        # If top sessions file is provided, load it
        top_sessions = None
        if args.sessions:
            try:
                with open(args.sessions, 'r') as f:
                    top_sessions = json.load(f)
                print(f"会话数据加载成功: {len(top_sessions)} 个会话")
            except Exception as e:
                print(f"警告: 加载会话数据文件失败: {e}")
                import traceback
                traceback.print_exc()
        
        # 创建源音频播放器
        source_audio_player = None
        source_audio_file_path = args.source_audio
        if not source_audio_file_path and 'audioFilePath' in source_data and os.path.exists(source_data['audioFilePath']):
            source_audio_file_path = source_data['audioFilePath']
            print(f"使用源数据JSON中的音频文件路径: {source_audio_file_path}")
            
        if source_audio_file_path and AUDIO_SUPPORT:
            print(f"\n===== 创建源音频播放器 =====")
            print(f"音频文件: {source_audio_file_path}")
            source_audio_player = AudioPlayer(source_audio_file_path)
            if source_audio_player.data is None:
                print("警告: 无法加载源音频数据，禁用源音频播放")
                source_audio_player = None
            else:
                print(f"源音频播放器创建成功: 长度 {source_audio_player.duration:.2f}秒")
        
        # 创建查询音频播放器
        query_audio_player = None
        query_audio_file_path = args.query_audio
        if not query_audio_file_path and 'audioFilePath' in query_data and os.path.exists(query_data['audioFilePath']):
            query_audio_file_path = query_data['audioFilePath']
            print(f"使用查询数据JSON中的音频文件路径: {query_audio_file_path}")
            
        if query_audio_file_path and AUDIO_SUPPORT:
            print(f"\n===== 创建查询音频播放器 =====")
            print(f"音频文件: {query_audio_file_path}")
            query_audio_player = AudioPlayer(query_audio_file_path)
            if query_audio_player.data is None:
                print("警告: 无法加载查询音频数据，禁用查询音频播放")
                query_audio_player = None
            else:
                print(f"查询音频播放器创建成功: 长度 {query_audio_player.duration:.2f}秒")
        else:
            if not AUDIO_SUPPORT:
                print("警告: 音频播放功能未启用，请安装 soundfile 和 sounddevice 包")
            elif not query_audio_file_path:
                print("注意: 未提供查询音频文件路径，比较可视化将不包含查询音频播放功能")
        
        # Check if we have audio files in the JSON data for future reference
        if 'audioFilePath' in source_data:
            print(f"源数据音频文件路径: {source_data['audioFilePath']}")
        if 'audioFilePath' in query_data:
            print(f"查询数据音频文件路径: {query_data['audioFilePath']}")
            
        try:
            print("\n===== 创建比较可视化 =====")
            fig, (ax1, ax2) = create_comparison_plot(source_data, query_data, top_sessions, source_audio_player, query_audio_player)
            print("比较可视化创建成功")
            
            # Save to file if output is specified
            if args.output:
                fig.savefig(args.output)
                print(f"已保存比较可视化图形到 {args.output}")
            else:
                print(f"\n===== 显示比较可视化图形 =====")
                # 检查是否存在有效的后端
                backend = plt.get_backend()
                print(f"当前Matplotlib后端: {backend}")
                
                # 输出matplotlib信息
                print(f"Matplotlib配置信息:")
                print(f"- 是否支持交互: {plt.isinteractive()}")
                print(f"- rcParams: {','.join([f'{k}={v}' for k,v in plt.rcParams.items() if k in ['backend', 'interactive']])}")
                
                try:
                    # 强制使用阻塞模式确保图形显示
                    plt.show(block=True)
                    print("图形显示完成")
                except Exception as e:
                    print(f"错误: 无法显示图形: {e}")
                    import traceback
                    traceback.print_exc()
                    
                    # 尝试备用方式显示
                    print("尝试备用方式显示图形...")
                    try:
                        # 使用命令行环境下更可靠的设置
                        plt.switch_backend('TkAgg')  # 尝试切换到更可靠的后端
                        print(f"切换到后端: TkAgg")
                        plt.figure(fig.number)  # 确保使用同一图形
                        plt.show(block=True)
                        print("备用方式显示成功")
                    except Exception as e2:
                        print(f"备用方式也失败: {e2}")
                        traceback.print_exc()
        except Exception as e:
            print(f"错误: 创建比较可视化失败: {e}")
            import traceback
            traceback.print_exc()
            sys.exit(1)
    
    else:
        print("错误: 必须至少指定 --source 或 --query 参数。")
        parser.print_help()
        sys.exit(1)

    # 在显示图形之前设置后端选项（已移至文件开头，这里保留为了兼容性）
    # 使用更安全的显示图形方法
    if args.output:
        fig.savefig(args.output)
        print(f"Saved plot to {args.output}")
    else:
        try:
            # 使用不阻塞的方式显示，并确保事件循环正确处理
            plt.show()
        except Exception as e:
            print(f"Error displaying plot: {e}")
            # 尝试不同的显示方法作为备选
            try:
                plt.show(block=True)
            except Exception as e2:
                print(f"Alternative display method also failed: {e2}")

    # 如果启用了比较可视化调试模式
    if args.debug_comparison:
        print("\n===== 比较可视化调试模式 =====")
        # 设置默认测试文件
        test_source = args.source or "./build/Debug/comparison_v_s_35_vs_source_source.json"
        test_query = args.query or "./build/Debug/comparison_v_s_35_vs_source_query.json"
        test_sessions = args.sessions or "./build/Debug/comparison_v_s_35_vs_source_sessions.json"
        
        print(f"使用测试文件:")
        print(f"- 源数据: {test_source}")
        print(f"- 查询数据: {test_query}")
        print(f"- 会话数据: {test_sessions}")
        
        # 检查文件是否存在
        for f, name in [(test_source, "源数据"), (test_query, "查询数据"), (test_sessions, "会话数据")]:
            if os.path.exists(f):
                print(f"✓ {name}文件存在")
            else:
                print(f"✗ {name}文件不存在: {f}")
                if name != "会话数据":  # 会话数据是可选的
                    print("错误: 必需的测试文件不存在，退出调试模式")
                    sys.exit(1)
        
        # 加载数据
        try:
            source_data = load_data(test_source)
            query_data = load_data(test_query)
            top_sessions = None
            if os.path.exists(test_sessions):
                with open(test_sessions, 'r') as f:
                    top_sessions = json.load(f)
            
            print("数据加载成功，准备创建比较可视化...")
            
            # 测试环境信息
            print("\n环境信息:")
            print(f"Python版本: {sys.version}")
            print(f"Matplotlib版本: {plt.__version__}")
            print(f"Numpy版本: {np.__version__}")
            print(f"操作系统: {os.name}, {sys.platform}")
            print(f"当前工作目录: {os.getcwd()}")
            print(f"Matplotlib后端: {plt.get_backend()}")
            
            # 创建比较图并显示
            fig, (ax1, ax2) = create_comparison_plot(source_data, query_data, top_sessions)
            plt.tight_layout()
            print("比较图创建成功，尝试显示...")
            
            # 确保图形显示
            try:
                plt.show(block=True)
                print("图形显示成功")
            except Exception as e:
                print(f"图形显示失败: {e}")
                import traceback
                traceback.print_exc()
                
                # 尝试备用方法
                print("尝试备用显示方法...")
                for backend in ['TkAgg', 'Qt5Agg', 'WXAgg', 'Agg']:
                    try:
                        print(f"尝试使用 {backend} 后端...")
                        plt.switch_backend(backend)
                        plt.figure(fig.number)
                        plt.show(block=True)
                        print(f"{backend} 后端显示成功")
                        break
                    except Exception as be:
                        print(f"{backend} 后端失败: {be}")
                
            print("比较可视化调试完成")
            return
        except Exception as e:
            print(f"比较可视化调试失败: {e}")
            import traceback
            traceback.print_exc()
            sys.exit(1)
            
    # Check for audio playback capability
    # ... existing code ...

def clean_up():
    """清理资源"""
    global _tk_root, _current_audio_player
    # 停止正在播放的音频
    if _current_audio_player is not None:
        try:
            _current_audio_player.stop()
            _current_audio_player = None
        except:
            pass
            
    # 关闭所有matplotlib图形
    try:
        plt.close('all')
    except:
        pass
        
    # 关闭tkinter实例
    if _tk_root is not None:
        try:
            _tk_root.destroy()
            _tk_root = None
        except:
            pass

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
    
    # Plot source data
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
    
    # Source matched points
    source_matched_scatter = None
    if 'matchedPoints' in source_data and source_data['matchedPoints']:
        source_matched_scatter = ax1.scatter([point[1] for point in source_data['matchedPoints']], 
                                           [point[0] for point in source_data['matchedPoints']], 
                                           color='red', s=150, alpha=1.0, marker='*', 
                                           edgecolors='black', linewidth=2,
                                           label='Source Matches')
    
    ax1.set_title(f"Source: {source_data['title']}")
    ax1.set_xlabel('Time (s)')
    ax1.set_ylabel('Frequency (Hz)')
    ax1.set_ylim(0, 5000)
    ax1.grid(True, alpha=0.3)
    ax1.legend()
    
    # Add colorbar for source
    source_amplitude_label = "Amplitude (dB)" if source_amplitude_info['is_absolute_log_scale'] else "Amplitude"
    cbar1 = fig.colorbar(source_peaks_scatter, ax=ax1, label=source_amplitude_label, pad=0.01)
    
    # Plot query data
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
    
    # Query matched points
    query_matched_scatter = None
    if 'matchedPoints' in query_data and query_data['matchedPoints']:
        query_matched_scatter = ax2.scatter([point[1] for point in query_data['matchedPoints']], 
                                          [point[0] for point in query_data['matchedPoints']], 
                                          color='red', s=150, alpha=1.0, marker='*', 
                                          edgecolors='black', linewidth=2,
                                          label='Query Matches')
    
    ax2.set_title(f"Query: {query_data['title']}")
    ax2.set_xlabel('Time (s)')
    ax2.set_ylabel('Frequency (Hz)')
    ax2.set_ylim(0, 5000)
    ax2.grid(True, alpha=0.3)
    ax2.legend()
    
    # Add colorbar for query
    query_amplitude_label = "Amplitude (dB)" if query_amplitude_info['is_absolute_log_scale'] else "Amplitude"
    cbar2 = fig.colorbar(query_peaks_scatter, ax=ax2, label=query_amplitude_label, pad=0.01)
    
    # Add connection lines between matched points
    connection_lines = []
    if (source_matched_scatter and query_matched_scatter and 
        'matchedPoints' in source_data and source_data['matchedPoints'] and
        'matchedPoints' in query_data and query_data['matchedPoints']):
        
        print(f"添加匹配点连接线...")
        print(f"源匹配点数量: {len(source_data['matchedPoints'])}")
        print(f"查询匹配点数量: {len(query_data['matchedPoints'])}")
        
        # Create a dictionary to match points by hash
        source_matches_by_hash = {}
        for i, point in enumerate(source_data['matchedPoints']):
            if len(point) >= 3:  # [frequency, time, hash, ...]
                hash_val = point[2]
                source_matches_by_hash[hash_val] = (i, point)
        
        query_matches_by_hash = {}
        for i, point in enumerate(query_data['matchedPoints']):
            if len(point) >= 3:  # [frequency, time, hash, ...]
                hash_val = point[2]
                query_matches_by_hash[hash_val] = (i, point)
        
        # Find common hashes and draw connection lines
        common_hashes = set(source_matches_by_hash.keys()) & set(query_matches_by_hash.keys())
        print(f"共同的hash数量: {len(common_hashes)}")
        
        for hash_val in common_hashes:
            source_idx, source_point = source_matches_by_hash[hash_val]
            query_idx, query_point = query_matches_by_hash[hash_val]
            
            # Source point coordinates (time, frequency)
            source_x, source_y = source_point[1], source_point[0]
            # Query point coordinates (time, frequency)
            query_x, query_y = query_point[1], query_point[0]
            
            # Create connection line between the two subplots
            # ConnectionPatch connects two points in different axes
            connection = ConnectionPatch(
                xyA=(source_x, source_y), coordsA=ax1.transData,
                xyB=(query_x, query_y), coordsB=ax2.transData,
                arrowstyle='-', 
                color='lightblue', 
                alpha=0.6, 
                linewidth=1.5,
                linestyle='--'
            )
            fig.add_artist(connection)
            connection_lines.append(connection)
        
        print(f"已添加 {len(connection_lines)} 条连接线")
        
        # Add a legend entry for connection lines if any were created
        if connection_lines:
            # Add a dummy line to the legend with match count
            ax1.plot([], [], '--', color='lightblue', alpha=0.6, linewidth=1.5, 
                    label=f'Match Connections ({len(connection_lines)})')
            ax1.legend()
    
    # Add hover functionality
    def create_hover_annotations():
        # Source annotation
        source_annot = ax1.annotate("", xy=(0, 0), xytext=(20, 20),
                                   textcoords="offset points",
                                   bbox=dict(boxstyle="round", fc="w"),
                                   arrowprops=dict(arrowstyle="->"))
        source_annot.set_visible(False)
        
        # Query annotation
        query_annot = ax2.annotate("", xy=(0, 0), xytext=(20, 20),
                                  textcoords="offset points",
                                  bbox=dict(boxstyle="round", fc="w"),
                                  arrowprops=dict(arrowstyle="->"))
        query_annot.set_visible(False)
        
        def count_nearby_matches(target_point, matched_points, tolerance=0.01):
            """
            计算在目标点附近的匹配点数量
            tolerance: 位置容差（秒和Hz），用于判断点是否在相同位置
            """
            if not matched_points:
                return 0
                
            target_freq, target_time = target_point[0], target_point[1]
            count = 0
            
            for point in matched_points:
                point_freq, point_time = point[0], point[1]
                # 计算时间和频率的差距
                time_diff = abs(point_time - target_time)
                freq_diff = abs(point_freq - target_freq)
                
                # 如果在容差范围内，认为是在相同位置
                if time_diff <= tolerance and freq_diff <= (tolerance * 100):  # 频率容差更大
                    count += 1
                    
            return count
        
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
            elif source_matched_scatter and scatter_obj == source_matched_scatter:
                pos = scatter_obj.get_offsets()[index]
                source_annot.xy = pos
                point = source_data['matchedPoints'][index]
                
                # 计算在相同位置的匹配点数量
                matches_at_location = count_nearby_matches(point, source_data['matchedPoints'])
                
                text = f"Source Match\nFreq: {point[0]} Hz\nTime: {point[1]:.2f} s\nHash: {point[2]}\nSession: {point[3] if len(point) > 3 else 'N/A'}\nMatches at location: {matches_at_location}"
                
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
            elif query_matched_scatter and scatter_obj == query_matched_scatter:
                pos = scatter_obj.get_offsets()[index]
                query_annot.xy = pos
                point = query_data['matchedPoints'][index]
                
                # 计算在相同位置的匹配点数量
                matches_at_location = count_nearby_matches(point, query_data['matchedPoints'])
                
                text = f"Query Match\nFreq: {point[0]} Hz\nTime: {point[1]:.2f} s\nHash: {point[2]}\nSession: {point[3] if len(point) > 3 else 'N/A'}\nMatches at location: {matches_at_location}"
                
            query_annot.set_text(text)
            query_annot.get_bbox_patch().set_alpha(0.9)
        
        def hover(event):
            source_vis = source_annot.get_visible()
            query_vis = query_annot.get_visible()
            
            if event.inaxes == ax1:
                # Check source scatters
                for scatter_obj, point_type in [(source_peaks_scatter, "peak"), 
                                               (source_fp_scatter, "fingerprint")]:
                    cont, ind = scatter_obj.contains(event)
                    if cont:
                        update_source_annot(ind, scatter_obj, point_type)
                        source_annot.set_visible(True)
                        fig.canvas.draw_idle()
                        return
                
                # Check source matched points
                if source_matched_scatter:
                    cont, ind = source_matched_scatter.contains(event)
                    if cont:
                        update_source_annot(ind, source_matched_scatter, "match")
                        source_annot.set_visible(True)
                        fig.canvas.draw_idle()
                        return
                
                if source_vis:
                    source_annot.set_visible(False)
                    fig.canvas.draw_idle()
                    
            elif event.inaxes == ax2:
                # Check query scatters
                for scatter_obj, point_type in [(query_peaks_scatter, "peak"), 
                                               (query_fp_scatter, "fingerprint")]:
                    cont, ind = scatter_obj.contains(event)
                    if cont:
                        update_query_annot(ind, scatter_obj, point_type)
                        query_annot.set_visible(True)
                        fig.canvas.draw_idle()
                        return
                
                # Check query matched points
                if query_matched_scatter:
                    cont, ind = query_matched_scatter.contains(event)
                    if cont:
                        update_query_annot(ind, query_matched_scatter, "match")
                        query_annot.set_visible(True)
                        fig.canvas.draw_idle()
                        return
                
                if query_vis:
                    query_annot.set_visible(False)
                    fig.canvas.draw_idle()
        
        return hover
    
    # Setup hover functionality
    hover_func = create_hover_annotations()
    fig.canvas.mpl_connect("motion_notify_event", hover_func)
    
    # 计算源数据和查询数据中峰值的最大时间
    source_max_time = 0
    if source_data['allPeaks']:
        source_max_time = max(peak[1] for peak in source_data['allPeaks'])
    if source_data['fingerprintPoints']:
        source_fp_max_time = max(point[1] for point in source_data['fingerprintPoints'])
        source_max_time = max(source_max_time, source_fp_max_time)
    if 'matchedPoints' in source_data and source_data['matchedPoints']:
        source_matched_max_time = max(point[1] for point in source_data['matchedPoints'])
        source_max_time = max(source_max_time, source_matched_max_time)
    
    query_max_time = 0
    if query_data['allPeaks']:
        query_max_time = max(peak[1] for peak in query_data['allPeaks'])
    if query_data['fingerprintPoints']:
        query_fp_max_time = max(point[1] for point in query_data['fingerprintPoints'])
        query_max_time = max(query_max_time, query_fp_max_time)
    if 'matchedPoints' in query_data and query_data['matchedPoints']:
        query_matched_max_time = max(point[1] for point in query_data['matchedPoints'])
        query_max_time = max(query_max_time, query_matched_max_time)
    
    # 添加一点边距
    if source_max_time > 0:
        source_max_time += source_max_time * 0.05  # 增加5%的边距
    else:
        source_max_time = 10  # 默认值
        
    if query_max_time > 0:
        query_max_time += query_max_time * 0.05  # 增加5%的边距
    else:
        query_max_time = 10  # 默认值
        
    print(f"源数据中的最大时间: {source_max_time:.2f}s")
    print(f"查询数据中的最大时间: {query_max_time:.2f}s")

    def on_key_comparison(event):
        """比较模式下的键盘事件处理"""
        if event.key == 'escape':
            print("ESC键被按下 - 关闭窗口")
            plt.close(fig)
        elif event.key == 'r':
            print("R键被按下 - 重置视图")
            ax1.relim()
            ax1.autoscale_view()
            ax2.relim()
            ax2.autoscale_view()
            fig.canvas.draw_idle()

    # 添加键盘事件处理
    cid_key = fig.canvas.mpl_connect('key_press_event', on_key_comparison)
    
    # 如果有音频播放器，添加音频控制
    if has_any_audio:
        print("\n===== 添加音频播放控制 =====")
        
        # Add audio control panel at the bottom
        controls_ax = fig.add_subplot(grid[2])
        controls_ax.set_facecolor('lightgray')
        controls_ax.set_xticks([])
        controls_ax.set_yticks([])
        
        # 使用新的相对布局系统创建音频控件
        controls = create_audio_controls_layout(fig, controls_ax, source_audio_player, query_audio_player)
        texts = create_audio_text_layout(controls_ax, source_audio_player, query_audio_player)
        
        # Add playback position lines and set axis ranges
        if source_audio_player and source_audio_player.data is not None:
            source_audio_player.playback_line = ax1.axvline(x=0, color='orange', linestyle='-', linewidth=2)
            # 使用数据中的最大时间和音频时长的较大值
            source_final_max_time = max(source_max_time, source_audio_player.duration)
            ax1.set_xlim(0, source_final_max_time)
            print(f"设置源图横轴范围: 0 到 {source_final_max_time:.2f}s (数据最大时间: {source_max_time:.2f}s, 音频时长: {source_audio_player.duration:.2f}s)")
        else:
            # 如果没有源音频播放器，只使用数据中的最大时间
            ax1.set_xlim(0, source_max_time)
            print(f"设置源图横轴范围: 0 到 {source_max_time:.2f}s (仅基于数据)")
        
        if query_audio_player and query_audio_player.data is not None:
            query_audio_player.playback_line = ax2.axvline(x=0, color='green', linestyle='-', linewidth=2)
            # 使用数据中的最大时间和音频时长的较大值
            query_final_max_time = max(query_max_time, query_audio_player.duration)
            ax2.set_xlim(0, query_final_max_time)
            print(f"设置查询图横轴范围: 0 到 {query_final_max_time:.2f}s (数据最大时间: {query_max_time:.2f}s, 音频时长: {query_audio_player.duration:.2f}s)")
        else:
            # 如果没有查询音频播放器，只使用数据中的最大时间
            ax2.set_xlim(0, query_max_time)
            print(f"设置查询图横轴范围: 0 到 {query_max_time:.2f}s (仅基于数据)")
        
        # 设置按钮引用
        if source_audio_player and 'source' in controls:
            source_audio_player.play_button = controls['source']['play_button']
        if query_audio_player and 'query' in controls:
            query_audio_player.play_button = controls['query']['play_button']
        
        # 创建源音频控制事件处理器
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
        
        # 创建查询音频控制事件处理器
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
        
        # 每100ms更新一次UI
        from matplotlib.animation import FuncAnimation
        ani = FuncAnimation(fig, update_playback_ui, interval=100, 
                          blit=True, cache_frame_data=False)
        # 保存动画对象的引用，防止被垃圾回收
        fig.ani = ani
    else:
        # 如果没有任何音频播放器，根据数据设置横轴范围
        print("\n===== 没有音频播放器，根据数据设置横轴范围 =====")
        ax1.set_xlim(0, source_max_time)
        ax2.set_xlim(0, query_max_time)
        print(f"设置源图横轴范围: 0 到 {source_max_time:.2f}s")
        print(f"设置查询图横轴范围: 0 到 {query_max_time:.2f}s")
    
    # 添加窗口关闭事件处理
    def on_close(event):
        print("Window close event detected - cleaning up resources")
        if source_audio_player:
            source_audio_player.stop()
        if query_audio_player:
            query_audio_player.stop()
        if plt.fignum_exists(fig.number):
            plt.close(fig)
        plt.close('all')
    
    fig.canvas.mpl_connect('close_event', on_close)
    
    plt.tight_layout(rect=[0, 0.15, 1, 1])  # 减少底部边距，从0.226改为0.15
    
    return fig, (ax1, ax2)

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"Fatal error: {e}")
        import traceback
        traceback.print_exc()
    finally:
        # 确保在程序退出时释放资源
        clean_up() 

