// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGINSYSTEM_PLUGINDEPENDENCY_H
#define STDCORELIB_PLUGINSYSTEM_PLUGINDEPENDENCY_H

#include <string>

#include <stdcorelib/support/versionnumber.h>

#include <stdcorelib/stdc_plugin_global.h>

namespace stdc::pluginsystem {

    /// One versioned dependency declared by a PluginSystem plugin.
    class STDC_PLUGIN_EXPORT PluginDependency {
    public:
        /// Whether failure to resolve this dependency disables the declaring plugin.
        enum Type {
            Required,
            Optional,
        };

        PluginDependency(std::string id, VersionNumber version, Type type = Required);

        /// The stable plugin identifier this dependency names.
        const std::string &id() const;

        /// The version requested by the declaring plugin.
        const VersionNumber &version() const;

        Type type() const;

    private:
        std::string _id;
        VersionNumber _version;
        Type _type;
    };

}

#endif // STDCORELIB_PLUGINSYSTEM_PLUGINDEPENDENCY_H
