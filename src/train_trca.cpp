#include "train_trca.h"
#include "filterbank.h"

#include <iostream>

namespace
{

    Eigen::MatrixXd trca(const Eigen::MatrixXd &eeg)
    {
        // eeg 维度为 (blocks，channels × samples)
        const Eigen::Index num_blocks = eeg.rows();
        const Eigen::Index total_cols = eeg.cols();
        const int num_chans = 9;
        const Eigen::Index num_smpls = total_cols / num_chans;

        // 初始化协方差矩阵
        Eigen::MatrixXd S = Eigen::MatrixXd::Zero(num_chans, num_chans);

        // 计算跨试验协方差
        for (int trial_i = 0; trial_i < num_blocks - 1; trial_i++)
        {
            // 获取当前试验数据，重组为 [channels × samples] 格式
            Eigen::MatrixXd x1(num_chans, num_smpls);
            for (int chan = 0; chan < num_chans; chan++) {
                for (Eigen::Index smpl = 0; smpl < num_smpls; smpl++) {
                    // 源数据索引：channel × samples + sample
                    const Eigen::Index src_idx = chan * num_smpls + smpl;
                    x1(chan, smpl) = eeg(trial_i, src_idx);
                }
            }

            // 去均值
            Eigen::VectorXd mean1 = x1.rowwise().mean();
            x1.colwise() -= mean1;

            for (int trial_j = trial_i + 1; trial_j < num_blocks; trial_j++)
            {
                // 获取另一个试验数据，重组为 [channels × samples] 格式
                Eigen::MatrixXd x2(num_chans, num_smpls);
                for (int chan = 0; chan < num_chans; chan++) {
                    for (Eigen::Index smpl = 0; smpl < num_smpls; smpl++) {
                        const Eigen::Index src_idx = chan * num_smpls + smpl;
                        x2(chan, smpl) = eeg(trial_j, src_idx);
                    }
                }

                // 去均值
                Eigen::VectorXd mean2 = x2.rowwise().mean();
                x2.colwise() -= mean2;

                S += x1 * x2.transpose() + x2 * x1.transpose();
                // std::cout << "S: " << S << std::endl;
            }
        }

        // 计算总体协方差
        Eigen::MatrixXd Q = Eigen::MatrixXd::Zero(num_chans, num_chans);
        {
            // 重组数据为 [channels × (samples*blocks)]
            Eigen::MatrixXd UX(num_chans, num_smpls * num_blocks);
            
            // 重组数据
            for (int chan = 0; chan < num_chans; chan++) {
                for (Eigen::Index trial = 0; trial < num_blocks; trial++) {
                    for (Eigen::Index smpl = 0; smpl < num_smpls; smpl++) {
                        // 源数据索引：channel × samples + sample
                        const Eigen::Index src_idx = chan * num_smpls + smpl;
                        // MATLAB reshape(eeg, num_chans, num_smpls*num_trials)
                        // 对应顺序应为 trial-major: trial*num_smpls + smpl
                        const Eigen::Index dst_idx = trial * num_smpls + smpl;
                        UX(chan, dst_idx) = eeg(trial, src_idx);
                    }
                }
            }
            
            // 去均值
            Eigen::VectorXd mean = UX.rowwise().mean();
            UX.colwise() -= mean;
            
            // 计算协方差
            Q = UX * UX.transpose();
            
        }

        // 求解广义特征值问题
        Eigen::GeneralizedEigenSolver<Eigen::MatrixXd> ges;
        ges.compute(S, Q);

        // 检查计算是否成功
        if (ges.info() != Eigen::Success)
        {
            std::cerr << "Failed to compute generalized eigenvalue problem" << std::endl;
            return Eigen::MatrixXd::Ones(num_chans, 1);
        }

        // 获取特征向量和特征值
        Eigen::MatrixXd eigenvectors = ges.eigenvectors().real();
        Eigen::VectorXd eigenvalues = ges.eigenvalues().real();

        // 找到最大特征值对应的索引
        Eigen::Index maxIndex = 0;
        double maxEigenvalue = eigenvalues(0);
        for(Eigen::Index i = 1; i < eigenvalues.size(); i++) {
            if(eigenvalues(i) > maxEigenvalue) {
                maxEigenvalue = eigenvalues(i);
                maxIndex = i;
            }
        }

        // 获取最大特征值对应的特征向量
        Eigen::VectorXd w = eigenvectors.col(maxIndex);

        // 归一化：使最大绝对值为1
        double maxAbs = w.array().abs().maxCoeff();
        w /= maxAbs;

        // std::cout << "eigenvalues: " << eigenvalues.transpose() << std::endl;
        // std::cout << "selected eigenvector: " << w.transpose() << std::endl;

        return w;
    }
}

// 函数输入： eeg(blocks, targets × channels × samples)
// 滤波时输入： eeg_tmp = eeg(blocks, channels × samples) 第targ_i的所有block脑电数据
// 滤波后输出： filtered_data(blocks，channels × samples)
// block平均得到： trains(targets, fbs × channels × samples)
// trca求跨试次协方差得到 w_tmp, 进而得到 W(fbs × targets × channels, 1)

