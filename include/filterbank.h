#ifndef FILTERBANK_H
#define FILTERBANK_H

#include <Eigen/Dense>

// One epoch: rows are channels and columns are time samples.
Eigen::MatrixXd filterbankEpoch(const Eigen::MatrixXd& eeg, double fs, int fb_idx = 1);

// Multiple trials: each row is one trial, laid out as
// [channel0 samples, channel1 samples, ...].
Eigen::MatrixXd filterbankTrials(const Eigen::MatrixXd& eeg,
                                 double fs,
                                 int numChannels,
                                 int fb_idx = 1);

#endif // FILTERBANK_H
