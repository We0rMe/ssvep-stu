#ifndef SSVEP_PREPROCESS_H
#define SSVEP_PREPROCESS_H

#include <Eigen/Dense>

#include <string>

struct SSVEPPreprocessConfig {
    bool enable = true;
    double raw_fs = 1000.0;
    double window_len_s = 0.5;
    double target_fs = 250.0;
    double bp_low_hz = 7.0;
    double bp_high_hz = 70.0;
    bool enable_notch = true;
    double notch_hz = 50.0;
    double notch_q = 30.0;
};

bool preprocessSSVEPEpoch(Eigen::MatrixXd& epoch,
                          const SSVEPPreprocessConfig& config,
                          std::string& errorMessage,
                          double& effectiveFs);

#endif
