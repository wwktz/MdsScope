#pragma once

#include "mdsscope_app.h"
#include <functional>

using LoadedSignalCallback = std::function<void(const LoadedSignal&)>;

QVector<LoadedSignal> fetchMdsSignals(const LayoutConfig& snapshot,
                                      DataReadMode readMode,
                                      LoadedSignalCallback callback = {});
void warmMdsConnections(const LayoutConfig& snapshot);
SignalSeries fetchMdsSignal(const PlotSpec& plot, const SignalSpec& sig, DataReadMode readMode);
void clearMdsCurrentThreadConnections();
