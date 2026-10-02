#include "CsvWriter.h"
#include "EEGDataSaver.h"
#include <QDir>
#include <QFileInfo>

QStringList ssvepChannelNames()
{
    return {"Pz", "POz", "O1", "O2", "PO5", "PO3", "PO4", "PO6", "Oz"};
}

bool writeSessionCsv(const QString& path, const QList<EEG_PACKET>& packets,
                     const QList<EventMarker>& markers, QString& error)
{
    if (packets.isEmpty()) { error = QStringLiteral("没有采集到脑电数据"); return false; }
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) { error = QStringLiteral("无法创建数据目录"); return false; }
    EEGDataSaver saver;
    saver.setParadigm(ExperimentParadigm::SSVEP);
    QDir subjectDir = info.dir();
    subjectDir.cdUp();
    saver.setSubjectInfo(subjectDir.dirName(), info.dir().dirName());
    saver.setSamplingRate(1000);
    saver.setChannels(ssvepChannelNames());
    saver.setEEGData(packets);
    saver.setEventMarkers(markers);
    if (!saver.saveToCSV(path)) { error = QStringLiteral("数据写入失败，请检查文件路径及磁盘空间"); return false; }
    return true;
}
