#!/usr/bin/env python3
"""
音频播放器模块
包含AudioPlayer类和音频播放相关功能
"""

import os
import threading
import time
import numpy as np
from visualization.config import (AUDIO_SUPPORT, PCM_SAMPLE_RATE, PCM_CHANNELS, 
                    PCM_FORMAT, PCM_SAMPLE_WIDTH, _playback_update_interval)

if AUDIO_SUPPORT:
    import soundfile as sf
    import sounddevice as sd


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
            self._playback_worker_impl(start_sample)
        
        # Start playback thread
        self.playback_thread = threading.Thread(target=playback_worker)
        self.playback_thread.daemon = True
        self.playback_thread.start()
        print(f"播放线程已启动: {self.playback_thread.ident}")
    
    def _playback_worker_impl(self, start_sample):
        """Implementation of playback worker thread"""
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
                        
                        # 处理音频数据格式
                        block = self._process_audio_block(block, channels)
                        
                        try:
                            # Write to stream
                            stream.write(block)
                            
                            # Update current time
                            self.current_time = (start_sample + i) / self.samplerate
                            
                            # 标记需要更新UI，但不直接调用matplotlib函数
                            self.update_needed = True
                            
                            # 减少更新频率，避免过多的UI更新请求
                            current_time = time.time()
                            if current_time - self.last_update_time > _playback_update_interval:
                                self.last_update_time = current_time
                            
                            # 不再直接调用plt.pause
                            time.sleep(0.005)  # 让出CPU时间，但不调用matplotlib（5ms）
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
    
    def _process_audio_block(self, block, channels):
        """Process audio block for playback"""
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
        
        return block
    
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