TRCAModel train_trca(const Eigen::MatrixXd &eeg, double fs, int num_fbs, int num_targs)
{
    if (num_fbs <= 0)
        num_fbs = 3;

    if (num_targs <= 0) {
        std::cerr << "Invalid num_targs in train_trca: " << num_targs << std::endl;
        return TRCAModel{Eigen::MatrixXd(), Eigen::MatrixXd(), num_fbs, fs, 0};
    }

    const Eigen::Index num_blocks = eeg.rows();
    const int num_chans = 9;
    const Eigen::Index stride = num_targs * num_chans;
    if (stride <= 0 || eeg.cols() % stride != 0) {
        std::cerr << "Invalid eeg shape in train_trca, cols=" << eeg.cols()
                  << " stride=" << stride << std::endl;
        return TRCAModel{Eigen::MatrixXd(), Eigen::MatrixXd(), num_fbs, fs, num_targs};
    }
    const Eigen::Index num_smpls = eeg.cols() / stride;

    // 初始化输出
    Eigen::MatrixXd trains = Eigen::MatrixXd::Zero(num_targs, num_fbs * num_chans * num_smpls);
    Eigen::MatrixXd W = Eigen::MatrixXd::Zero(num_fbs * num_targs * num_chans, 1);

    // 创建 eeg_tmp, 用于存储所有 block 中 targ_i 目标的数据
    Eigen::MatrixXd eeg_tmp(num_blocks, num_chans * num_smpls);

    // 对每个目标进行处理
    for (int targ_i = 0; targ_i < num_targs; ++targ_i)
    {
        // 遍历所有 blocks，将 targ_i 的数据提取到 eeg_tmp
        for (Eigen::Index block = 0; block < num_blocks; block++)
        {
            for (int chan = 0; chan < num_chans; chan++)
            {
                for (Eigen::Index smpl = 0; smpl < num_smpls; smpl++)
                {
                    // 源数据索引：target + num_targs * channel + num_targs * num_chans * sample
                    const Eigen::Index src_idx = targ_i + num_targs * chan + num_targs * num_chans * smpl;
                    // 目标索引：channel × samples + sample
                    const Eigen::Index dst_idx = chan * num_smpls + smpl;
                    eeg_tmp(block, dst_idx) = eeg(block, src_idx);
                }
            }
        }

        // 对每个频带进行处理
        for (int fb_i = 0; fb_i < num_fbs; ++fb_i)
        {
            // 每个子带都从原始eeg_tmp独立滤波（工程在线实现语义）
            Eigen::MatrixXd filtered_data = filterbankTrials(eeg_tmp, fs, num_chans, fb_i + 1);

            // 不使用滤波器
            // Eigen::MatrixXd filtered_data = eeg_tmp;

            /*
            // 在if条件中添加文件存在检查
            if (fb_i == 0 && targ_i == 0 && !file_exists("filtered_data.txt"))
            {
                // 导出第一个block第一个通道的数据
                std::ofstream verification_file("filtered_data.txt");
                if (verification_file.is_open())
                {
                    verification_file << std::fixed << std::setprecision(6);
                    verification_file << "filtered_data: block=0, target= 0, channel=0\n";
                    // 第一个通道的样本起始位置是0
                    const Eigen::Index chan_start = 0; // chan=0
                    for (Eigen::Index smpl = 0; smpl < num_smpls; smpl++)
                    {
                        verification_file << "Sample[" << smpl << "] = " << filtered_data(0, chan_start + smpl) << "\n";
                    }
                    verification_file.close();
                }
            }
            */

            // 存储训练模板 (对所有block数据进行平均)
            // 计算 block 均值并存入 trains
            for (int chan = 0; chan < num_chans; chan++)
            {
                Eigen::VectorXd avg_data = Eigen::VectorXd::Zero(num_smpls);
                for (Eigen::Index block = 0; block < num_blocks; block++)
                {
                    avg_data += filtered_data.row(block).segment(chan * num_smpls, num_smpls);
                }
                avg_data /= static_cast<double>(num_blocks);

                // 直接存入 trains
                Eigen::Index train_start = fb_i * num_chans * num_smpls + chan * num_smpls;
                trains.row(targ_i).segment(train_start, num_smpls) = avg_data;

                /*
                // 在if条件中添加文件存在检查
                if (fb_i == 0 && targ_i == 0 && chan == 0 && !file_exists("trains_data.txt"))
                {
                    // 导出第一个block第一个通道的数据
                    std::ofstream verification_file("trains_data.txt");
                    if (verification_file.is_open())
                    {
                        verification_file << std::fixed << std::setprecision(6);
                        verification_file << "trains_data: target= 0, fb_i = 0, channel=0\n";
                        // 第一个通道的样本起始位置是0
                        const Eigen::Index chan_start = 0; // chan=0
                        for (Eigen::Index smpl = 0; smpl < num_smpls; smpl++)
                        {
                            verification_file << "Sample[" << smpl << "] = " << trains(0, chan_start + smpl) << "\n";
                        }
                        verification_file.close();
                    }
                }
                */
            }

            // 计算TRCA权重
            Eigen::MatrixXd w_tmp = trca(filtered_data); // W(fbs × targets × channels, 1)
     
            /*
            if (fb_i == 0 && targ_i == 0 && !file_exists("w_tmp.txt"))
            {
                // 导出第一个block第一个通道的数据
                std::ofstream verification_file("w_tmp.txt");
                if (verification_file.is_open())
                {
                    verification_file << std::fixed << std::setprecision(6);
                    verification_file << "w_tmp: fb=0, target= 0\n";
                    for (int i = 0; i < num_chans; i++)
                    {
                        verification_file << "Sample[" << i << "] = " << w_tmp(i) << "\n";
                    }
                    verification_file.close();
                }
            }
            */

            // 存储权重向量
            const Eigen::Index weight_start = fb_i * num_targs * num_chans + targ_i * num_chans;
            for (int chan = 0; chan < num_chans; chan++)
            {
                W(weight_start + chan, 0) = w_tmp(chan, 0);
            }
        }
    }

    return TRCAModel{trains, W, num_fbs, fs, num_targs};
}
