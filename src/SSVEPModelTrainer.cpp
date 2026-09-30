#include "SSVEPModelTrainer.h"
#include "SSVEPPreprocess.h"

#include "itr.h"

#include <QDateTime>
#include <QDir>
#include <QCoreApplication>
#include "AppPathResolver.h"

#include <Eigen/Dense>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void reportProgress(const SSVEPModelTrainer::ProgressCallback& progressCallback,
                    int percent,
                    const std::string& message)
{
    if (!progressCallback) {
        return;
    }

    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }

    progressCallback(percent, message);
}

struct EventAtSample {
    int sampleIndex;
    int code;
};

struct ParsedCsv {
    Eigen::MatrixXd channels;
    std::vector<EventAtSample> events;
    int numChannels = 0;
};

struct TrialWindow {
    int startSample = -1;
};

bool startsWith(const std::string& s, const std::string& prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string trim(const std::string& s)
{
    size_t left = 0;
    while (left < s.size() && std::isspace(static_cast<unsigned char>(s[left]))) {
        ++left;
    }

    size_t right = s.size();
    while (right > left && std::isspace(static_cast<unsigned char>(s[right - 1]))) {
        --right;
    }

    return s.substr(left, right - left);
}

std::vector<std::string> splitCsvLine(const std::string& line)
{
    std::vector<std::string> fields;
    std::string token;
    std::stringstream ss(line);

    while (std::getline(ss, token, ',')) {
        fields.push_back(trim(token));
    }

    if (!line.empty() && line.back() == ',') {
        fields.push_back("");
    }

    return fields;
}

bool parseDoubleSafe(const std::string& s, double& outValue)
{
    if (s.empty()) {
        return false;
    }

    char* endPtr = nullptr;
    const double value = std::strtod(s.c_str(), &endPtr);
    if (endPtr == s.c_str() || *endPtr != '\0') {
        return false;
    }

    outValue = value;
    return true;
}

bool parseIntSafe(const std::string& s, int& outValue)
{
    if (s.empty()) {
        return false;
    }

    char* endPtr = nullptr;
    const long value = std::strtol(s.c_str(), &endPtr, 10);
    if (endPtr == s.c_str() || *endPtr != '\0') {
        return false;
    }

    outValue = static_cast<int>(value);
    return true;
}

bool parseCsvData(const std::string& csvPath,
                  int expectedChannels,
                  ParsedCsv& parsed,
                  std::string& errorMessage)
{
    std::ifstream in(csvPath.c_str());
    if (!in.is_open()) {
        errorMessage = "无法打开CSV文件: " + csvPath;
        return false;
    }

    std::string headerLine;
    if (!std::getline(in, headerLine)) {
        errorMessage = "CSV为空: " + csvPath;
        return false;
    }

    const std::vector<std::string> headers = splitCsvLine(headerLine);
    std::vector<int> channelIndices;
    std::vector<int> markerIndices;

    for (size_t i = 0; i < headers.size(); ++i) {
        const std::string& colName = headers[i];
        if (startsWith(colName, "Marker")) {
            markerIndices.push_back(static_cast<int>(i));
        } else if (colName != "TimePoint") {
            channelIndices.push_back(static_cast<int>(i));
        }
    }

    if (channelIndices.empty()) {
        errorMessage = "CSV中未找到通道列";
        return false;
    }

    if (expectedChannels > 0 && static_cast<int>(channelIndices.size()) != expectedChannels) {
        std::ostringstream oss;
        oss << "CSV通道数与配置不一致: csv=" << channelIndices.size() << " config=" << expectedChannels;
        errorMessage = oss.str();
        return false;
    }

    std::vector<std::vector<double> > channelRows;
    std::vector<EventAtSample> events;

    std::string line;
    int sampleIdx = 0;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }

        const std::vector<std::string> fields = splitCsvLine(line);

        std::vector<double> rowValues(channelIndices.size(), 0.0);
        for (size_t ch = 0; ch < channelIndices.size(); ++ch) {
            const int colIndex = channelIndices[ch];
            if (colIndex < static_cast<int>(fields.size())) {
                double value = 0.0;
                if (parseDoubleSafe(fields[colIndex], value)) {
                    rowValues[ch] = value;
                }
            }
        }
        channelRows.push_back(rowValues);

        for (size_t m = 0; m < markerIndices.size(); ++m) {
            const int colIndex = markerIndices[m];
            if (colIndex < static_cast<int>(fields.size())) {
                int code = 0;
                if (parseIntSafe(fields[colIndex], code) && code != 0) {
                    EventAtSample evt;
                    evt.sampleIndex = sampleIdx;
                    evt.code = code;
                    events.push_back(evt);
                }
            }
        }

        ++sampleIdx;
    }

    if (channelRows.empty()) {
        errorMessage = "CSV中没有有效采样数据";
        return false;
    }

    parsed.numChannels = static_cast<int>(channelIndices.size());
    parsed.channels = Eigen::MatrixXd::Zero(static_cast<int>(channelRows.size()), parsed.numChannels);
    for (int i = 0; i < static_cast<int>(channelRows.size()); ++i) {
        for (int ch = 0; ch < parsed.numChannels; ++ch) {
            parsed.channels(i, ch) = channelRows[i][ch];
        }
    }
    parsed.events = events;

    return true;
}

