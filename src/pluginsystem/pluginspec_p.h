// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGINSYSTEM_PLUGINSPEC_P_H
#define STDCORELIB_PLUGINSYSTEM_PLUGINSPEC_P_H

#include <stdcorelib/pluginsystem/pluginspec.h>

namespace stdc::pluginsystem {

    class PluginSpec::Impl {
    public:
        explicit Impl(plugin::PluginLoader &loader);

        bool read();
        bool reportError(std::string message);

        plugin::PluginLoader *loader;
        State state = Invalid;
        std::string errorMessage;
        std::string id;
        std::string name;
        VersionNumber version;
        VersionNumber compatVersion;
    };

}

#endif // STDCORELIB_PLUGINSYSTEM_PLUGINSPEC_P_H
