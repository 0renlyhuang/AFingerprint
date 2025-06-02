#!/usr/bin/env python3
"""
音频指纹可视化工具 - 主入口文件
简化版本，通过模块导入提供功能
"""

import argparse
import json
import os
import sys
import matplotlib.pyplot as plt

# Add the parent directory to sys.path for imports
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# 导入自定义模块
from visualization.config import (AUDIO_SUPPORT, PCM_SAMPLE_RATE, PCM_CHANNELS, PCM_FORMAT,
                    REFRESH_RATE_30FPS, REFRESH_RATE_60FPS, 
                    _ui_refresh_interval, _playback_update_interval,
                    clean_up)
from visualization.plot_utils import load_data
from visualization.audio_player import AudioPlayer
from visualization.plotting import create_interactive_plot, create_comparison_plot


def main():
    global PCM_SAMPLE_RATE, PCM_CHANNELS, PCM_FORMAT, _ui_refresh_interval, _playback_update_interval
    
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
    # 刷新率参数
    parser.add_argument('--high-refresh', action='store_true', help='Enable high refresh rate (60fps) for smoother audio playback visualization')
    # 诊断参数
    parser.add_argument('--debug-comparison', action='store_true', help='Run comparison visualization in debug mode')
    parser.add_argument('--force-backend', type=str, help='Force specific matplotlib backend (e.g., TkAgg, Qt5Agg)')
    args = parser.parse_args()
    
    # 设置刷新率
    if args.high_refresh:
        _ui_refresh_interval = REFRESH_RATE_60FPS
        _playback_update_interval = 0.016  # 16ms更新间隔（约60fps）
        print(f"启用高刷新率模式: 60fps (UI间隔: {_ui_refresh_interval}ms, 播放更新间隔: {_playback_update_interval*1000:.1f}ms)")
    else:
        _ui_refresh_interval = REFRESH_RATE_30FPS
        _playback_update_interval = 0.033  # 33ms更新间隔（约30fps）
        print(f"使用标准刷新率模式: 30fps (UI间隔: {_ui_refresh_interval}ms, 播放更新间隔: {_playback_update_interval*1000:.1f}ms)")
    
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

