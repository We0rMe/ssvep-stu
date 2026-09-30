#pragma once

#include <QMetaType>
#include <QMap>
#include <QString>

// 定义通道数量
#define EEG_CHANNEL_COUNT 64

// 定义电极ID枚举
enum Electrode_ID {
    Fp1 = 0, Fpz, Fp2, F7, F3, Fz, F4, F8,
    FC5 = 8, FC1, FC2, FC6, M1, T7, C3, Cz,
    C4 = 16, T8, M2, CP5, CP1, CP2, CP6, P7,
    P3 = 24, Pz, P4, P8, POz, O1, O2, EOG,
    AF7 = 32, AF3, AF4, AF8, F5, F1, F2, F6,
    FC3 = 40, FCz, FC4, C5, C1, C2, C6, CP3,
    CP4 = 48, P5, P1, P2, P6, PO5, PO3, PO4,
    PO6 = 56, FT7, FT8, TP7, TP8, PO7, PO8, Oz,
    CPz = 64, GND
};

// 定义电极映射
const QMap<int, QString> electrodeMap = QMap<int, QString>({
    {Fp1, "Fp1"}, {Fpz, "Fpz"}, {Fp2, "Fp2"}, {F7, "F7"}, {F3, "F3"},
    {Fz, "Fz"}, {F4, "F4"}, {F8, "F8"}, {FC5, "FC5"}, {FC1, "FC1"},
    {FC2, "FC2"}, {FC6, "FC6"}, {M1, "M1"}, {T7, "T7"}, {C3, "C3"},
    {Cz, "Cz"}, {C4, "C4"}, {T8, "T8"}, {M2, "M2"}, {CP5, "CP5"},
    {CP1, "CP1"}, {CP2, "CP2"}, {CP6, "CP6"}, {P7, "P7"}, {P3, "P3"},
    {Pz, "Pz"}, {P4, "P4"}, {P8, "P8"}, {POz, "POz"}, {O1, "O1"},
    {O2, "O2"}, {EOG, "EOG"}, {AF7, "AF7"}, {AF3, "AF3"}, {AF4, "AF4"},
    {AF8, "AF8"}, {F5, "F5"}, {F1, "F1"}, {F2, "F2"}, {F6, "F6"},
    {FC3, "FC3"}, {FCz, "FCz"}, {FC4, "FC4"}, {C5, "C5"}, {C1, "C1"},
    {C2, "C2"}, {C6, "C6"}, {CP3, "CP3"}, {CP4, "CP4"}, {P5, "P5"},
    {P1, "P1"}, {P2, "P2"}, {P6, "P6"}, {PO5, "PO5"}, {PO3, "PO3"},
    {PO4, "PO4"}, {PO6, "PO6"}, {FT7, "FT7"}, {FT8, "FT8"}, {TP7, "TP7"},
    {TP8, "TP8"}, {PO7, "PO7"}, {PO8, "PO8"}, {Oz, "Oz"},{CPz, "CPz"}, {GND, "GND"}
});

// 定义EEG电压值结构体
typedef struct _EEG_VOLTAGE
{
    double data[EEG_CHANNEL_COUNT];
} EEG_VOLTAGE;

Q_DECLARE_METATYPE(EEG_VOLTAGE)

// 定义EEG数据包结构体
typedef struct _EEG_PACKET
{
    EEG_VOLTAGE voltage;  // 电压值
    uint64_t timestamp;   // 时间戳
} EEG_PACKET;

Q_DECLARE_METATYPE(EEG_PACKET)

// 标记事件结构体
struct EventMarker {
    int code;              // 事件代码
    qint64 timestamp;      // 时间戳
    QString description;   // 事件描述
};

// 让Qt元对象系统识别此类型
Q_DECLARE_METATYPE(EventMarker)