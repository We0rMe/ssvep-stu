#include "ModelDecoder.h"
#include "SSVEPOnlineThread.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

bool ModelDecoder::load(const QString& packageDir, QString& error)
{
    m_packageDir.clear();
    QFile file(QDir(packageDir).filePath("config.json"));
    if (!file.open(QIODevice::ReadOnly)) { error = QStringLiteral("找不到模型配置文件"); return false; }
    const auto config = QJsonDocument::fromJson(file.readAll()).object();
    if (config.value("num_targs").toInt(40) != 40 || config.value("num_chans").toInt(9) != 9 ||
        config.value("sampling_rate_raw").toDouble(1000) != 1000) {
        error = QStringLiteral("请选择 40 目标、9 通道、1000 Hz 的纯脑电模型"); return false;
    }
    SSVEPOnlineThread engine;
    if (!engine.setModelPackage(packageDir, error)) return false;
    m_packageDir = packageDir;
    return true;
}
