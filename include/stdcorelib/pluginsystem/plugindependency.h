// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGINSYSTEM_PLUGINDEPENDENCY_H
#define STDCORELIB_PLUGINSYSTEM_PLUGINDEPENDENCY_H

#include <string>
#include <utility>

#include <stdcorelib/support/versionnumber.h>

#include <stdcorelib/stdc_plugin_global.h>

namespace stdc::pluginsystem {

    /// One versioned dependency declared by a PluginSystem plugin.
    class STDC_PLUGIN_EXPORT PluginDependency {
    public:
        /// Whether failure to resolve this dependency disables the declaring plugin.
        enum Type {
            /// The declaring plugin cannot run without this dependency.
            Required,
            /// The declaring plugin can run without this dependency.
            Optional,
        };

        inline PluginDependency(std::string id, VersionNumber version, Type type = Required)
            : _id(std::move(id)), _version(std::move(version)), _type(type) {
        }

        /// The stable plugin identifier this dependency names.
        inline const std::string &id() const {
            return _id;
        }

        /// The version requested by the declaring plugin.
        inline const VersionNumber &version() const {
            return _version;
        }

        /// Whether this dependency is required or optional.
        inline Type type() const {
            return _type;
        }

    private:
        std::string _id;
        VersionNumber _version;
        Type _type;
    };

}

#endif // STDCORELIB_PLUGINSYSTEM_PLUGINDEPENDENCY_H
