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
            self.time_display.set_text(f"{mins:02}:{secs:02}")
            
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
                self.time_display.set_text(f"{mins:02}:{secs:02}")
                
            # 检查播放是否已完成，如果完成则更新按钮状态
            if self.has_finished and self.play_button is not None:
                print("播放已完成，更新按钮状态为Play")
                self.play_button.label.set_text('Play')
                self.play_button.ax.figure.canvas.draw_idle()
                self.playing = False  # 确保播放状态正确设置
                self.has_finished = False  # 重置完成标志，避免重复更新
                
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
        # Plot all peaks
        peaks_scatter = ax.scatter([peak[1] for peak in data['allPeaks']], 
                                  [peak[0] for peak in data['allPeaks']], 
                                  c=[peak[2] for peak in data['allPeaks']], 
                                  cmap='viridis', alpha=0.7, 
                                  s=[10 + 40 * peak[2] for peak in data['allPeaks']], 
                                  label='All Peaks')
        
        # Plot fingerprint points
        fp_scatter = ax.scatter([point[1] for point in data['fingerprintPoints']], 
                               [point[0] for point in data['fingerprintPoints']], 
                               color='red', s=12, label='Fingerprint Points')
        
        # Set title and labels
        ax.set_title(f"Audio Fingerprint Extraction: {data['title']}")
        
        # Create hover function
        def update_annot(ind, scatter_obj, point_type):
            index = ind["ind"][0]
            if scatter_obj == peaks_scatter:
                pos = scatter_obj.get_offsets()[index]
                annot.xy = pos
                text = f"Peak\nFreq: {data['allPeaks'][index][0]} Hz\nTime: {data['allPeaks'][index][1]:.2f} s\nAmplitude: {data['allPeaks'][index][2]:.4f}"
            else:  # fingerprint points
                pos = scatter_obj.get_offsets()[index]
                annot.xy = pos
                point = data['fingerprintPoints'][index]
                text = f"Fingerprint\nFreq: {point[0]} Hz\nTime: {point[1]:.2f} s\nHash: {point[2]}"
            annot.set_text(text)
            annot.get_bbox_patch().set_alpha(0.9)
    
    elif plot_type == 'matching':
        # Plot all peaks
        peaks_scatter = ax.scatter([peak[1] for peak in data['allPeaks']], 
                                  [peak[0] for peak in data['allPeaks']], 
                                  c=[peak[2] for peak in data['allPeaks']], 
                                  cmap='viridis', alpha=0.7,
                                  s=[10 + 40 * peak[2] for peak in data['allPeaks']],
                                  label='All Peaks')
        
        # Plot fingerprint points
        fp_scatter = ax.scatter([point[1] for point in data['fingerprintPoints']], 
                               [point[0] for point in data['fingerprintPoints']], 
                               color='red', s=12, label='Fingerprint Points')
        
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
                text = f"Peak\nFreq: {data['allPeaks'][index][0]} Hz\nTime: {data['allPeaks'][index][1]:.2f} s\nAmplitude: {data['allPeaks'][index][2]:.4f}"
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
    cbar = fig.colorbar(peaks_scatter, ax=ax, label='Amplitude', pad=0.01)
    cbar.set_label('Amplitude')
    
    # Add playback position line if audio player is provided
    if audio_player and audio_player.data is not None:
        # Add a vertical line to show playback position
        audio_player.playback_line = ax.axvline(x=0, color='green', linestyle='-', linewidth=2)
        
        # Set x-axis limit to audio duration
        ax.set_xlim(0, audio_player.duration)
        
        # Create audio control panel
        controls_ax = fig.add_subplot(grid[1])
        controls_ax.set_facecolor('lightgray')
        controls_ax.set_xticks([])
        controls_ax.set_yticks([])
        
        # Create play button - 使按钮更大，更容易点击
        play_ax = plt.axes([0.2, 0.07, 0.15, 0.06])
        play_button = Button(play_ax, 'Play', color='lightgreen', hovercolor='green')
        
        # Create stop button
        stop_ax = plt.axes([0.36, 0.07, 0.15, 0.06])
        stop_button = Button(stop_ax, 'Stop', color='lightcoral', hovercolor='red')
        
        # Create time slider - 让滑块更长，更容易操作
        slider_ax = plt.axes([0.2, 0.15, 0.6, 0.05])
        time_slider = Slider(slider_ax, 'Time', 0, audio_player.duration, valinit=0)
        
        # Create time display
        time_text = controls_ax.text(0.85, 0.5, "00:00", fontsize=14)
        audio_player.time_display = time_text
        
        # Create status display
        status_text = controls_ax.text(0.5, 0.5, "Ready", fontsize=14, weight='bold')
        audio_player.status_text = status_text
        
        # Add file info with more details
        duration_mins = int(audio_player.duration) // 60
        duration_secs = int(audio_player.duration) % 60
        file_info = f"Audio: {os.path.basename(audio_player.audio_file)}\nDuration: {duration_mins:02}:{duration_secs:02}"
        file_text = controls_ax.text(0.05, 0.8, file_info, fontsize=12)
        
        # 保存按钮引用到音频播放器中，用于状态更新
        audio_player.play_button = play_button
        
        # Connect button events
        def on_play(event):
            print(f"\n===== 播放按钮被点击 =====")
            print(f"音频播放器状态: playing={audio_player.playing}, has_finished={audio_player.has_finished}")
            
            if audio_player.playing:
                print("停止播放")
                audio_player.stop()
                play_button.label.set_text('Play')
            else:
                # 检查是否播放已经完成过一次
                if audio_player.current_time >= audio_player.duration - 0.1:
                    print("检测到播放已到达结尾，从头开始播放")
                    audio_player.restart()  # 从头重新开始播放
                else:
                    print(f"从当前位置继续播放: {audio_player.current_time:.2f}秒")
                    audio_player.play(audio_player.current_time)
                
                play_button.label.set_text('Pause')
                
            print("按钮事件处理完成")
            # 强制重绘按钮
            play_button.ax.figure.canvas.draw_idle()
        
        def on_stop(event):
            print(f"\n===== 停止按钮被点击 =====")
            audio_player.stop()
            play_button.label.set_text('Play')
            # 强制重绘按钮
            play_button.ax.figure.canvas.draw_idle()
            print("停止播放")
        
        def on_slider_changed(val):
            print(f"\n===== 滑块被调整 =====")
            print(f"滑块值: {val:.2f}")
            audio_player.seek(val)
        
        print(f"\n正在连接音频控制按钮事件处理器...")
        play_button.on_clicked(on_play)
        stop_button.on_clicked(on_stop)
        time_slider.on_changed(on_slider_changed)
        print(f"按钮事件处理器已连接")
        
        # Add click handler to seek in the main plot
        def on_plot_click(event):
            if event.inaxes == ax:
                # Get x position (time)
                time_pos = event.xdata
                if time_pos < 0:
                    time_pos = 0
                elif time_pos > audio_player.duration:
                    time_pos = audio_player.duration
                    
                # Seek to that position
                audio_player.seek(time_pos)
                time_slider.set_val(time_pos)
        
        fig.canvas.mpl_connect('button_press_event', on_plot_click)
    
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
            plt.close(fig)  # 关闭当前图形
        plt.close('all')  # 保证关闭所有图形
    
    # 注册窗口关闭事件
    fig.canvas.mpl_connect('close_event', on_close)
    
    # 添加键盘事件处理 - 增强的版本
    def on_key(event):
        if event.key == 'escape':
            print("ESC键被按下 - 关闭窗口")
            if audio_player:
                audio_player.stop()
            if plt.fignum_exists(fig.number):
                plt.close(fig)  # 关闭当前图形
    
    # 注册键盘事件
    fig.canvas.mpl_connect('key_press_event', on_key)
    
    # 尝试设置窗口位置（居中）
    try:
        print("尝试居中显示窗口...")
        mngr = plt.get_current_fig_manager()
        print(f"图形管理器类型: {type(mngr)}")
        
        # 不同backend的设置方法不同
        if hasattr(mngr, 'window'):
            # TkAgg, WXAgg
            print("使用TkAgg/WXAgg模式设置窗口位置")
            try:
                geom = mngr.window.geometry()
                print(f"当前窗口几何: {geom}")
            except:
                print("无法获取当前窗口几何信息")
                
            x = (screen_width - fig_width * 100) / 2
            y = (screen_height - fig_height * 100) / 2
            try:
                mngr.window.geometry(f"+{int(x)}+{int(y)}")
                print(f"窗口位置设置为: +{int(x)}+{int(y)}")
            except Exception as we:
                print(f"窗口位置设置失败: {we}")
                
        elif hasattr(mngr, 'frame'):
            # WX
            print("使用WX模式设置窗口位置")
            x = (screen_width - fig_width * 100) / 2
            y = (screen_height - fig_height * 100) / 2
            try:
                mngr.frame.SetPosition((int(x), int(y)))
                print(f"窗口位置设置为: ({int(x)}, {int(y)})")
            except Exception as we:
                print(f"窗口位置设置失败: {we}")
                
        elif hasattr(mngr, 'window') and hasattr(mngr.window, 'setGeometry'):
            # Qt backends
            print("使用Qt模式设置窗口位置")
            x = (screen_width - fig_width * 100) / 2
            y = (screen_height - fig_height * 100) / 2
            try:
                mngr.window.setGeometry(int(x), int(y), int(fig_width * 100), int(fig_height * 100))
                print(f"窗口位置和大小设置为: ({int(x)}, {int(y)}, {int(fig_width * 100)}, {int(fig_height * 100)})")
            except Exception as we:
                print(f"窗口位置设置失败: {we}")
        else:
            print(f"未知的图形管理器类型，无法设置窗口位置。可用属性: {dir(mngr)}")
    except Exception as e:
        print(f"无法居中窗口: {e}")
    
    plt.tight_layout()
    
    # 添加定时器，用于安全地更新UI
    if audio_player and audio_player.data is not None:
        # 设置定时器用于更新播放进度
        def update_playback_ui(frame):
            if audio_player and audio_player.playing:
                # 从主线程安全地更新UI
                if audio_player.update_ui():
                    # 仅当UI实际发生变化时重绘
                    fig.canvas.draw_idle()
            return []
            
        # 每100ms更新一次UI
        from matplotlib.animation import FuncAnimation
        ani = FuncAnimation(fig, update_playback_ui, interval=100, 
                           blit=True, cache_frame_data=False)
        # 保存动画对象的引用，防止被垃圾回收
        fig.ani = ani
    
    return fig, ax