bool buildTrainingMatrix(const ParsedCsv& parsed,
                         const SSVEPTrainingConfig& config,
                         Eigen::MatrixXd& eegRaw,
                         Eigen::MatrixXd& eeg,
                         int& numBlocks,
                         int& numSmpls,
                         double& effectiveFs,
                         std::string& errorMessage)
{
    const int totalSamples = parsed.channels.rows();
    const int rawNumSmpls = static_cast<int>(std::round(config.len_gaze_s * config.fs));
    numSmpls = config.enable_preprocess
        ? static_cast<int>(std::round(config.len_gaze_s * config.preprocess_fs))
        : rawNumSmpls;
    const int delaySamples = static_cast<int>(std::round(config.len_delay_s * config.fs));

    if (numSmpls <= 0) {
        errorMessage = "训练窗样本数无效";
        return false;
    }

    std::vector<std::map<int, TrialWindow> > windows;
    int currentBlock = -1;
    int maxBlock = -1;

    for (size_t i = 0; i < parsed.events.size(); ++i) {
        const int code = parsed.events[i].code;
        const int sampleIdx = parsed.events[i].sampleIndex;

        if (code >= 1001 && code < 2000) {
            currentBlock = code - 1001;
            if (currentBlock > maxBlock) {
                maxBlock = currentBlock;
            }
            if (currentBlock >= static_cast<int>(windows.size())) {
                windows.resize(currentBlock + 1);
            }
            continue;
        }

        if (code >= 3001 && code < 4000) {
            if (currentBlock < 0) {
                errorMessage = "检测到FlickerStart但未找到对应Block起点";
                return false;
            }

            const int targetId = code - 3000;
            if (targetId < 1) {
                std::ostringstream oss;
                oss << "发现非法target标签: " << targetId;
                errorMessage = oss.str();
                return false;
            }

            if (currentBlock >= static_cast<int>(windows.size())) {
                windows.resize(currentBlock + 1);
            }

            std::map<int, TrialWindow>& blockWindows = windows[currentBlock];
            std::map<int, TrialWindow>::iterator it = blockWindows.find(targetId);
            if (it != blockWindows.end() && it->second.startSample >= 0) {
                std::ostringstream oss;
                oss << "Block " << (currentBlock + 1) << " target " << targetId << " 出现重复FlickerStart";
                errorMessage = oss.str();
                return false;
            }

            const int start = sampleIdx + delaySamples;
            const int endExclusive = start + rawNumSmpls;
            if (start < 0 || endExclusive > totalSamples) {
                std::ostringstream oss;
                oss << "Block " << (currentBlock + 1) << " target " << targetId << " 切片越界";
                errorMessage = oss.str();
                return false;
            }

            blockWindows[targetId].startSample = start;
        }
    }

    numBlocks = maxBlock + 1;
    if (numBlocks <= 1) {
        errorMessage = "可用block数量不足，至少需要2个block进行LOOCV";
        return false;
    }

    if (static_cast<int>(windows.size()) < numBlocks) {
        errorMessage = "block窗口解析不完整";
        return false;
    }

    std::vector<int> labelsSorted(config.num_targs, 0);
    for (int i = 0; i < config.num_targs; ++i) {
        labelsSorted[i] = i + 1;
    }

    for (int block = 0; block < numBlocks; ++block) {
        for (int classIdx = 0; classIdx < config.num_targs; ++classIdx) {
            const int label = labelsSorted[classIdx];
            std::map<int, TrialWindow>::const_iterator it = windows[block].find(label);
            if (it == windows[block].end() || it->second.startSample < 0) {
                std::ostringstream oss;
                oss << "Block " << (block + 1) << " 缺失 target(label=" << label << ") 的FlickerStart";
                errorMessage = oss.str();
                return false;
            }
        }
    }

    effectiveFs = config.enable_preprocess ? config.preprocess_fs : config.fs;
    eegRaw = Eigen::MatrixXd::Zero(numBlocks, config.num_targs * config.num_chans * rawNumSmpls);
    eeg = Eigen::MatrixXd::Zero(numBlocks, config.num_targs * config.num_chans * numSmpls);

    for (int block = 0; block < numBlocks; ++block) {
        for (int classIdx = 0; classIdx < config.num_targs; ++classIdx) {
            const int label = labelsSorted[classIdx];
            const int start = windows[block].find(label)->second.startSample;

            Eigen::MatrixXd epoch(config.num_chans, rawNumSmpls);
            for (int chan = 0; chan < config.num_chans; ++chan) {
                for (int smpl = 0; smpl < rawNumSmpls; ++smpl) {
                    epoch(chan, smpl) = parsed.channels(start + smpl, chan);
                    const int rawDstIndex = classIdx + config.num_targs * chan + config.num_targs * config.num_chans * smpl;
                    eegRaw(block, rawDstIndex) = epoch(chan, smpl);
                }
            }

            double epochFs = config.fs;
            SSVEPPreprocessConfig preprocessConfig;
            preprocessConfig.enable = config.enable_preprocess;
            preprocessConfig.raw_fs = config.fs;
            preprocessConfig.window_len_s = config.len_gaze_s;
            preprocessConfig.target_fs = config.preprocess_fs;
            preprocessConfig.bp_low_hz = config.preprocess_bp_low_hz;
            preprocessConfig.bp_high_hz = config.preprocess_bp_high_hz;
            preprocessConfig.enable_notch = config.preprocess_notch_enable;
            preprocessConfig.notch_hz = config.preprocess_notch_hz;
            preprocessConfig.notch_q = config.preprocess_notch_q;
            if (!preprocessSSVEPEpoch(epoch, preprocessConfig, errorMessage, epochFs)) {
                return false;
            }
            if (epoch.cols() != numSmpls) {
                errorMessage = "预处理后样本数与配置不一致";
                return false;
            }
            effectiveFs = epochFs;

            for (int chan = 0; chan < config.num_chans; ++chan) {
                for (int smpl = 0; smpl < numSmpls; ++smpl) {
                    const int dstIndex = classIdx + config.num_targs * chan + config.num_targs * config.num_chans * smpl;
                    eeg(block, dstIndex) = epoch(chan, smpl);
                }
            }
        }
    }

    return true;
}

