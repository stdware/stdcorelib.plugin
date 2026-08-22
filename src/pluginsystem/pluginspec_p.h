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
        /// The raw \c metadata.enabledByDefault manifest value.
        bool enabledByManifest = true;
        /// The manifest value after the global settings override.
        bool enabledByGlobalSettings = true;
        /// The global result after the local settings override.
        bool enabled = true;
        /// The result of the host load predicate, frozen when loading starts.
        bool selectedForLoad = true;
        IPlugin *plugin = nullptr;
    };

}

#endif // STDCORELIB_PLUGINSYSTEM_PLUGINSPEC_P_H
