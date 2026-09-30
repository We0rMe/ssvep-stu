#include "SSVEPPreprocess.h"

#include <DspFilters/Dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <vector>

namespace {

constexpr double kMatlabCoeffFs = 1000.0;
constexpr double kMatlabNotchHz = 50.0;
constexpr double kMatlabNotchQ = 30.0;
constexpr double kMatlabBpLowHz = 7.0;
constexpr double kMatlabBpHighHz = 70.0;

// Exported from MATLAB:
// fs=1000;
// [bN,aN]=iirnotch(50/(fs/2),(50/(fs/2))/30);
// bp=designfilt('bandpassiir','FilterOrder',4,'HalfPowerFrequency1',7,'HalfPowerFrequency2',70,'SampleRate',fs);
// [bB,aB]=tf(bp);
const std::vector<double> kMatlabNotchB = {
    0.994791237659376937,
   -1.89220537785854237,
    0.994791237659376937
};
const std::vector<double> kMatlabNotchA = {
    1.0,
   -1.89220537785854237,
    0.989582475318753874
};

const std::vector<double> kMatlabBandB = {
    0.0303804335477254328,
    0.0,
   -0.0607608670954508656,
    0.0,
    0.0303804335477254328
};
const std::vector<double> kMatlabBandA = {
    1.0,
   -3.41624621056108335,
    4.41269183947098576,
   -2.56768149891193698,
    0.571525151502637385
};

// Equivalent SOS representation for MATLAB bandpass design.
const std::array<std::array<double, 6>, 2> kMatlabBandSos = {{
    {{1.0,  2.0, 1.0, 1.0, -1.4760765414114259, 0.60646461601781176}},
    {{1.0, -2.0, 1.0, 1.0, -1.9401696691496575, 0.94238828846339806}}
}};
constexpr double kMatlabBandSosGain = 0.030380433547725433;

// Exported from MATLAB:
// [~,b]=resample(randn(2000,1),1,4);
// length = 81, group delay = 40 samples at input rate.
const std::vector<double> kMatlabResampleB_1_4 = {
   -3.57991155196923968e-19,-0.000282649394796115284,-0.00052579196542946907,-0.00047541698372319328,
    9.27260273505974027e-19,0.000731628527240471994,0.00125463273086795501,0.001063079766713776,
   -1.74728057609040158e-18,-0.00148301296094592952,-0.00244770849279972273,-0.00200653136531637327,
    2.80355925724935603e-18,0.00265127251920136342,0.00427878875724402915,0.00343848976765793752,
   -4.04598809559075826e-18,-0.00439479131278935984,-0.0069958192527395293,-0.00555497139192494185,
    5.39120766187575425e-18,0.00696672908988022182,0.0110128710249590665,0.00869881368759027954,
   -6.73063978364704892e-18,-0.0108557716105287076,-0.0171721337464203413,-0.0136063727671956285,
    7.94324573649579508e-18,0.0172430846995437481,0.0276474325182313602,0.0223208371330115458,
   -8.91116413941195178e-18,-0.030033587538636726,-0.0504711057057638385,-0.0434944357428851264,
    9.53578335605530041e-18,0.0741357049010938074,0.158369021719968334,0.224908142752552165,
    0.250159141272278063,0.224908142752552165,0.158369021719968334,0.0741357049010938074,
    9.53578335605530041e-18,-0.0434944357428851264,-0.0504711057057638385,-0.030033587538636726,
   -8.91116413941195178e-18,0.0223208371330115458,0.0276474325182313602,0.0172430846995437481,
    7.94324573649579508e-18,-0.0136063727671956285,-0.0171721337464203413,-0.0108557716105287076,
   -6.73063978364704892e-18,0.00869881368759027954,0.0110128710249590665,0.00696672908988022182,
    5.39120766187575425e-18,-0.00555497139192494185,-0.0069958192527395293,-0.00439479131278935984,
   -4.04598809559075826e-18,0.00343848976765793752,0.00427878875724402915,0.00265127251920136342,
    2.80355925724935603e-18,-0.00200653136531637327,-0.00244770849279972273,-0.00148301296094592952,
   -1.74728057609040158e-18,0.001063079766713776,0.00125463273086795501,0.000731628527240471994,
    9.27260273505974027e-19,-0.00047541698372319328,-0.00052579196542946907,-0.000282649394796115284,
   -3.57991155196923968e-19
};

bool approxEqual(double a, double b, double eps = 1e-9)
{
    return std::fabs(a - b) <= eps;
}

void applyIirBaOnePass(std::vector<double>& samples,
                       const std::vector<double>& b,
                       const std::vector<double>& a)
{
    if (samples.empty() || b.empty() || a.empty()) {
        return;
    }

    const int order = static_cast<int>(std::max(b.size(), a.size())) - 1;
    if (order <= 0) {
        return;
    }

    const double a0 = a[0];
    if (a0 == 0.0) {
        return;
    }

    std::vector<double> bN(order + 1, 0.0);
    std::vector<double> aN(order + 1, 0.0);
    for (size_t i = 0; i < b.size(); ++i) {
        bN[static_cast<int>(i)] = b[i] / a0;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        aN[static_cast<int>(i)] = a[i] / a0;
    }

    std::vector<double> xHist(order, 0.0);
    std::vector<double> yHist(order, 0.0);

    for (size_t n = 0; n < samples.size(); ++n) {
        const double x = samples[n];
        double y = bN[0] * x;

        for (int i = 1; i <= order; ++i) {
            y += bN[i] * xHist[i - 1];
            y -= aN[i] * yHist[i - 1];
        }

        for (int i = order - 1; i >= 1; --i) {
            xHist[i] = xHist[i - 1];
            yHist[i] = yHist[i - 1];
        }
        xHist[0] = x;
        yHist[0] = y;
        samples[n] = y;
    }
}

void applyBiquadOnePass(std::vector<double>& samples,
                        double b0,
                        double b1,
                        double b2,
                        double a0,
                        double a1,
                        double a2,
                        bool useSteadyStateInit)
{
    if (samples.empty() || a0 == 0.0) {
        return;
    }

    const double b0n = b0 / a0;
    const double b1n = b1 / a0;
    const double b2n = b2 / a0;
    const double a1n = a1 / a0;
    const double a2n = a2 / a0;

    double z1 = 0.0;
    double z2 = 0.0;
    if (useSteadyStateInit) {
        // Steady-state zi for transposed direct-form II under unit-step input.
        const double B1 = b1n - a1n * b0n;
        const double B2 = b2n - a2n * b0n;
        const double det = 1.0 + a1n + a2n;
        if (std::fabs(det) > 1e-12) {
            const double zi1 = (B1 + B2) / det;
            const double zi2 = ((1.0 + a1n) * B2 - a2n * B1) / det;
            const double x0 = samples.front();
            z1 = zi1 * x0;
            z2 = zi2 * x0;
        }
    }

    for (size_t i = 0; i < samples.size(); ++i) {
        const double x = samples[i];
        const double y = b0n * x + z1;
        z1 = b1n * x - a1n * y + z2;
        z2 = b2n * x - a2n * y;
        samples[i] = y;
    }
}

void applySosCascadeOnePass(std::vector<double>& samples,
                            const std::array<std::array<double, 6>, 2>& sos,
                            double gain)
{
    if (samples.empty()) {
        return;
    }

    if (gain != 1.0) {
        for (size_t i = 0; i < samples.size(); ++i) {
            samples[i] *= gain;
        }
    }

    for (size_t s = 0; s < sos.size(); ++s) {
        const double b0 = sos[s][0];
        const double b1 = sos[s][1];
        const double b2 = sos[s][2];
        const double a0 = sos[s][3];
        const double a1 = sos[s][4];
        const double a2 = sos[s][5];
        if (a0 == 0.0) {
            continue;
        }

        applyBiquadOnePass(samples, b0, b1, b2, a0, a1, a2, true);
    }
}

bool resampleDownBy4MatlabEquivalent(const std::vector<double>& in,
                                     int outSamples,
                                     std::vector<double>& out)
{
    const int n = static_cast<int>(in.size());
    const int L = static_cast<int>(kMatlabResampleB_1_4.size());
    const int delay = (L - 1) / 2; // 40

    if (n <= 0 || outSamples <= 0) {
        return false;
    }

    // Equivalent to MATLAB: z = filter(b,1,[x; zeros(L-1,1)]).
    std::vector<double> z(n + L - 1, 0.0);
    for (int k = 0; k < n + L - 1; ++k) {
        double acc = 0.0;
        const int i0 = std::max(0, k - (L - 1));
        const int i1 = std::min(k, n - 1);
        for (int i = i0; i <= i1; ++i) {
            const int bi = k - i;
            acc += kMatlabResampleB_1_4[bi] * in[i];
        }
        z[k] = acc;
    }

    out.assign(outSamples, 0.0);
    const int start = delay; // 0-based index, MATLAB start=41 in 1-based.
    for (int j = 0; j < outSamples; ++j) {
        const int idx = start + 4 * j;
        if (idx < static_cast<int>(z.size())) {
            out[j] = z[idx];
        }
    }
    return true;
}

bool computeDecimationFactor(double rawFs,
                             double targetFs,
                             int& factor,
                             std::string& errorMessage)
{
    if (rawFs <= 0.0 || targetFs <= 0.0) {
        errorMessage = "采样率配置无效";
        return false;
    }
    if (targetFs > rawFs) {
        errorMessage = "预处理采样率不能高于原始采样率";
        return false;
    }

    const double ratio = rawFs / targetFs;
    const int rounded = static_cast<int>(std::round(ratio));
    if (rounded <= 0) {
        errorMessage = "降采样因子无效";
        return false;
    }

    if (std::fabs(ratio - static_cast<double>(rounded)) > 1e-6) {
        std::ostringstream oss;
        oss << "原始采样率与目标采样率不成整数倍: fs=" << rawFs << " target=" << targetFs;
        errorMessage = oss.str();
        return false;
    }

    factor = rounded;
    return true;
}

void applyIirBandpassOnePass(std::vector<double>& samples,
                             double fs,
                             double lowHz,
                             double highHz)
{
    if (approxEqual(fs, kMatlabCoeffFs) &&
        approxEqual(lowHz, kMatlabBpLowHz) &&
        approxEqual(highHz, kMatlabBpHighHz)) {
        applySosCascadeOnePass(samples, kMatlabBandSos, kMatlabBandSosGain);
        return;
    }

    // Match MATLAB designfilt('bandpassiir','FilterOrder',4,...) more closely.
    Dsp::SimpleFilter<Dsp::Butterworth::BandPass<4>, 1> filter;
    filter.setup(4,
                 fs,
                 (lowHz + highHz) * 0.5,
                 (highHz - lowHz));

    filter.reset();
    double* channelPointers[1] = {samples.data()};
    filter.process(static_cast<int>(samples.size()), channelPointers);
}

void applyIirLowpassOnePass(std::vector<double>& samples,
                            double fs,
                            double cutoffHz)
{
    Dsp::SimpleFilter<Dsp::ChebyshevI::LowPass<8>, 1> filter;
    filter.setup(8,
                 fs,
                 cutoffHz,
                 0.5);

    filter.reset();
    double* channelPointers[1] = {samples.data()};
    filter.process(static_cast<int>(samples.size()), channelPointers);
}

void applyNotchBiquadOnePass(std::vector<double>& samples,
                             double fs,
                             double notchHz,
                             double qFactor)
{
    if (approxEqual(fs, kMatlabCoeffFs) &&
        approxEqual(notchHz, kMatlabNotchHz) &&
        approxEqual(qFactor, kMatlabNotchQ)) {
        applyBiquadOnePass(samples,
                           kMatlabNotchB[0],
                           kMatlabNotchB[1],
                           kMatlabNotchB[2],
                           kMatlabNotchA[0],
                           kMatlabNotchA[1],
                           kMatlabNotchA[2],
                           true);
        return;
    }

    const double pi = 3.14159265358979323846;
    const double w0 = 2.0 * pi * notchHz / fs;
    const double cosw0 = std::cos(w0);
    const double sinw0 = std::sin(w0);
    const double alpha = sinw0 / (2.0 * qFactor);

    const double b0 = 1.0;
    const double b1 = -2.0 * cosw0;
    const double b2 = 1.0;
    const double a0 = 1.0 + alpha;
    const double a1 = -2.0 * cosw0;
    const double a2 = 1.0 - alpha;

    applyBiquadOnePass(samples, b0, b1, b2, a0, a1, a2, true);
}

bool buildOddMirrorPadding(const std::vector<double>& signal,
                           int padLen,
                           std::vector<double>& padded,
                           std::string& errorMessage)
{
    const int n = static_cast<int>(signal.size());
    if (n < 3) {
        errorMessage = "信号长度过短，无法进行镜像padding";
        return false;
    }
    if (padLen <= 0 || padLen >= n - 1) {
        errorMessage = "镜像padding长度无效";
        return false;
    }

    padded.resize(n + 2 * padLen);

    for (int i = 0; i < padLen; ++i) {
        const int src = padLen - i;
        padded[i] = 2.0 * signal.front() - signal[src];
    }

    for (int i = 0; i < n; ++i) {
        padded[padLen + i] = signal[i];
    }

    for (int i = 0; i < padLen; ++i) {
        const int src = n - 2 - i;
        padded[padLen + n + i] = 2.0 * signal.back() - signal[src];
    }

    return true;
}

bool zeroPhaseBandpass(std::vector<double>& signal,
                       double fs,
                       double lowHz,
                       double highHz,
                       std::string& errorMessage)
{
    const int n = static_cast<int>(signal.size());
    const int padLen = std::min(24, n - 2);
    if (padLen < 1) {
        errorMessage = "信号长度不足，无法进行零相位带通滤波";
        return false;
    }

    std::vector<double> padded;
    if (!buildOddMirrorPadding(signal, padLen, padded, errorMessage)) {
        return false;
    }

    applyIirBandpassOnePass(padded, fs, lowHz, highHz);
    std::reverse(padded.begin(), padded.end());
    applyIirBandpassOnePass(padded, fs, lowHz, highHz);
    std::reverse(padded.begin(), padded.end());

    for (int i = 0; i < n; ++i) {
        signal[i] = padded[padLen + i];
    }

    return true;
}

bool zeroPhaseLowpass(std::vector<double>& signal,
                      double fs,
                      double cutoffHz,
                      std::string& errorMessage)
{
    const int n = static_cast<int>(signal.size());
    const int padLen = std::min(24, n - 2);
    if (padLen < 1) {
        errorMessage = "信号长度不足，无法进行零相位低通滤波";
        return false;
    }

    std::vector<double> padded;
    if (!buildOddMirrorPadding(signal, padLen, padded, errorMessage)) {
        return false;
    }

    applyIirLowpassOnePass(padded, fs, cutoffHz);
    std::reverse(padded.begin(), padded.end());
    applyIirLowpassOnePass(padded, fs, cutoffHz);
    std::reverse(padded.begin(), padded.end());

    for (int i = 0; i < n; ++i) {
        signal[i] = padded[padLen + i];
    }

    return true;
}

bool zeroPhaseNotch(std::vector<double>& signal,
                    double fs,
                    double notchHz,
                    double qFactor,
                    std::string& errorMessage)
{
    const int n = static_cast<int>(signal.size());
    const int padLen = std::min(24, n - 2);
    if (padLen < 1) {
        errorMessage = "信号长度不足，无法进行零相位陷波滤波";
        return false;
    }

    std::vector<double> padded;
    if (!buildOddMirrorPadding(signal, padLen, padded, errorMessage)) {
        return false;
    }

    applyNotchBiquadOnePass(padded, fs, notchHz, qFactor);
    std::reverse(padded.begin(), padded.end());
    applyNotchBiquadOnePass(padded, fs, notchHz, qFactor);
    std::reverse(padded.begin(), padded.end());

    for (int i = 0; i < n; ++i) {
        signal[i] = padded[padLen + i];
    }

    return true;
}

} // namespace

