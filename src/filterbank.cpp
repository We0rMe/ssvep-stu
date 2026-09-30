#include "filterbank.h"
#include "DspFilters/Dsp.h"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void setupFilter(Dsp::SimpleFilter<Dsp::ChebyshevI::BandPass<8>, 1>& filter,
                 double fs,
                 int& fb_idx)
{
    if (fb_idx < 1 || fb_idx > 10) {
        std::cerr << "Warning: Invalid filter index. Using default index (fb_idx = 1)" << std::endl;
        fb_idx = 1;
    }

    const std::vector<double> passband = {6, 14, 22, 30, 38, 46, 54, 62, 70, 78};
    const std::vector<double> stopband = {4, 10, 16, 24, 32, 40, 48, 56, 64, 72};
    const int idx = fb_idx - 1;

    const double fs_half = fs / 2.0;
    const double wp_low = passband[idx] / fs_half;
    const double wp_high = 90.0 / fs_half;
    const double ws_low = stopband[idx] / fs_half;
    const double ws_high = 100.0 / fs_half;

    (void)ws_low;
    (void)ws_high;

    filter.setup(8,
                 fs,
                 (wp_low + wp_high) * fs_half / 2.0,
                 (wp_high - wp_low) * fs_half,
                 0.5);

}

void filterSegment(Dsp::SimpleFilter<Dsp::ChebyshevI::BandPass<8>, 1>& filter,
                   Eigen::MatrixXd& output,
                   Eigen::Index row,
                   Eigen::Index start,
                   Eigen::Index count)
{
    std::vector<double> samples(static_cast<size_t>(count));
    for (Eigen::Index i = 0; i < count; ++i) {
        samples[static_cast<size_t>(i)] = output(row, start + i);
    }

    filter.reset();
    double* channelPointers[1] = {samples.data()};
    filter.process(static_cast<int>(count), channelPointers);

    for (Eigen::Index i = 0; i < count; ++i) {
        output(row, start + i) = samples[static_cast<size_t>(i)];
    }
}
}  // namespace

Eigen::MatrixXd filterbankEpoch(const Eigen::MatrixXd& eeg, double fs, int fb_idx)
{
    Dsp::SimpleFilter<Dsp::ChebyshevI::BandPass<8>, 1> filter;
    setupFilter(filter, fs, fb_idx);
    Eigen::MatrixXd y = eeg;

    for (Eigen::Index channel = 0; channel < y.rows(); ++channel) {
        filterSegment(filter, y, channel, 0, y.cols());
    }

    return y;
}

Eigen::MatrixXd filterbankTrials(const Eigen::MatrixXd& eeg,
                                 double fs,
                                 int numChannels,
                                 int fb_idx)
{
    if (numChannels <= 0 || eeg.cols() % numChannels != 0) {
        throw std::invalid_argument("filterbankTrials: columns must be divisible by numChannels");
    }

    Dsp::SimpleFilter<Dsp::ChebyshevI::BandPass<8>, 1> filter;
    setupFilter(filter, fs, fb_idx);
    Eigen::MatrixXd y = eeg;
    const Eigen::Index samplesPerChannel = y.cols() / numChannels;
    for (Eigen::Index trial = 0; trial < y.rows(); ++trial) {
        for (int channel = 0; channel < numChannels; ++channel) {
            filterSegment(filter,
                          y,
                          trial,
                          static_cast<Eigen::Index>(channel) * samplesPerChannel,
                          samplesPerChannel);
        }
    }

    return y;
}
