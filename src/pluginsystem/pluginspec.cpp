// SPDX-License-Identifier: MIT

#include "pluginspec.h"
#include "pluginspec_p.h"

#include <utility>

namespace stdc::pluginsystem {

    PluginSpecData::PluginSpecData(plugin::PluginLoader &pluginLoader)
        : spec(this), loader(&pluginLoader) {
        read();
    }

    bool PluginSpecData::read() {
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
        if (compatVersion > version) {
            return reportError("plugin compatibility version is newer than its version");
        }

        const auto &enabledValue = metadata["enabledByDefault"];
        if (!enabledValue.isNull()) {
            if (!enabledValue.isBool()) {
                return reportError("invalid plugin enabledByDefault value");
            }
            enabledByDefault = enabledValue.toBool();
            enabled = enabledByDefault;
        }

        const auto &dependencyValues = metadata["dependencies"];
        if (!dependencyValues.isNull()) {
            if (!dependencyValues.isArray()) {
                return reportError("invalid plugin dependencies");
            }
            for (const auto &value : dependencyValues.toArray()) {
                if (!value.isObject()) {
                    return reportError("invalid plugin dependency");
                }

                std::string dependencyId;
                const auto &idValue = value["id"];
                if (!idValue.isString() || idValue.toString().empty()) {
                    return reportError("missing or invalid dependency id");
                }
                dependencyId = idValue.toString();

                const auto &versionValue = value["version"];
                if (!versionValue.isString() || versionValue.toString().empty()) {
                    return reportError("missing or invalid dependency version");
                }
                auto dependencyVersion = VersionNumber::fromString(versionValue.toString());
                if (!dependencyVersion) {
                    return reportError("invalid dependency version");
                }

                const auto &typeValue = value["type"];
                if (!typeValue.isString()) {
                    return reportError("missing or invalid dependency type");
                }
                PluginDependency::Type type;
                if (typeValue.toString() == "required") {
                    type = PluginDependency::Required;
                } else if (typeValue.toString() == "optional") {
                    type = PluginDependency::Optional;
                } else {
                    return reportError("invalid dependency type");
                }

                dependencies.emplace_back(std::move(dependencyId), *dependencyVersion, type);
            }
        }

        state = PluginSpec::Read;
        return true;
    }

    bool PluginSpecData::reportError(std::string message, PluginSpec::State errorState) {
        state = errorState;
        errorMessage = std::move(message);
        return false;
    }

    PluginSpec::PluginSpec(PluginSpecData *data) : _data(data) {
    }

    PluginSpec::State PluginSpec::state() const {
        return _data->state;
    }

    bool PluginSpec::hasError() const {
        return !_data->errorMessage.empty();
    }

    const std::string &PluginSpec::errorMessage() const {
        return _data->errorMessage;
    }

    const std::string &PluginSpec::id() const {
        return _data->id;
    }

    const std::string &PluginSpec::name() const {
        return _data->name;
    }

    const VersionNumber &PluginSpec::version() const {
        return _data->version;
    }

    const VersionNumber &PluginSpec::compatVersion() const {
        return _data->compatVersion;
    }

    const std::vector<PluginDependency> &PluginSpec::dependencies() const {
        return _data->dependencies;
    }

    bool PluginSpec::enabledByDefault() const {
        return _data->enabledByDefault;
    }

    bool PluginSpec::isEnabled() const {
        return _data->enabled;
    }

    const std::filesystem::path &PluginSpec::filePath() const {
        return _data->loader->filePath();
    }

}
