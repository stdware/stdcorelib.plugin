// SPDX-License-Identifier: MIT

#include "plugindependency.h"

#include <utility>

namespace stdc::pluginsystem {

    PluginDependency::PluginDependency(std::string id, VersionNumber version, Type type)
        : _id(std::move(id)), _version(std::move(version)), _type(type) {
    }

    const std::string &PluginDependency::id() const {
        return _id;
    }

    const VersionNumber &PluginDependency::version() const {
        return _version;
    }

    PluginDependency::Type PluginDependency::type() const {
        return _type;
    }

}
