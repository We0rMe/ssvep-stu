#pragma once

#include <QList>
#include <QString>
#include "EEG_DataStruct.h"

QStringList ssvepChannelNames();
bool writeSessionCsv(const QString& path, const QList<EEG_PACKET>& packets,
                     const QList<EventMarker>& markers, QString& error);