bool writeMatrixCsv(const Eigen::MatrixXd& matrix,
                    const std::string& filePath,
                    std::string& errorMessage)
{
    std::ofstream out(filePath.c_str());
    if (!out.is_open()) {
        errorMessage = "无法写入CSV文件: " + filePath;
        return false;
    }

    out << std::setprecision(10);
    for (int r = 0; r < matrix.rows(); ++r) {
        for (int c = 0; c < matrix.cols(); ++c) {
            out << matrix(r, c);
            if (c + 1 < matrix.cols()) {
                out << ',';
            }
        }
        out << '\n';
    }

    if (!out.good()) {
        errorMessage = "写入CSV文件失败: " + filePath;
        return false;
    }

    return true;
}

bool saveAnalysisMatrices(const std::string& subjectId,
                         const Eigen::MatrixXd& eegRaw,
                         const Eigen::MatrixXd& eegProcessed,
                         double rawFs,
                         double modelFs,
                         int numTargs,
                         int numChans,
                         const std::string& sourceCsv,
                         std::string& errorMessage)
{
    QDir root = resolveProjectRootDir();

    const QString sourcePath = QDir::fromNativeSeparators(QString::fromStdString(sourceCsv)).toLower();
    QString sessionScope = "offline";
    if (sourcePath.contains("/online/")) {
        sessionScope = "online";
    } else if (sourcePath.contains("/offline/")) {
        sessionScope = "offline";
    }

    const QString analysisDir = root.filePath(
        QString("data/%1/%2/analysis")
            .arg(QString::fromStdString(subjectId))
            .arg(sessionScope));
    if (!QDir().mkpath(analysisDir)) {
        errorMessage = "无法创建分析数据目录: " + analysisDir.toStdString();
        return false;
    }

    const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    const QString rawPath = QDir(analysisDir).filePath(ts + "_segmented_raw.csv");
    const QString procPath = QDir(analysisDir).filePath(ts + "_segmented_preprocessed.csv");
    const QString metaPath = QDir(analysisDir).filePath(ts + "_segmented_meta.txt");

    if (!writeMatrixCsv(eegRaw, rawPath.toStdString(), errorMessage)) {
        return false;
    }
    if (!writeMatrixCsv(eegProcessed, procPath.toStdString(), errorMessage)) {
        return false;
    }

    std::ofstream meta(metaPath.toStdString().c_str());
    if (!meta.is_open()) {
        errorMessage = "无法写入分析元信息文件: " + metaPath.toStdString();
        return false;
    }

    meta << "source_csv=" << sourceCsv << "\n";
    meta << "num_blocks=" << eegRaw.rows() << "\n";
    meta << "num_targs=" << numTargs << "\n";
    meta << "num_chans=" << numChans << "\n";
    meta << "raw_rows=" << eegRaw.rows() << "\n";
    meta << "raw_cols=" << eegRaw.cols() << "\n";
    meta << "processed_rows=" << eegProcessed.rows() << "\n";
    meta << "processed_cols=" << eegProcessed.cols() << "\n";
    meta << "sampling_rate_raw=" << rawFs << "\n";
    meta << "sampling_rate_model=" << modelFs << "\n";
    meta << "layout=row-major blocks x (target + num_targs*chan + num_targs*num_chans*sample)\n";

    if (!meta.good()) {
        errorMessage = "写入分析元信息文件失败: " + metaPath.toStdString();
        return false;
    }

    return true;
}

