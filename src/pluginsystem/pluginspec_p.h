// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGINSYSTEM_PLUGINSPEC_P_H
#define STDCORELIB_PLUGINSYSTEM_PLUGINSPEC_P_H

#include <stdcorelib/plugin/pluginloader.h>
#include <stdcorelib/pluginsystem/iplugin.h>
#include <stdcorelib/pluginsystem/pluginspec.h>

namespace stdc::pluginsystem {

    class PluginSpecData {
    public:
        explicit PluginSpecData(plugin::PluginLoader &loader);

        bool read();
        bool reportError(std::string message, PluginSpec::State errorState = PluginSpec::Invalid);

        PluginSpec spec;
        plugin::PluginLoader *loader;
        PluginSpec::State state = PluginSpec::Invalid;
        std::string errorMessage;
        std::string id;
        std::string name;
        VersionNumber version;
        VersionNumber compatVersion;
        std::vector<PluginDependency> dependencies;
        IPlugin *plugin = nullptr;
    };

}

#endif // STDCORELIB_PLUGINSYSTEM_PLUGINSPEC_P_H