def create_comparison_plot(source_data, query_data, top_sessions=None, query_audio_player=None):
    """Create comparison plot with connected matching points"""
    print("\n===== 创建比较可视化 =====")
    print(f"源数据标题: {source_data.get('title', 'Unknown')}")
    print(f"查询数据标题: {query_data.get('title', 'Unknown')}")
    print(f"会话数据: {'有' if top_sessions else '无'}")
    print(f"音频播放: {'启用' if query_audio_player else '禁用'}")
    if top_sessions:
        print(f"会话数量: {len(top_sessions)}")
    
    # 添加诊断助手
    def diagnose_data():
        print("\n===== 可视化诊断助手 =====")
        print(f"源音频: {source_data.get('title', 'Unknown')}")
        print(f"查询音频: {query_data.get('title', 'Unknown')}")
        
        # 检查匹配点数据结构
        if 'matchedPoints' in source_data:
            print(f"\n源数据匹配点数量: {len(source_data['matchedPoints'])}")
            if source_data['matchedPoints']:
                print(f"源数据匹配点示例: {source_data['matchedPoints'][0]}")
                print(f"类型: {type(source_data['matchedPoints'][0])}")
        else:
            print("警告: 源数据中没有匹配点")
        
        if 'matchedPoints' in query_data:
            print(f"\n查询数据匹配点数量: {len(query_data['matchedPoints'])}")
            if query_data['matchedPoints']:
                print(f"查询数据匹配点示例: {query_data['matchedPoints'][0]}")
                print(f"类型: {type(query_data['matchedPoints'][0])}")
        else:
            print("警告: 查询数据中没有匹配点")
        
        # 检查会话数据
        if top_sessions:
            print(f"\n会话数量: {len(top_sessions)}")
            if top_sessions:
                print(f"首个会话信息: {top_sessions[0]}")
                
                # 检查会话ID是否匹配
                if 'matchedPoints' in source_data and source_data['matchedPoints']:
                    session_ids_in_source = set()
                    for point in source_data['matchedPoints']:
                        if len(point) > 3:  # 应该有会话ID
                            session_ids_in_source.add(point[3])
                    
                    session_ids_in_top = {session['id'] for session in top_sessions}
                    
                    print(f"\n源数据中的会话ID: {session_ids_in_source}")
                    print(f"top_sessions中的会话ID: {session_ids_in_top}")
                    
                    if not session_ids_in_source.intersection(session_ids_in_top):
                        print("错误: 源数据中的会话ID与top_sessions中的ID不匹配!")
        
        print("===== 诊断结束 =====\n")
    
    # 运行诊断
    diagnose_data()
    
    # 获取屏幕尺寸（使用改进的方法）
    screen_width, screen_height = get_screen_size()
    
    # 设置为屏幕宽度的2/3，高度的2/3
    fig_width = screen_width * 2 / 3 / 100  # 转换为英寸 (假设DPI=100)
    fig_height = screen_height * 2 / 3 / 100
    print(f"调整比较窗口大小: {fig_width:.1f}x{fig_height:.1f} inches (屏幕: {screen_width}x{screen_height})")
    
    # Create figure with two subplots, add space for audio controls if needed
    print("创建比较图形...")
    if query_audio_player and query_audio_player.data is not None:
        # Create a figure with space for audio controls at the bottom
        grid = gridspec.GridSpec(3, 1, height_ratios=[3, 3, 1])
        fig = plt.figure(figsize=(fig_width, fig_height))
        ax1 = fig.add_subplot(grid[0])  # Source plot
        ax2 = fig.add_subplot(grid[1], sharex=ax1)  # Query plot
        # The grid[2] will be used for audio controls
    else:
        # Standard layout without audio
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(fig_width, fig_height), sharex=True)
    
    print("成功创建图形对象")
    
    # Dictionary to store annotations
    ax1_annot = ax1.annotate("", xy=(0, 0), xytext=(20, 20),
                            textcoords="offset points",
                            bbox=dict(boxstyle="round", fc="w"),
                            arrowprops=dict(arrowstyle="->"))
    ax1_annot.set_visible(False)
    
    ax2_annot = ax2.annotate("", xy=(0, 0), xytext=(20, 20),
                            textcoords="offset points",
                            bbox=dict(boxstyle="round", fc="w"),
                            arrowprops=dict(arrowstyle="->"))
    ax2_annot.set_visible(False)
    
    # Plot source audio (top)
    peaks_scatter1 = ax1.scatter([peak[1] for peak in source_data['allPeaks']], 
                                [peak[0] for peak in source_data['allPeaks']], 
                                c=[peak[2] for peak in source_data['allPeaks']], 
                                cmap='viridis', alpha=0.3, 
                                s=[10 + 30 * peak[2] for peak in source_data['allPeaks']], 
                                label='Source Peaks')
    
    fp_scatter1 = ax1.scatter([point[1] for point in source_data['fingerprintPoints']], 
                             [point[0] for point in source_data['fingerprintPoints']], 
                             color='red', s=12, label='Source Fingerprints')
    
    # Store matched point scatters for each session
    matched_scatters1 = []
    
    # Dictionary for matched points by session
    session_stats = {}  # 添加统计信息
    total_lines_created = 0  # 添加连线统计
    
    if top_sessions:
        # Use different colors for different sessions
        colors = list(mcolors.TABLEAU_COLORS)
        
        # For each session, add matched points with different colors
        for i, session in enumerate(top_sessions):
            session_id = session['id']
            session_color = colors[i % len(colors)]
            
            # 为该会话初始化统计数据
            session_stats[session_id] = {
                'match_count': session.get('matchCount', 0),
                'source_points': 0,
                'query_points': 0,
                'unique_hashes': set(),
                'connections': 0
            }
            
            # Filter source matched points for this session
            if 'matchedPoints' in source_data:
                session_points = [p for p in source_data['matchedPoints'] if len(p) > 3 and p[3] == session_id]
                session_stats[session_id]['source_points'] = len(session_points)
                
                if session_points:
                    scatter = ax1.scatter([p[1] for p in session_points], 
                                        [p[0] for p in session_points], 
                                        color=session_color, s=140, alpha=1.0, marker='*', 
                                        label=f'Session {session_id} ({session["matchCount"]} matches)')
                    matched_scatters1.append((scatter, session_id))
    elif 'matchedPoints' in source_data and source_data['matchedPoints']:
        # Just show all matched points without session differentiation
        scatter = ax1.scatter([point[1] for point in source_data['matchedPoints']], 
                            [point[0] for point in source_data['matchedPoints']], 
                            color='orange', s=100, alpha=0.8, marker='*', 
                            label='Matched Points')
        matched_scatters1.append((scatter, 'all'))
    
    ax1.set_title(f"Source Audio: {source_data['title']}")
    ax1.set_ylabel('Frequency (Hz)')
    ax1.set_ylim(0, 5000)
    ax1.grid(True, alpha=0.3)
    ax1.legend()
    
    # Add colorbar for source amplitude visualization
    cbar1 = fig.colorbar(peaks_scatter1, ax=ax1, label='Amplitude', pad=0.01)
    
    # Plot query audio (bottom)
    peaks_scatter2 = ax2.scatter([peak[1] for peak in query_data['allPeaks']], 
                                [peak[0] for peak in query_data['allPeaks']], 
                                c=[peak[2] for peak in query_data['allPeaks']], 
                                cmap='viridis', alpha=0.7, 
                                s=[10 + 30 * peak[2] for peak in query_data['allPeaks']], 
                                label='Query Peaks')
    
    fp_scatter2 = ax2.scatter([point[1] for point in query_data['fingerprintPoints']], 
                             [point[0] for point in query_data['fingerprintPoints']], 
                             color='purple', s=12, label='Query Fingerprints')
    
    # Store matched point scatters for each session
    matched_scatters2 = []
    
    # Add matched points for query with session differentiation
    if top_sessions:
        # For each session, add matched points with different colors
        for i, session in enumerate(top_sessions):
            session_id = session['id']
            session_color = colors[i % len(colors)]
            
            # Filter query matched points for this session
            if 'matchedPoints' in query_data:
                session_points = [p for p in query_data['matchedPoints'] if len(p) > 3 and p[3] == session_id]
                session_stats[session_id]['query_points'] = len(session_points)
                
                if session_points:
                    scatter = ax2.scatter([p[1] for p in session_points], 
                                        [p[0] for p in session_points], 
                                        color=session_color, s=150, alpha=1.0, marker='*', 
                                        label=f'Session {session_id} ({session["matchCount"]} matches)')
                    matched_scatters2.append((scatter, session_id))
    elif 'matchedPoints' in query_data and query_data['matchedPoints']:
        # Just show all matched points without session differentiation
        scatter = ax2.scatter([point[1] for point in query_data['matchedPoints']], 
                            [point[0] for point in query_data['matchedPoints']], 
                            color='orange', s=100, alpha=0.8, marker='*', 
                            label='Matched Points')
        matched_scatters2.append((scatter, 'all'))
        
    # 绘制连线
    if top_sessions:
        print("\n连线详情统计:")
        # For each session, connect the matching points
        for i, session in enumerate(top_sessions):
            session_id = session['id']
            session_color = colors[i % len(colors)]
            
            # Group points by hash in source and query
            source_points_by_hash = {}
            query_points_by_hash = {}
            
            # Extract points for this session
            source_session_points = [p for p in source_data.get('matchedPoints', []) 
                                    if len(p) > 3 and p[3] == session_id]
            query_session_points = [p for p in query_data.get('matchedPoints', []) 
                                   if len(p) > 3 and p[3] == session_id]
            
            print(f"\n会话 ID {session_id} (匹配计数: {session['matchCount']}):")
            print(f"  源数据匹配点数量: {len(source_session_points)}")
            print(f"  查询数据匹配点数量: {len(query_session_points)}")
            
            # Group by hash and collect unique hashes
            unique_hashes = set()
            source_hashes = set()
            query_hashes = set()
            
            for point in source_session_points:
                hash_value = point[2]
                if isinstance(hash_value, str) and hash_value.startswith('0x'):
                    # 转换字符串形式的哈希值为整数
                    hash_value = int(hash_value, 16)
                unique_hashes.add(hash_value)
                source_hashes.add(hash_value)
                if hash_value not in source_points_by_hash:
                    source_points_by_hash[hash_value] = []
                # 保存原始点
                source_points_by_hash[hash_value].append(point)
            
            for point in query_session_points:
                hash_value = point[2]
                if isinstance(hash_value, str) and hash_value.startswith('0x'):
                    # 转换字符串形式的哈希值为整数
                    hash_value = int(hash_value, 16)
                unique_hashes.add(hash_value)
                query_hashes.add(hash_value)
                if hash_value not in query_points_by_hash:
                    query_points_by_hash[hash_value] = []
                # 保存原始点
                query_points_by_hash[hash_value].append(point)
            
            session_stats[session_id]['unique_hashes'] = unique_hashes
            print(f"  唯一哈希值数量: {len(unique_hashes)}")
            print(f"  源数据哈希值数量: {len(source_hashes)}")
            print(f"  查询数据哈希值数量: {len(query_hashes)}")
            print(f"  源数据和查询数据哈希值交集数量: {len(source_hashes.intersection(query_hashes))}")
            
            # 如果交集很小，打印出部分哈希值进行对比
            if len(source_hashes.intersection(query_hashes)) < min(5, len(source_hashes), len(query_hashes)):
                print("\n  哈希值不匹配问题分析:")
                print(f"  源数据哈希值示例: {[f'0x{h:x}' for h in list(source_hashes)[:5]]}")
                print(f"  查询数据哈希值示例: {[f'0x{h:x}' for h in list(query_hashes)[:5]]}")
                print(f"  交集哈希值: {[f'0x{h:x}' for h in list(source_hashes.intersection(query_hashes))]}")
                
                # 可能的问题诊断
                if len(source_hashes) > 0 and len(query_hashes) > 0:
                    source_hash_example = next(iter(source_hashes))
                    query_hash_example = next(iter(query_hashes))
                    
                    print("\n  潜在问题诊断:")
                    if len(hex(source_hash_example)) != len(hex(query_hash_example)):
                        print(f"  哈希值格式不一致: 源={hex(source_hash_example)}，查询={hex(query_hash_example)}")
                    
                    # 检查是否存在字节序问题
                    source_hash_bytes = source_hash_example.to_bytes(4, byteorder='little')
                    source_hash_big = int.from_bytes(source_hash_bytes, byteorder='big')
                    if source_hash_big in query_hashes:
                        print(f"  疑似字节序问题: 源哈希 {hex(source_hash_example)} 经过字节序转换后 {hex(source_hash_big)} 存在于查询哈希中")
            
            # Connect points with the same hash values
            connections_count = 0
            # 详细记录哈希值和连线
            hash_connections = {}
            
            # 限制每个会话的最大连线数，避免图形过于复杂
            MAX_CONNECTIONS_PER_SESSION = 1000
            print(f"    设置最大连线数量: {MAX_CONNECTIONS_PER_SESSION}")
            
            # 从哈希值到源和查询间的连线
            for hash_value, source_points in source_points_by_hash.items():
                if connections_count >= MAX_CONNECTIONS_PER_SESSION:
                    print(f"    已达到最大连线数量 ({connections_count})，停止添加更多连线")
                    break
                    
                if hash_value in query_points_by_hash:
                    query_points = query_points_by_hash[hash_value]
                    hash_connections[hash_value] = []
                    
                    # 打印调试信息
                    print(f"    哈希值 {hash_value:x} 有 {len(source_points)} 个源点和 {len(query_points)} 个查询点")
                    if len(source_points) > 0 and len(query_points) > 0:
                        print(f"      源点示例: {source_points[0]}, 查询点示例: {query_points[0]}")
                    
                    # 连接具有相同哈希值的每个源点到每个查询点
                    # 限制每个哈希值的连线数量，避免过多连线
                    MAX_CONNECTIONS_PER_HASH = 5
                    connection_counter = 0
                    
                    # 计算可能的连线总数
                    total_possible_connections = len(source_points) * len(query_points)
                    if total_possible_connections > MAX_CONNECTIONS_PER_HASH:
                        print(f"      哈希值 {hash_value:x} 连线过多 ({total_possible_connections})，限制为 {MAX_CONNECTIONS_PER_HASH}")
                    
                    for i, source_point in enumerate(source_points):
                        # 如果已经达到连线限制，跳出循环
                        if connection_counter >= MAX_CONNECTIONS_PER_HASH:
                            break
                        
                        for j, query_point in enumerate(query_points):
                            # 如果已经达到连线限制，跳出循环
                            if connection_counter >= MAX_CONNECTIONS_PER_HASH:
                                break
                            
                            # 创建连线坐标 - 注意坐标系是 (时间, 频率)，与点数据相反
                            sp = (source_point[1], source_point[0])
                            qp = (query_point[1], query_point[0])
                            
                            # 打印第一个连线点的信息
                            if i == 0 and j == 0:
                                print(f"      连接点: 源=({sp[0]}, {sp[1]}), 查询=({qp[0]}, {qp[1]})")
                            
                            # 创建连线 - 使用比之前更高的透明度和更粗的线条
                            con = ConnectionPatch(xyA=sp, xyB=qp,
                                                 coordsA='data', coordsB='data',
                                                 axesA=ax1, axesB=ax2, 
                                                 color=session_color, alpha=0.6, linestyle='-', linewidth=0.7)
                            fig.add_artist(con)
                            # 立即刷新画布，确保连线显示
                            fig.canvas.draw_idle()
                            connections_count += 1
                            total_lines_created += 1
                            connection_counter += 1
                            hash_connections[hash_value].append((sp, qp))
                            # 添加详细日志
                            print(f"      LINE{total_lines_created}: 创建连线 session={session_id}, hash=0x{hash_value:x}, 源=({sp[0]}, {sp[1]}), 查询=({qp[0]}, {qp[1]}), 有效={con in fig.get_children()}")
                        
                    print(f"      为哈希值 {hash_value:x} 创建了 {connection_counter} 条连线")
            
            # 检查是否需要强制创建连线
            if len(unique_hashes) > connections_count and connections_count < 5:
                print(f"  警告: 哈希值数量({len(unique_hashes)})远大于连线数量({connections_count})")
                # 直接强制为每个哈希值创建连线，即使在source或query中没有对应点
                all_hashes = list(unique_hashes)
                all_source_points = source_session_points
                all_query_points = query_session_points
                
                # 如果没有足够的连线但有足够的匹配点，则强制连接
                if len(all_source_points) > 0 and len(all_query_points) > 0:
                    print(f"  尝试强制创建连线...")
                    for i, hash_value in enumerate(all_hashes):
                        if hash_value not in hash_connections or len(hash_connections[hash_value]) == 0:
                            # 为每个未连接的哈希值选择连线端点
                            source_idx = i % len(all_source_points)
                            query_idx = i % len(all_query_points)
                            
                            try:
                                # 获取原始点
                                source_point = all_source_points[source_idx]
                                query_point = all_query_points[query_idx]
                                
                                # 提取坐标
                                source_freq = float(source_point[0])
                                source_time = float(source_point[1])
                                query_freq = float(query_point[0])
                                query_time = float(query_point[1])
                                
                                # 创建连线坐标 (时间, 频率)
                                sp = (source_time, source_freq)
                                qp = (query_time, query_freq)
                                
                                print(f"    强制连接哈希值: {hash_value:x}")
                                print(f"      连线坐标: 源=({sp}), 查询=({qp})")
                                
                                # 创建虚线连线用于强制连接的点
                                con = ConnectionPatch(xyA=sp, xyB=qp,
                                                   coordsA='data', coordsB='data',
                                                   axesA=ax1, axesB=ax2, 
                                                   color=session_color, alpha=0.6, linestyle=':', linewidth=0.7)
                                fig.add_artist(con)
                                # 立即刷新画布，确保连线显示
                                fig.canvas.draw_idle()
                                connections_count += 1
                                total_lines_created += 1
                                # 添加详细日志
                                print(f"      FORCED_LINE{total_lines_created}: 强制创建连线 session={session_id}, hash=0x{hash_value:x}, 源=({source_time}, {source_freq}), 查询=({query_time}, {query_freq}), 有效={con in fig.get_children()}")
                            except Exception as e:
                                print(f"    强制连线失败: {e}")
            
            # 最后更新会话统计数据
            session_stats[session_id]['connections'] = connections_count
            print(f"  创建连线数量: {connections_count}")
            if connections_count > 0:
                print(f"  连线到哈希值比例: {len(unique_hashes)}/{connections_count} = {len(unique_hashes)/connections_count:.2f}")
            else:
                print(f"  警告: 没有创建任何连线！")

    # 添加统计信息到标题
    if top_sessions and session_stats:
        ax2.set_title(f"Query Audio: {query_data['title']}")
        for session_id, stats in session_stats.items():
            ax2.set_title(f"{ax2.get_title()}\nSession {session_id}: {stats['match_count']} matches, "
                         f"{len(stats['unique_hashes'])} unique hashes, {stats['connections']} connections")
    else:
        ax2.set_title(f"Query Audio: {query_data['title']}")
    
    ax2.set_xlabel('Time (s)')
    ax2.set_ylabel('Frequency (Hz)')
    ax2.set_ylim(0, 5000)
    ax2.grid(True, alpha=0.3)
    ax2.legend()
    
    # Add colorbar for query amplitude visualization
    cbar2 = fig.colorbar(peaks_scatter2, ax=ax2, label='Amplitude', pad=0.01)
    
    print(f"\n总共创建连线数量: {total_lines_created}")
    
    # 如果连线数远少于匹配点数，显示警告和建议
    for session_id, stats in session_stats.items():
        if stats['connections'] < stats['match_count'] * 0.1 and stats['match_count'] > 10:
            print(f"\n警告: 会话 {session_id} 的连线数 ({stats['connections']}) 远少于匹配点数 ({stats['match_count']})")
            print("可能原因:")
            print("1. 哈希碰撞 - 多个点共享相同的哈希值")
            print("2. 源数据或查询数据匹配点不完整")
            print("3. 会话ID在源和查询数据之间不一致")
            
            # 提供解决建议
            print("\n建议:")
            print("1. 检查指纹生成算法，减少哈希碰撞")
            print("2. 修改 saveComparisonData 方法，确保源和查询数据包含完整的匹配点")
            print("3. 尝试查看完整的 JSON 数据文件内容，检查匹配点的哈希值分布")
    
    # Hover function for top plot
    def hover1(event):
        if event.inaxes != ax1:
            if ax1_annot.get_visible():
                ax1_annot.set_visible(False)
                fig.canvas.draw_idle()
            return
        
        for scatter_obj, point_type in [(peaks_scatter1, 'peak'), (fp_scatter1, 'fingerprint')]:
            cont, ind = scatter_obj.contains(event)
            if cont:
                index = ind["ind"][0]
                if point_type == 'peak':
                    pos = scatter_obj.get_offsets()[index]
                    ax1_annot.xy = pos
                    text = f"Peak\nFreq: {source_data['allPeaks'][index][0]} Hz\nTime: {source_data['allPeaks'][index][1]:.2f} s\nAmplitude: {source_data['allPeaks'][index][2]:.4f}"
                else:  # fingerprint
                    pos = scatter_obj.get_offsets()[index]
                    ax1_annot.xy = pos
                    point = source_data['fingerprintPoints'][index]
                    text = f"Fingerprint\nFreq: {point[0]} Hz\nTime: {point[1]:.2f} s\nHash: {point[2]}"
                
                ax1_annot.set_text(text)
                ax1_annot.set_visible(True)
                fig.canvas.draw_idle()
                return
        
        # Check matched points for each session
        for scatter_obj, session_id in matched_scatters1:
            cont, ind = scatter_obj.contains(event)
            if cont:
                index = ind["ind"][0]
                pos = scatter_obj.get_offsets()[index]
                ax1_annot.xy = pos
                
                # 获取位置坐标
                x, y = pos
                
                # 找出这个位置的所有点
                if session_id == 'all':
                    session_points = source_data['matchedPoints']
                else:
                    session_points = [p for p in source_data['matchedPoints'] if len(p) > 3 and p[3] == session_id]
                
                # 筛选出与当前位置相同或接近的所有点
                same_position_points = []
                for p in session_points:
                    point_x = float(p[1])  # 时间坐标
                    point_y = float(p[0])  # 频率坐标
                    
                    # 使用一个小的阈值来判断点是否在同一位置
                    if abs(point_x - x) < 0.001 and abs(point_y - y) < 0.1:
                        same_position_points.append(p)
                
                # 构建显示文本，包含所有相同位置的点
                if len(same_position_points) == 1:
                    point = same_position_points[0]
                    text = f"Match (Session {session_id})\nFreq: {point[0]} Hz\nTime: {point[1]:.2f} s\nHash: {point[2]}"
                else:
                    # 多个点在同一位置的情况
                    text = f"Multiple Matches (Session {session_id})\nPosition: ({x:.2f}, {y:.0f})\n"
                    for i, point in enumerate(same_position_points):
                        text += f"Point {i+1}: Hash: {point[2]}\n"
                    text += f"total: {len(same_position_points)} points"
                
                ax1_annot.set_text(text)
                ax1_annot.set_visible(True)
                fig.canvas.draw_idle()
                return
        
        # If we get here, we didn't hover over anything
        if ax1_annot.get_visible():
            ax1_annot.set_visible(False)
            fig.canvas.draw_idle()
    
    # Hover function for bottom plot
    def hover2(event):
        if event.inaxes != ax2:
            if ax2_annot.get_visible():
                ax2_annot.set_visible(False)
                fig.canvas.draw_idle()
            return
        
        for scatter_obj, point_type in [(peaks_scatter2, 'peak'), (fp_scatter2, 'fingerprint')]:
            cont, ind = scatter_obj.contains(event)
            if cont:
                index = ind["ind"][0]
                if point_type == 'peak':
                    pos = scatter_obj.get_offsets()[index]
                    ax2_annot.xy = pos
                    text = f"Peak\nFreq: {query_data['allPeaks'][index][0]} Hz\nTime: {query_data['allPeaks'][index][1]:.2f} s\nAmplitude: {query_data['allPeaks'][index][2]:.4f}"
                else:  # fingerprint
                    pos = scatter_obj.get_offsets()[index]
                    ax2_annot.xy = pos
                    point = query_data['fingerprintPoints'][index]
                    text = f"Fingerprint\nFreq: {point[0]} Hz\nTime: {point[1]:.2f} s\nHash: {point[2]}"
                
                ax2_annot.set_text(text)
                ax2_annot.set_visible(True)
                fig.canvas.draw_idle()
                return
        
        # Check matched points for each session
        for scatter_obj, session_id in matched_scatters2:
            cont, ind = scatter_obj.contains(event)
            if cont:
                index = ind["ind"][0]
                pos = scatter_obj.get_offsets()[index]
                ax2_annot.xy = pos
                
                # 获取位置坐标
                x, y = pos
                
                # 找出这个位置的所有点
                if session_id == 'all':
                    session_points = query_data['matchedPoints']
                else:
                    session_points = [p for p in query_data['matchedPoints'] if len(p) > 3 and p[3] == session_id]
                
                # 筛选出与当前位置相同或接近的所有点
                same_position_points = []
                for p in session_points:
                    point_x = float(p[1])  # 时间坐标
                    point_y = float(p[0])  # 频率坐标
                    
                    # 使用一个小的阈值来判断点是否在同一位置
                    if abs(point_x - x) < 0.001 and abs(point_y - y) < 0.1:
                        same_position_points.append(p)
                
                # 构建显示文本，包含所有相同位置的点
                if len(same_position_points) == 1:
                    point = same_position_points[0]
                    text = f"Match (Session {session_id})\nFreq: {point[0]} Hz\nTime: {point[1]:.2f} s\nHash: {point[2]}"
                else:
                    # 多个点在同一位置的情况
                    text = f"Multiple Matches (Session {session_id})\nPosition: ({x:.2f}, {y:.0f})\n"
                    for i, point in enumerate(same_position_points):
                        text += f"Point {i+1}: Hash: {point[2]}\n"
                    text += f"total: {len(same_position_points)} points"
                
                ax2_annot.set_text(text)
                ax2_annot.set_visible(True)
                fig.canvas.draw_idle()
                return
        
        # If we get here, we didn't hover over anything
        if ax2_annot.get_visible():
            ax2_annot.set_visible(False)
            fig.canvas.draw_idle()
    
    # Connect hover events
    fig.canvas.mpl_connect("motion_notify_event", hover1)
    fig.canvas.mpl_connect("motion_notify_event", hover2)
    
    # 添加窗口关闭事件处理
    def on_close(event):
        print("Window close event detected - cleaning up resources")
        if plt.fignum_exists(fig.number):
            plt.close(fig)  # 关闭当前图形
        plt.close('all')  # 保证关闭所有图形
    
    # 添加键盘事件处理 - 增强的版本
    def on_key(event):
        if event.key == 'escape':
            print("ESC键被按下 - 关闭窗口")
            if plt.fignum_exists(fig.number):
                plt.close(fig)  # 关闭当前图形
    
    # 注册窗口事件
    fig.canvas.mpl_connect('close_event', on_close)
    fig.canvas.mpl_connect('key_press_event', on_key)
    
    plt.tight_layout()
    
    # 添加比较模式下的交互功能
    def add_comparison_controls(fig, ax1, ax2):
        """添加比较模式下的交互控制"""
        print("添加比较模式交互控制...")
        
        # 创建控制按钮
        reset_ax = plt.axes([0.8, 0.01, 0.1, 0.04])
        reset_button = Button(reset_ax, 'Reset View', color='lightblue')
        
        # 添加重置视图功能
        def reset_view(event):
            print("重置视图")
            ax1.relim()  # 重置轴范围
            ax1.autoscale_view()  # 自动调整视图
            ax2.relim()
            ax2.autoscale_view()
            fig.canvas.draw_idle()  # 重绘画布
        
        reset_button.on_clicked(reset_view)
        
        # 保存按钮引用，防止垃圾回收
        fig.reset_button = reset_button
        
        return reset_button

    # 在比较图创建完成后添加控制
    reset_button = add_comparison_controls(fig, ax1, ax2)

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
    if query_audio_player and query_audio_player.data is not None:
        print("\n===== 添加音频播放控制 =====")
        
        # Add audio control panel at the bottom
        controls_ax = fig.add_subplot(grid[2])
        controls_ax.set_facecolor('lightgray')
        controls_ax.set_xticks([])
        controls_ax.set_yticks([])
        
        # Add a vertical line to show playback position in the query subplot
        query_audio_player.playback_line = ax2.axvline(x=0, color='green', linestyle='-', linewidth=2)
        
        # Create play button
        play_ax = plt.axes([0.2, 0.07, 0.15, 0.06])
        play_button = Button(play_ax, 'Play', color='lightgreen', hovercolor='green')
        
        # Create stop button
        stop_ax = plt.axes([0.36, 0.07, 0.15, 0.06])
        stop_button = Button(stop_ax, 'Stop', color='lightcoral', hovercolor='red')
        
        # Create time slider
        slider_ax = plt.axes([0.2, 0.15, 0.6, 0.05])
        time_slider = Slider(slider_ax, 'Time', 0, query_audio_player.duration, valinit=0)
        
        # Create time display
        time_text = controls_ax.text(0.85, 0.5, "00:00", fontsize=14)
        query_audio_player.time_display = time_text
        
        # Create status display
        status_text = controls_ax.text(0.5, 0.5, "Ready", fontsize=14, weight='bold')
        query_audio_player.status_text = status_text
        
        # Add file info
        duration_mins = int(query_audio_player.duration) // 60
        duration_secs = int(query_audio_player.duration) % 60
        file_info = f"Audio: {os.path.basename(query_audio_player.audio_file)}\nDuration: {duration_mins:02}:{duration_secs:02}"
        file_text = controls_ax.text(0.05, 0.8, file_info, fontsize=12)
        
        # 保存按钮引用到音频播放器中
        query_audio_player.play_button = play_button
        
        # Connect button events
        def on_play(event):
            print(f"\n===== 播放按钮被点击 =====")
            print(f"音频播放器状态: playing={query_audio_player.playing}, has_finished={query_audio_player.has_finished}")
            
            if query_audio_player.playing:
                print("停止播放")
                query_audio_player.stop()
                play_button.label.set_text('Play')
            else:
                # 检查是否播放已经完成过一次
                if query_audio_player.current_time >= query_audio_player.duration - 0.1:
                    print("检测到播放已到达结尾，从头开始播放")
                    query_audio_player.restart()  # 从头重新开始播放
                else:
                    print(f"从当前位置继续播放: {query_audio_player.current_time:.2f}秒")
                    query_audio_player.play(query_audio_player.current_time)
                
                play_button.label.set_text('Pause')
                
            print("按钮事件处理完成")
            # 强制重绘按钮
            play_button.ax.figure.canvas.draw_idle()
        
        def on_stop(event):
            print(f"\n===== 停止按钮被点击 =====")
            query_audio_player.stop()
            play_button.label.set_text('Play')
            # 强制重绘按钮
            play_button.ax.figure.canvas.draw_idle()
            print("停止播放")
        
        def on_slider_changed(val):
            print(f"\n===== 滑块被调整 =====")
            print(f"滑块值: {val:.2f}")
            query_audio_player.seek(val)
        
        print(f"\n正在连接音频控制按钮事件处理器...")
        play_button.on_clicked(on_play)
        stop_button.on_clicked(on_stop)
        time_slider.on_changed(on_slider_changed)
        print(f"按钮事件处理器已连接")
        
        # Add click handler to seek in the main plot
        def on_plot_click(event):
            if event.inaxes == ax2:  # 只在查询图上点击触发
                # Get x position (time)
                time_pos = event.xdata
                if time_pos is not None:
                    if time_pos < 0:
                        time_pos = 0
                    elif time_pos > query_audio_player.duration:
                        time_pos = query_audio_player.duration
                    
                    # Seek to that position
                    query_audio_player.seek(time_pos)
                    time_slider.set_val(time_pos)
        
        fig.canvas.mpl_connect('button_press_event', on_plot_click)
        
        # 设置定时器用于更新播放进度
        def update_playback_ui(frame):
            if query_audio_player and query_audio_player.playing:
                # 从主线程安全地更新UI
                if query_audio_player.update_ui():
                    # 仅当UI实际发生变化时重绘
                    fig.canvas.draw_idle()
            return []
        
        # 每100ms更新一次UI
        from matplotlib.animation import FuncAnimation
        ani = FuncAnimation(fig, update_playback_ui, interval=100, 
                          blit=True, cache_frame_data=False)
        # 保存动画对象的引用，防止被垃圾回收
        fig.ani = ani
    
    return fig, (ax1, ax2)

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
        
        # 创建查询音频文件的音频播放器
        query_audio_player = None
        
        # 检查是否提供了查询音频文件
        audio_file_path = args.query_audio
        if not audio_file_path and 'audioFilePath' in query_data and os.path.exists(query_data['audioFilePath']):
            audio_file_path = query_data['audioFilePath']
            print(f"使用查询数据JSON中的音频文件路径: {audio_file_path}")
            
        if audio_file_path and AUDIO_SUPPORT:
            print(f"\n===== 创建查询音频播放器 =====")
            print(f"音频文件: {audio_file_path}")
            query_audio_player = AudioPlayer(audio_file_path)
            if query_audio_player.data is None:
                print("警告: 无法加载查询音频数据，禁用音频播放")
                query_audio_player = None
            else:
                print(f"查询音频播放器创建成功: 长度 {query_audio_player.duration:.2f}秒")
        else:
            if not AUDIO_SUPPORT:
                print("警告: 音频播放功能未启用，请安装 soundfile 和 sounddevice 包")
            elif not audio_file_path:
                print("注意: 未提供查询音频文件路径，比较可视化将不包含音频播放功能")
        
        # Check if we have audio files in the JSON data for future reference
        if 'audioFilePath' in source_data:
            print(f"源数据音频文件路径: {source_data['audioFilePath']}")
        if 'audioFilePath' in query_data:
            print(f"查询数据音频文件路径: {query_data['audioFilePath']}")
            
        try:
            print("\n===== 创建比较可视化 =====")
            fig, axes = create_comparison_plot(source_data, query_data, top_sessions, query_audio_player)
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
            fig, axes = create_comparison_plot(source_data, query_data, top_sessions)
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