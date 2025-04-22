#!/usr/bin/env python3
import json
import matplotlib.pyplot as plt
import numpy as np
import os
import sys
import argparse
from matplotlib.patches import ConnectionPatch
import matplotlib.colors as mcolors

def load_data(filename):
    """Load fingerprint data from JSON file"""
    with open(filename, 'r') as f:
        return json.load(f)

def create_interactive_plot(data, plot_type='extraction'):
    """Create interactive plot with hover information"""
    # Create plot
    fig, ax = plt.figure(figsize=(15, 8)), plt.gca()
    
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
                                  color='blue', alpha=0.3, s=10, label='All Peaks')
        
        # Plot fingerprint points
        fp_scatter = ax.scatter([point[1] for point in data['fingerprintPoints']], 
                               [point[0] for point in data['fingerprintPoints']], 
                               color='red', s=25, label='Fingerprint Points')
        
        # Set title and labels
        plt.title(f"Audio Fingerprint Extraction: {data['title']}")
        
        # Create hover function
        def update_annot(ind, scatter_obj, point_type):
            index = ind["ind"][0]
            if scatter_obj == peaks_scatter:
                pos = scatter_obj.get_offsets()[index]
                annot.xy = pos
                text = f"Peak\nFreq: {data['allPeaks'][index][0]} Hz\nTime: {data['allPeaks'][index][1]:.2f} s"
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
                                  color='blue', alpha=0.3, s=10, label='All Peaks')
        
        # Plot fingerprint points
        fp_scatter = ax.scatter([point[1] for point in data['fingerprintPoints']], 
                               [point[0] for point in data['fingerprintPoints']], 
                               color='red', s=25, label='Fingerprint Points')
        
        # Plot matched points
        matched_scatter = None
        if 'matchedPoints' in data and data['matchedPoints']:
            matched_scatter = ax.scatter([point[1] for point in data['matchedPoints']], 
                                        [point[0] for point in data['matchedPoints']], 
                                        color='orange', s=100, alpha=0.8, marker='*', 
                                        label='Matched Points')
        
        # Set title and labels
        plt.title(f"Audio Fingerprint Matching: {data['title']}")
        
        # Create hover function
        def update_annot(ind, scatter_obj, point_type):
            index = ind["ind"][0]
            if scatter_obj == peaks_scatter:
                pos = scatter_obj.get_offsets()[index]
                annot.xy = pos
                text = f"Peak\nFreq: {data['allPeaks'][index][0]} Hz\nTime: {data['allPeaks'][index][1]:.2f} s"
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
    plt.xlabel('Time (s)')
    plt.ylabel('Frequency (Hz)')
    plt.ylim(0, 5000)  # Limit frequency display range
    plt.grid(True, alpha=0.3)
    plt.legend()
    
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
    
    return fig, ax 

