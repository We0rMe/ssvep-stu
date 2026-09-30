#pragma once
#include <QString>

// Package validation delegates to the same loader used by the online engine.
class ModelDecoder {
public:
    bool load(const QString& packageDir, QString& error);
    QString packageDir() const { return m_packageDir; }
private:
    QString m_packageDir;
};
