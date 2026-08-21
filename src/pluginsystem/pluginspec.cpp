// SPDX-License-Identifier: MIT

#include "pluginspec.h"
#include "pluginspec_p.h"

#include <utility>

namespace stdc::pluginsystem {

    PluginSpec::Impl::Impl(plugin::PluginLoader &pluginLoader) : loader(&pluginLoader) {
        read();
    }

    bool PluginSpec::Impl::read() {
        if (loader->hasError()) {
            return reportError(loader->errorMessage());
        }

        const auto &metadata = loader->metadata()["metadata"];
        if (!metadata.isObject()) {
            return reportError("missing or invalid metadata object");
        }

        const auto readString = [&metadata](std::string_view key, std::string *out) {
            const auto &value = metadata[key];
            if (!value.isString() || value.toString().empty()) {
                return false;
            }
            *out = value.toString();
            return true;
        };

        if (!readString("id", &id)) {
            return reportError("missing or invalid plugin id");
        }
        if (!readString("name", &name)) {
            return reportError("missing or invalid plugin name");
        }

        std::string versionString;
        if (!readString("version", &versionString)) {
            return reportError("missing or invalid plugin version");
        }
        auto parsedVersion = VersionNumber::fromString(versionString);
        if (!parsedVersion) {
            return reportError("invalid plugin version");
        }
        version = *parsedVersion;

        const auto &compatValue = metadata["compatVersion"];
        if (compatValue.isNull()) {
            compatVersion = version;
        } else if (compatValue.isString()) {
            auto parsedCompatVersion = VersionNumber::fromString(compatValue.toString());
            if (!parsedCompatVersion) {
                return reportError("invalid plugin compatibility version");
            }
            compatVersion = *parsedCompatVersion;
        } else {
            return reportError("invalid plugin compatibility version");
        }

        state = Read;
        return true;
    }

    bool PluginSpec::Impl::reportError(std::string message) {
        state = Invalid;
        errorMessage = std::move(message);
        return false;
    }

    PluginSpec::PluginSpec(plugin::PluginLoader &loader) : _impl(std::make_unique<Impl>(loader)) {
    }

    PluginSpec::~PluginSpec() = default;

    PluginSpec::PluginSpec(PluginSpec &&RHS) noexcept = default;

    PluginSpec &PluginSpec::operator=(PluginSpec &&RHS) noexcept = default;

    PluginSpec::State PluginSpec::state() const {
        return _impl->state;
    }

    bool PluginSpec::hasError() const {
        return !_impl->errorMessage.empty();
    }

    const std::string &PluginSpec::errorMessage() const {
        return _impl->errorMessage;
    }

    const std::string &PluginSpec::id() const {
        return _impl->id;
    }

    const std::string &PluginSpec::name() const {
        return _impl->name;
    }

    const VersionNumber &PluginSpec::version() const {
        return _impl->version;
    }

    const VersionNumber &PluginSpec::compatVersion() const {
        return _impl->compatVersion;
    }

    const std::filesystem::path &PluginSpec::filePath() const {
        return _impl->loader->filePath();
    }

}
