#pragma once
#include <array>
#include "SSVEPOfflineThread.h"
struct StimulusTarget { int id; double frequency; double phase; };
inline std::array<StimulusTarget, 40> makeTargets()
{
    std::array<StimulusTarget, 40> targets{};
    const auto protocol = SSVEPOfflineThread::createDefaultJFPMStimuli();
    for (int i = 0; i < 40; ++i)
        targets[static_cast<size_t>(i)] = {protocol[i].targetId, protocol[i].frequency, protocol[i].phase};
    return targets;
}
