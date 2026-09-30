#ifndef SSVEP_MODEL_TRAINER_H
#define SSVEP_MODEL_TRAINER_H

#include "train_trca.h"

#include <functional>
#include <string>
#include <vector>

struct SSVEPTrainingConfig {
    double fs = 1000.0;
    double len_gaze_s = 0.5;
    double len_delay_s = 0.13;
    double len_shift_s = 1.0;
    bool enable_preprocess = true;
    double preprocess_fs = 250.0;
    double preprocess_bp_low_hz = 7.0;
    double preprocess_bp_high_hz = 70.0;
    bool preprocess_notch_enable = true;
    double preprocess_notch_hz = 50.0;
    double preprocess_notch_q = 30.0;
    int num_fbs = 5;
    int num_targs = 40;
    int num_chans = 9;
    bool is_ensemble = true;
};

struct SSVEPFoldMetric {
    double accuracy = 0.0;
    double itr = 0.0;
};

struct SSVEPTrainingReport {
    std::string csv_path;
    std::string subject_id;
    int num_blocks = 0;
    int num_smpls = 0;
    std::vector<SSVEPFoldMetric> folds;
    double mean_accuracy = 0.0;
    double mean_itr = 0.0;
    TRCAModel model;
    std::string output_pkg_dir;
};

class SSVEPModelTrainer {
public:
    using ProgressCallback = std::function<void(int, const std::string&)>;

    bool trainFromCsv(const std::string& csvPath,
                      const std::string& subjectId,
                      const SSVEPTrainingConfig& config,
                      SSVEPTrainingReport& report,
                      std::string& errorMessage,
                      const ProgressCallback& progressCallback = ProgressCallback()) const;

    bool exportSegmentedFromCsv(const std::string& csvPath,
                                const std::string& subjectId,
                                const SSVEPTrainingConfig& config,
                                std::string& errorMessage,
                                const ProgressCallback& progressCallback = ProgressCallback()) const;
};

#endif
