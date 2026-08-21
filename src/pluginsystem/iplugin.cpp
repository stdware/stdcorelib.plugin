// SPDX-License-Identifier: MIT

#include "iplugin.h"

namespace stdc::pluginsystem {

    IPlugin::~IPlugin() = default;

    void IPlugin::pluginInitialized() {
    }

    void IPlugin::aboutToShutdown() {
    }

}