bool writeBinaryMatrix(const Eigen::MatrixXd& matrix, const std::string& filePath, std::string& errorMessage)
{
    std::ofstream out(filePath.c_str(), std::ios::binary);
    if (!out.is_open()) {
        errorMessage = "无法写入二进制文件: " + filePath;
        return false;
    }

    const std::streamsize bytes = static_cast<std::streamsize>(matrix.size() * sizeof(double));
    out.write(reinterpret_cast<const char*>(matrix.data()), bytes);
    if (!out.good()) {
        errorMessage = "写入二进制文件失败: " + filePath;
        return false;
    }

    return true;
}

std::string normalizePath(const QString& path)
{
    return QDir::toNativeSeparators(path).toStdString();
}

bool savePackage(const SSVEPTrainingReport& report,
                 const SSVEPTrainingConfig& config,
                 std::string& errorMessage,
                 std::string& pkgDirOut)
{
    QDir root = resolveProjectRootDir();
    const QString modelDir = root.filePath(QString("model/%1").arg(QString::fromStdString(report.subject_id)));
    if (!QDir().mkpath(modelDir)) {
        errorMessage = "无法创建模型目录: " + modelDir.toStdString();
        return false;
    }

    const QString pkgName = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + "_trca_model.pkg";
    const QString pkgDir = QDir(modelDir).filePath(pkgName);
    if (!QDir().mkpath(pkgDir)) {
        errorMessage = "无法创建模型包目录: " + pkgDir.toStdString();
        return false;
    }

    const QString trainsPath = QDir(pkgDir).filePath("trains.bin");
    const QString wPath = QDir(pkgDir).filePath("W.bin");
    const QString configPath = QDir(pkgDir).filePath("config.json");
    const QString summaryPath = QDir(pkgDir).filePath("readable_summary.txt");

    if (!writeBinaryMatrix(report.model.trains, trainsPath.toStdString(), errorMessage)) {
        return false;
    }
    if (!writeBinaryMatrix(report.model.W, wPath.toStdString(), errorMessage)) {
        return false;
    }

    std::ofstream cfg(configPath.toStdString().c_str());
    if (!cfg.is_open()) {
        errorMessage = "无法写入配置文件: " + configPath.toStdString();
        return false;
    }

    cfg << std::fixed << std::setprecision(6);
    cfg << "{\n";
    cfg << "  \"version\": 1,\n";
    cfg << "  \"subject_id\": \"" << report.subject_id << "\",\n";
    cfg << "  \"csv_path\": \"" << report.csv_path << "\",\n";
    cfg << "  \"timestamp\": \"" << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toStdString() << "\",\n";
    cfg << "  \"sampling_rate_raw\": " << config.fs << ",\n";
    cfg << "  \"sampling_rate_model\": " << report.model.fs << ",\n";
    cfg << "  \"len_gaze_s\": " << config.len_gaze_s << ",\n";
    cfg << "  \"len_delay_s\": " << config.len_delay_s << ",\n";
    cfg << "  \"len_shift_s\": " << config.len_shift_s << ",\n";
    cfg << "  \"preprocess_enable\": " << (config.enable_preprocess ? "true" : "false") << ",\n";
    cfg << "  \"preprocess_bp_low_hz\": " << config.preprocess_bp_low_hz << ",\n";
    cfg << "  \"preprocess_bp_high_hz\": " << config.preprocess_bp_high_hz << ",\n";
    cfg << "  \"preprocess_notch_enable\": " << (config.preprocess_notch_enable ? "true" : "false") << ",\n";
    cfg << "  \"preprocess_notch_hz\": " << config.preprocess_notch_hz << ",\n";
    cfg << "  \"preprocess_notch_q\": " << config.preprocess_notch_q << ",\n";
    cfg << "  \"preprocess_implementation\": {\n";
    cfg << "    \"profile\": \"matlab_aligned_v2\",\n";
    cfg << "    \"zero_phase\": \"forward_backward\",\n";
    cfg << "    \"padding\": {\"mode\": \"odd_mirror\", \"pad_len_cap\": 24},\n";
    cfg << "    \"initial_state\": \"steady_state_zi_scaled_by_edge_sample\",\n";
    cfg << "    \"notch\": {\n";
    cfg << "      \"type\": \"fixed_biquad_ba\",\n";
    cfg << "      \"fs\": 1000.0,\n";
    cfg << "      \"hz\": 50.0,\n";
    cfg << "      \"q\": 30.0,\n";
    cfg << "      \"b\": [0.9947912376593769, -1.8922053778585424, 0.9947912376593769],\n";
    cfg << "      \"a\": [1.0, -1.8922053778585424, 0.9895824753187539]\n";
    cfg << "    },\n";
    cfg << "    \"bandpass\": {\n";
    cfg << "      \"type\": \"fixed_sos\",\n";
    cfg << "      \"fs\": 1000.0,\n";
    cfg << "      \"low_hz\": 7.0,\n";
    cfg << "      \"high_hz\": 70.0,\n";
    cfg << "      \"sos\": [[1.0, 2.0, 1.0, 1.0, -1.4760765414114259, 0.6064646160178118], [1.0, -2.0, 1.0, 1.0, -1.9401696691496575, 0.9423882884633981]],\n";
    cfg << "      \"gain\": 0.030380433547725433\n";
    cfg << "    },\n";
    cfg << "    \"downsample\": {\n";
    cfg << "      \"method\": \"fixed_fir_resample_1_over_4\",\n";
    cfg << "      \"factor\": 4,\n";
    cfg << "      \"fir_length\": 81,\n";
    cfg << "      \"group_delay_input_samples\": 40,\n";
    cfg << "      \"pick_start_1based\": 41\n";
    cfg << "    }\n";
    cfg << "  },\n";
    cfg << "  \"num_fbs\": " << config.num_fbs << ",\n";
    cfg << "  \"num_targs\": " << config.num_targs << ",\n";
    cfg << "  \"num_chans\": " << config.num_chans << ",\n";
    cfg << "  \"class_labels_canonical\": [";
    for (int i = 0; i < config.num_targs; ++i) {
        cfg << (i + 1);
        if (i + 1 < config.num_targs) {
            cfg << ", ";
        }
    }
    cfg << "],\n";
    cfg << "  \"num_blocks\": " << report.num_blocks << ",\n";
    cfg << "  \"num_smpls\": " << report.num_smpls << ",\n";
    cfg << "  \"mean_accuracy\": " << report.mean_accuracy << ",\n";
    cfg << "  \"mean_itr\": " << report.mean_itr << ",\n";
    cfg << "  \"storage\": {\n";
    cfg << "    \"dtype\": \"float64\",\n";
    cfg << "    \"endianness\": \"little\",\n";
    cfg << "    \"order\": \"column_major\",\n";
    cfg << "    \"trains\": {\"file\": \"trains.bin\", \"rows\": " << report.model.trains.rows() << ", \"cols\": " << report.model.trains.cols() << "},\n";
    cfg << "    \"W\": {\"file\": \"W.bin\", \"rows\": " << report.model.W.rows() << ", \"cols\": " << report.model.W.cols() << "}\n";
    cfg << "  },\n";
    cfg << "  \"fold_metrics\": [\n";

    for (size_t i = 0; i < report.folds.size(); ++i) {
        cfg << "    {\"fold\": " << (i + 1)
            << ", \"accuracy\": " << report.folds[i].accuracy
            << ", \"itr\": " << report.folds[i].itr << "}";
        if (i + 1 < report.folds.size()) {
            cfg << ",";
        }
        cfg << "\n";
    }

    cfg << "  ]\n";
    cfg << "}\n";

    std::ofstream summary(summaryPath.toStdString().c_str());
    if (!summary.is_open()) {
        errorMessage = "无法写入摘要文件: " + summaryPath.toStdString();
        return false;
    }

    summary << std::fixed << std::setprecision(4);
    summary << "SSVEP TRCA Offline Training Summary\n";
    summary << "Subject: " << report.subject_id << "\n";
    summary << "Input CSV: " << report.csv_path << "\n";
    summary << "Blocks: " << report.num_blocks << "\n";
    summary << "Targets: " << config.num_targs << "\n";
    summary << "Channels: " << config.num_chans << "\n";
    summary << "Samples per trial: " << report.num_smpls << "\n";
    summary << "Sampling rate (raw): " << config.fs << " Hz\n";
    summary << "Sampling rate (model): " << report.model.fs << " Hz\n";
        summary << "Preprocess: " << (config.enable_preprocess ? "ON" : "OFF") << "\n";
        summary << "Bandpass: " << config.preprocess_bp_low_hz << "-" << config.preprocess_bp_high_hz << " Hz\n";
        summary << "Notch: " << (config.preprocess_notch_enable ? "ON" : "OFF")
            << " (" << config.preprocess_notch_hz << " Hz, Q=" << config.preprocess_notch_q << ")\n";
    summary << "Preprocess implementation profile: matlab_aligned_v2\n";
    summary << "Zero-phase strategy: forward+backward IIR with odd-mirror padding (pad_len_cap=24)\n";
    summary << "IIR initial state: steady-state zi scaled by edge sample\n";
    summary << "Fixed notch (1000Hz, 50Hz, Q=30): b=[0.9947912376593769, -1.8922053778585424, 0.9947912376593769], a=[1.0, -1.8922053778585424, 0.9895824753187539]\n";
    summary << "Fixed bandpass SOS (7-70Hz@1000Hz): [[1,2,1,1,-1.4760765414114259,0.6064646160178118],[1,-2,1,1,-1.9401696691496575,0.9423882884633981]], gain=0.030380433547725433\n";
    summary << "Fixed downsample (1/4): FIR length=81, group_delay=40 input samples, pick_start=41 (1-based)\n";
    summary << "Mean Accuracy: " << report.mean_accuracy << " %\n";
    summary << "Mean ITR: " << report.mean_itr << " bpm\n";

    pkgDirOut = normalizePath(pkgDir);
    return true;
}

}  // namespace

