#!/usr/bin/env python3
"""
绘图工具函数模块
包含数据处理、文件名清理等工具函数
"""

import json
import re


def load_data(filename):
    """Load fingerprint data from JSON file"""
    with open(filename, 'r') as f:
        return json.load(f)


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