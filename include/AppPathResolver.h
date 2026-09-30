#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

inline QDir resolveProjectRootDir()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 5; ++depth) {
        if (QFileInfo::exists(dir.filePath("CMakeLists.txt")) &&
            QFileInfo::exists(dir.filePath("include/Targets.h"))) return dir;
        if (!dir.cdUp()) break;
    }
    return QDir(QCoreApplication::applicationDirPath());
}
