#ifndef TRAIN_TRCA_H
#define TRAIN_TRCA_H

#include <Eigen/Dense>

struct TRCAModel {
    Eigen::MatrixXd trains;
    Eigen::MatrixXd W;
    int num_fbs;
    double fs;
    int num_targs;
};

TRCAModel train_trca(const Eigen::MatrixXd& eeg, double fs, int num_fbs, int num_targs);
Eigen::VectorXi test_trca(const Eigen::RowVectorXd& test_data, const TRCAModel& model, bool is_ensemble);

#endif 