bool SSVEPModelTrainer::trainFromCsv(const std::string& csvPath,
                                     const std::string& subjectId,
                                     const SSVEPTrainingConfig& config,
                                     SSVEPTrainingReport& report,
                                     std::string& errorMessage,
                                     const ProgressCallback& progressCallback) const
{
    reportProgress(progressCallback, 1, "正在解析CSV...");

    ParsedCsv parsed;
    if (!parseCsvData(csvPath, config.num_chans, parsed, errorMessage)) {
        return false;
    }

    reportProgress(progressCallback, 10, "CSV解析完成，正在构建训练矩阵...");

    Eigen::MatrixXd eegRaw;
    Eigen::MatrixXd eeg;
    int numBlocks = 0;
    int numSmpls = 0;
    double effectiveFs = config.fs;
    if (!buildTrainingMatrix(parsed,
                             config,
                             eegRaw,
                             eeg,
                             numBlocks,
                             numSmpls,
                             effectiveFs,
                             errorMessage)) {
        return false;
    }

    if (!saveAnalysisMatrices(subjectId,
                              eegRaw,
                              eeg,
                              config.fs,
                              effectiveFs,
                              config.num_targs,
                              config.num_chans,
                              csvPath,
                              errorMessage)) {
        return false;
    }

    reportProgress(progressCallback, 20, "训练矩阵构建完成，开始LOOCV评估...");

    std::vector<double> accs;
    std::vector<double> itrs;
    std::vector<SSVEPFoldMetric> folds;

    const double lenSel = config.len_gaze_s + config.len_shift_s;
    Eigen::VectorXi canonicalLabels(config.num_targs);
    for (int i = 0; i < config.num_targs; ++i) {
        canonicalLabels(i) = i + 1;
    }
    for (int loocv = 0; loocv < numBlocks; ++loocv) {
        Eigen::MatrixXd trainData(numBlocks - 1, eeg.cols());
        int row = 0;
        for (int b = 0; b < numBlocks; ++b) {
            if (b == loocv) {
                continue;
            }
            trainData.row(row++) = eeg.row(b);
        }

        const Eigen::RowVectorXd testData = eeg.row(loocv);
        const TRCAModel model = train_trca(trainData, effectiveFs, config.num_fbs, config.num_targs);
        const Eigen::VectorXi estimated = test_trca(testData, model, config.is_ensemble);

        const int correct = static_cast<int>((estimated.array() == canonicalLabels.array()).count());
        const double accuracy = 100.0 * static_cast<double>(correct) / static_cast<double>(config.num_targs);
        const double itrValue = itr(config.num_targs, accuracy / 100.0, lenSel);

        accs.push_back(accuracy);
        itrs.push_back(itrValue);
        SSVEPFoldMetric metric;
        metric.accuracy = accuracy;
        metric.itr = itrValue;
        folds.push_back(metric);

        const int loocvProgress = 20 + static_cast<int>((65.0 * (loocv + 1)) / numBlocks);
        std::ostringstream oss;
        oss << "LOOCV评估中 (" << (loocv + 1) << "/" << numBlocks << ")";
        reportProgress(progressCallback, loocvProgress, oss.str());
    }

    double meanAcc = 0.0;
    double meanItr = 0.0;
    for (size_t i = 0; i < accs.size(); ++i) {
        meanAcc += accs[i];
        meanItr += itrs[i];
    }
    meanAcc /= static_cast<double>(accs.size());
    meanItr /= static_cast<double>(itrs.size());

    reportProgress(progressCallback, 88, "LOOCV完成，正在训练最终模型...");
    const TRCAModel finalModel = train_trca(eeg, effectiveFs, config.num_fbs, config.num_targs);

    report.csv_path = csvPath;
    report.subject_id = subjectId;
    report.num_blocks = numBlocks;
    report.num_smpls = numSmpls;
    report.folds = folds;
    report.mean_accuracy = meanAcc;
    report.mean_itr = meanItr;
    report.model = finalModel;

    reportProgress(progressCallback, 95, "最终模型完成，正在写入模型包...");

    if (!savePackage(report, config, errorMessage, report.output_pkg_dir)) {
        return false;
    }

    reportProgress(progressCallback, 100, "训练完成");

    return true;
}

