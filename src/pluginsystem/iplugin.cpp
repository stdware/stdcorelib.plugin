// SPDX-License-Identifier: MIT

#include "iplugin.h"

namespace stdc::pluginsystem {

    IPlugin::~IPlugin() = default;

    void IPlugin::pluginsInitialized() {
    }

    void IPlugin::aboutToShutdown() {
    }

    void IPlugin::remoteCommand(const std::vector<std::string> &, const std::filesystem::path &) {
    }

}
