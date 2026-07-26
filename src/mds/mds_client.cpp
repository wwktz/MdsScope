// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mds_ip_client.hpp"

QVector<LoadedSignal> fetchMdsSignals(const LayoutConfig& snapshot,
                                      DataReadMode readMode,
                                      LoadedSignalCallback callback,
                                      std::shared_ptr<std::atomic_bool> cancel,
                                      bool preserveConnectionsOnCancel)
{
    mds_client_internal::MdsIpClient client(readMode,
                                            std::move(callback),
                                            std::move(cancel),
                                            preserveConnectionsOnCancel);
    return client.fetchAll(snapshot);
}

void warmMdsConnections(const LayoutConfig& snapshot, std::shared_ptr<std::atomic_bool> cancel)
{
    mds_client_internal::MdsIpClient client(DataReadMode::Thin, {}, std::move(cancel));
    client.warmConnections(snapshot);
}

void shutdownMdsConnectionWorkers()
{
    mds_client_internal::MdsIpClient::shutdownWorkers();
}