def create_comparison_plot(source_data, query_data, top_sessions=None):
    """Create comparison plot with connected matching points"""
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
    
    # Create figure with two subplots
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(15, 12), sharex=True)
    
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
                                color='blue', alpha=0.3, s=10, label='Source Peaks')
    
    fp_scatter1 = ax1.scatter([point[1] for point in source_data['fingerprintPoints']], 
                             [point[0] for point in source_data['fingerprintPoints']], 
                             color='red', s=25, label='Source Fingerprints')
    
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
                                        color=session_color, s=150, alpha=1.0, marker='*', 
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
    
    # Plot query audio (bottom)
    peaks_scatter2 = ax2.scatter([peak[1] for peak in query_data['allPeaks']], 
                                [peak[0] for peak in query_data['allPeaks']], 
                                color='green', alpha=0.3, s=10, label='Query Peaks')
    
    fp_scatter2 = ax2.scatter([point[1] for point in query_data['fingerprintPoints']], 
                             [point[0] for point in query_data['fingerprintPoints']], 
                             color='purple', s=25, label='Query Fingerprints')
    
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
            
            # 从哈希值到源和查询间的连线
            for hash_value, source_points in source_points_by_hash.items():
                if hash_value in query_points_by_hash:
                    query_points = query_points_by_hash[hash_value]
                    hash_connections[hash_value] = []
                    
                    # 打印调试信息
                    print(f"    哈希值 {hash_value:x} 有 {len(source_points)} 个源点和 {len(query_points)} 个查询点")
                    if len(source_points) > 0 and len(query_points) > 0:
                        print(f"      源点示例: {source_points[0]}, 查询点示例: {query_points[0]}")
                    
                    # 连接具有相同哈希值的每个源点到每个查询点
                    connection_counter = 0
                    for i, source_point in enumerate(source_points):
                        for j, query_point in enumerate(query_points):
                            try:
                                # 从点中提取时间和频率值
                                source_freq = float(source_point[0])
                                source_time = float(source_point[1])
                                query_freq = float(query_point[0])
                                query_time = float(query_point[1])
                                
                                # 创建连线坐标 - 注意坐标系是 (时间, 频率)，与点数据相反
                                sp = (source_time, source_freq)
                                qp = (query_time, query_freq)
                                
                                # 打印第一个连线点的信息
                                if i == 0 and j == 0:
                                    print(f"      连接点: 源=({source_time}, {source_freq}), 查询=({query_time}, {query_freq})")
                                
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
                                print(f"      LINE{total_lines_created}: 创建连线 session={session_id}, hash=0x{hash_value:x}, 源=({source_time}, {source_freq}), 查询=({query_time}, {query_freq}), 有效={con in fig.get_children()}")
                            except Exception as e:
                                print(f"      创建连线失败: {e}")
                    
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
                    text = f"Peak\nFreq: {source_data['allPeaks'][index][0]} Hz\nTime: {source_data['allPeaks'][index][1]:.2f} s"
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
                    text = f"Peak\nFreq: {query_data['allPeaks'][index][0]} Hz\nTime: {query_data['allPeaks'][index][1]:.2f} s"
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
    
    plt.tight_layout()
    return fig, (ax1, ax2)

def main():
    parser = argparse.ArgumentParser(description='Visualize audio fingerprints')
    parser.add_argument('--source', type=str, help='Source audio fingerprint JSON file')
    parser.add_argument('--query', type=str, help='Query audio fingerprint JSON file')
    parser.add_argument('--output', type=str, help='Output image file')
    parser.add_argument('--sessions', type=str, help='JSON file containing top sessions data')
    args = parser.parse_args()
    
    # If only source is provided, create extraction plot
    if args.source and not args.query:
        data = load_data(args.source)
        fig, ax = create_interactive_plot(data, 'extraction')
        
        # Save to file if output is specified
        if args.output:
            fig.savefig(args.output)
            print(f"Saved extraction plot to {args.output}")
        else:
            plt.show()
    
    # If only query is provided, create matching plot
    elif args.query and not args.source:
        data = load_data(args.query)
        fig, ax = create_interactive_plot(data, 'matching')
        
        # Save to file if output is specified
        if args.output:
            fig.savefig(args.output)
            print(f"Saved matching plot to {args.output}")
        else:
            plt.show()
    
    # If both source and query are provided, create comparison plot
    elif args.source and args.query:
        source_data = load_data(args.source)
        query_data = load_data(args.query)
        
        # If top sessions file is provided, load it
        top_sessions = None
        if args.sessions:
            with open(args.sessions, 'r') as f:
                top_sessions = json.load(f)
        
        fig, axes = create_comparison_plot(source_data, query_data, top_sessions)
        
        # Save to file if output is specified
        if args.output:
            fig.savefig(args.output)
            print(f"Saved comparison plot to {args.output}")
        else:
            plt.show()
    
    else:
        print("Error: You must specify at least --source or --query.")
        parser.print_help()
        sys.exit(1)

if __name__ == "__main__":
    main() 