bool SSVEPModelTrainer::exportSegmentedFromCsv(const std::string& csvPath,
                                               const std::string& subjectId,
                                               const SSVEPTrainingConfig& config,
                                               std::string& errorMessage,
                                               const ProgressCallback& progressCallback) const
{
    reportProgress(progressCallback, 1, "正在解析CSV...");

    ParsedCsv parsed;
    if (!parseCsvData(csvPath, config.num_chans, parsed, errorMessage)) {
        return false;
    }

    reportProgress(progressCallback, 35, "CSV解析完成，正在构建分段矩阵...");

    Eigen::MatrixXd eegRaw;
    Eigen::MatrixXd eeg;
    int numBlocks = 0;
    int numSmpls = 0;
    double effectiveFs = config.fs;
    if (!buildTrainingMatrix(parsed,
                             config,
                             eegRaw,
                             eeg,
                             numBlocks,
                             numSmpls,
                             effectiveFs,
                             errorMessage)) {
        return false;
    }

    reportProgress(progressCallback, 80, "正在写入分段文件...");

    if (!saveAnalysisMatrices(subjectId,
                              eegRaw,
                              eeg,
                              config.fs,
                              effectiveFs,
                              config.num_targs,
                              config.num_chans,
                              csvPath,
                              errorMessage)) {
        return false;
    }

    reportProgress(progressCallback, 100, "分段文件导出完成");
    return true;
}
