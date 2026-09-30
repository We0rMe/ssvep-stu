#include "train_trca.h"
#include "filterbank.h"
#include <vector>
#include <cmath>
#include <Eigen/Dense>
#include <iostream>

namespace {
Eigen::MatrixXd corrcoef(const Eigen::MatrixXd& X, const Eigen::MatrixXd& Y) {
    // 将输入转换为列向量
    Eigen::VectorXd x = Eigen::Map<const Eigen::VectorXd>(X.data(), X.size());
    Eigen::VectorXd y = Eigen::Map<const Eigen::VectorXd>(Y.data(), Y.size());
    
    // 构造合并矩阵
    Eigen::MatrixXd combined(x.size(), 2);
    combined.col(0) = x;
    combined.col(1) = y;
    
    // 计算协方差矩阵
    Eigen::MatrixXd centered = combined.rowwise() - combined.colwise().mean();
    Eigen::MatrixXd cov = (centered.transpose() * centered) / (combined.rows() - 1);
    
    // 计算标准差
    Eigen::VectorXd std_dev = (centered.array().square().colwise().sum() / (combined.rows() - 1)).sqrt();
    
    // 计算相关系数矩阵
    Eigen::MatrixXd R(2, 2);
    R(0,0) = R(1,1) = 1.0;
    R(0,1) = R(1,0) = cov(0,1) / (std_dev(0) * std_dev(1));
    
    return R;
}
}  // namespace

// 输入test_data: num_targs * num_chans * num_smpls
// 输入model: trains(targets, fbs × channels × samples), W(fbs × targets × channels, 1)
Eigen::VectorXi test_trca(const Eigen::RowVectorXd& test_data, const TRCAModel& model, bool is_ensemble) {
    const int num_targs = model.num_targs;
    const int num_chans = 9;
    const Eigen::Index num_smpls = test_data.cols() / (num_targs * num_chans);
    const int num_fbs = model.num_fbs;
    
    // 初始化结果向量
    Eigen::VectorXi results = Eigen::VectorXi::Zero(num_targs);
    
    // 获取频带权重
    Eigen::VectorXd fb_coefs = Eigen::VectorXd::Zero(num_fbs);
    for (int i = 0; i < num_fbs; i++) {
        fb_coefs(i) = std::pow(i + 1, -1.25) + 0.25;
    }

    // 对每个目标进行处理
    for (int targ_i = 0; targ_i < num_targs; ++targ_i) {
        // 重组测试数据为 [channels × samples]
        Eigen::MatrixXd testdata(num_chans, num_smpls);
        for (int chan = 0; chan < num_chans; chan++) {
            for (Eigen::Index smpl = 0; smpl < num_smpls; smpl++) {
                // 源数据索引：target + num_targs * channel + num_targs * num_chans * sample
                const Eigen::Index src_idx = targ_i + num_targs * chan + num_targs * num_chans * smpl;
                testdata(chan, smpl) = test_data(src_idx);
            }
        }

        

        // 初始化相关系数矩阵
        Eigen::MatrixXd r = Eigen::MatrixXd::Zero(num_fbs, num_targs);
        
        // 对每个频带进行处理
        for (int fb_i = 0; fb_i < num_fbs; ++fb_i) {

            // 应用滤波器组
            Eigen::MatrixXd filtered_test = filterbankEpoch(testdata, model.fs, fb_i + 1);

            // 不应用滤波器组
            // Eigen::MatrixXd filtered_test = testdata;

            // std::cout << "testdata: " << testdata.row(0) << std::endl;

            // 对每个类别计算相关系数
            for (int class_i = 0; class_i < num_targs; ++class_i) {
                // 获取训练模板
                Eigen::MatrixXd traindata(num_chans, num_smpls); // model.trains (num_targs, num_fbs * num_chans * num_smpls);
                for (int chan = 0; chan < num_chans; chan++) {
                    const Eigen::Index train_offset = fb_i * num_chans * num_smpls + chan * num_smpls;
                    traindata.row(chan) = model.trains.block(class_i, train_offset, 1, num_smpls);
                }

                // std::cout << "traindata: " << traindata.row(0) << std::endl;

                // 获取权重向量
                if (is_ensemble) {
                    // 集成模式：获取所有类别的权重 [channels × targets]
                    Eigen::MatrixXd W_fb = Eigen::MatrixXd::Zero(num_chans, num_targs);
                    for (int class_i = 0; class_i < num_targs; class_i++) {
                        Eigen::Index w_offset = fb_i * num_targs * num_chans + class_i * num_chans;
                        W_fb.col(class_i) = model.W.block(w_offset, 0, num_chans, 1);
                    }
                    
                    // 计算投影后的相关系数
                    Eigen::MatrixXd test_proj = filtered_test.transpose() * W_fb;  // [samples × targets]
                    Eigen::MatrixXd train_proj = traindata.transpose() * W_fb;     // [samples × targets]
                    
                    // 计算相关系数
                    r(fb_i, class_i) = corrcoef(test_proj, train_proj)(0, 1);
                    
                    // std::cout << "r: " << r(0,0) << std::endl;
                    
                } else {
                    // 非集成模式：只获取当前目标的权重 [channels × 1]
                    Eigen::Index w_offset = fb_i * num_targs * num_chans + class_i * num_chans;
                    Eigen::VectorXd w = model.W.block(w_offset, 0, num_chans, 1);
                    
                    // 计算投影后的相关系数
                    Eigen::VectorXd test_proj = filtered_test.transpose() * w;
                    Eigen::VectorXd train_proj = traindata.transpose() * w;
                    
                    // 存储相关系数
                    r(fb_i, class_i) = corrcoef(test_proj, train_proj)(0, 1);
                }
            }
        }
        
        // 计算加权相关系数并找到最大值对应的类别
        Eigen::VectorXd rho = fb_coefs.transpose() * r;
        int max_idx;
        rho.maxCoeff(&max_idx);
        results(targ_i) = max_idx + 1;  // 类别标签从1开始
    }
    
    return results;
}

