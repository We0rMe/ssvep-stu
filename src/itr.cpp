#include "itr.h"
#include <cmath>

double itr(int num_targets, double accuracy, double selection_time) {
    // 确保准确率在有效范围内
    if (accuracy <= 0.0) return 0.0;
    if (accuracy >= 1.0) accuracy = 0.999999;  // 避免log2(1)的情况
    
    double p = accuracy;
    double n = static_cast<double>(num_targets);
    
    // 计算每个选择的信息传输率（bits per selection）
    double bits = log2(n) + p * log2(p) + (1 - p) * log2((1 - p) / (n - 1));
    
    // 转换为每分钟的信息传输率（bits per minute）
    return bits * 60.0 / selection_time;
} 