bool preprocessSSVEPEpoch(Eigen::MatrixXd& epoch,
                          const SSVEPPreprocessConfig& config,
                          std::string& errorMessage,
                          double& effectiveFs)
{
    if (!config.enable) {
        effectiveFs = config.raw_fs;
        return true;
    }

    if (config.bp_low_hz <= 0.0 ||
        config.bp_high_hz <= config.bp_low_hz ||
        config.bp_high_hz >= (config.target_fs * 0.5)) {
        errorMessage = "预处理带通参数无效";
        return false;
    }

    if (config.enable_notch) {
        if (config.notch_hz <= 0.0 ||
            config.notch_hz >= (config.target_fs * 0.5) ||
            config.notch_q <= 0.0) {
            errorMessage = "预处理陷波参数无效";
            return false;
        }
    }

    int decimation = 1;
    if (!computeDecimationFactor(config.raw_fs, config.target_fs, decimation, errorMessage)) {
        return false;
    }

    const int inSamples = epoch.cols();
    const int outSamples = static_cast<int>(std::round(config.window_len_s * config.target_fs));
    if (outSamples <= 0) {
        errorMessage = "预处理后的样本数无效";
        return false;
    }
    if (inSamples < (outSamples - 1) * decimation + 1) {
        errorMessage = "降采样后样本长度不足";
        return false;
    }

    const bool useMatlabResample14 =
        (decimation == 4) &&
        approxEqual(config.raw_fs, kMatlabCoeffFs) &&
        approxEqual(config.target_fs, 250.0);

    const double antiAliasCutoffHz = 0.45 * config.target_fs;
    if (!useMatlabResample14) {
        if (antiAliasCutoffHz <= 0.0 || antiAliasCutoffHz >= (config.raw_fs * 0.5)) {
            errorMessage = "抗混叠低通截止频率无效";
            return false;
        }
    }

    Eigen::MatrixXd downsampled(epoch.rows(), outSamples);

    for (int ch = 0; ch < downsampled.rows(); ++ch) {
        std::vector<double> rawSignal(inSamples, 0.0);
        for (int i = 0; i < inSamples; ++i) {
            rawSignal[i] = epoch(ch, i);
        }

        if (config.enable_notch) {
            if (!zeroPhaseNotch(rawSignal,
                                config.raw_fs,
                                config.notch_hz,
                                config.notch_q,
                                errorMessage)) {
                return false;
            }
        }

        if (!zeroPhaseBandpass(rawSignal,
                               config.raw_fs,
                               config.bp_low_hz,
                               config.bp_high_hz,
                               errorMessage)) {
            return false;
        }

        std::vector<double> signal(outSamples, 0.0);
        if (useMatlabResample14) {
            if (!resampleDownBy4MatlabEquivalent(rawSignal, outSamples, signal)) {
                errorMessage = "MATLAB等价降采样失败";
                return false;
            }
        } else {
            // Fallback: anti-aliasing low-pass + decimation.
            if (decimation > 1) {
                if (!zeroPhaseLowpass(rawSignal,
                                      config.raw_fs,
                                      antiAliasCutoffHz,
                                      errorMessage)) {
                    return false;
                }
            }
            for (int i = 0; i < outSamples; ++i) {
                signal[i] = rawSignal[i * decimation];
            }
        }

        for (int i = 0; i < outSamples; ++i) {
            downsampled(ch, i) = signal[i];
        }
    }

    epoch = downsampled;
    effectiveFs = config.target_fs;
    return true;
}
