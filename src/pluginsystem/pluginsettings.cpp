// SPDX-License-Identifier: MIT

#include "pluginsettings.h"

#include <utility>

#include <stdcorelib/stlextra/algorithms.h>

namespace stdc::pluginsystem {

    void PluginSettings::setPluginEnabled(std::string id, std::optional<bool> enabled) {
        if (enabled) {
            _overrides[std::move(id)] = *enabled;
        } else if (auto it = _overrides.find(id); it != _overrides.end()) {
            _overrides.erase(it);
        }
    }

    std::optional<bool> PluginSettings::pluginEnabled(std::string_view id) const {
        auto it = _overrides.find(id);
        return it == _overrides.end() ? std::optional<bool>() : it->second;
    }

    std::vector<std::string> PluginSettings::enabledPlugins() const {
        std::vector<std::string> result;
        for (const auto &[id, enabled] : _overrides) {
            if (enabled) {
                result.push_back(id);
            }
        }
        return result;
    }

    std::vector<std::string> PluginSettings::disabledPlugins() const {
        std::vector<std::string> result;
        for (const auto &[id, enabled] : _overrides) {
            if (!enabled) {
                result.push_back(id);
            }
        }
        return result;
    }

    json::Value PluginSettings::toJson() const {
        json::Array enabled;
        json::Array disabled;
        for (const auto &[id, isEnabled] : _overrides) {
            (isEnabled ? enabled : disabled).emplace_back(id);
        }
        return json::Object{
            {"disabled", std::move(disabled)},
            {"enabled",  std::move(enabled) },
        };
    }

    std::optional<PluginSettings> PluginSettings::fromJson(const json::Value &value,
                                                           std::string *errorMessage) {
        const auto fail = [errorMessage](std::string message) -> std::optional<PluginSettings> {
            if (errorMessage) {
                *errorMessage = std::move(message);
            }
            return std::nullopt;
        };
        if (errorMessage) {
            errorMessage->clear();
        }
        if (!value.isObject()) {
            return fail("plugin settings are not a JSON object");
        }

        PluginSettings result;
        const auto readIds = [&](std::string_view field, bool enabled) {
            const auto &ids = value[field];
            if (ids.isNull()) {
                return true;
            }
            if (!ids.isArray()) {
                return false;
            }
            for (const auto &idValue : ids.toArray()) {
                if (!idValue.isString() || idValue.toString().empty()) {
                    return false;
                }
                const auto &id = idValue.toString();
                if (stdc::contains(result._overrides, id)) {
                    return false;
                }
                result._overrides.emplace(id, enabled);
            }
            return true;
        };

        if (!readIds("enabled", true)) {
            return fail("invalid or conflicting plugin ID in settings enabled array");
        }
        if (!readIds("disabled", false)) {
            return fail("invalid or conflicting plugin ID in settings disabled array");
        }
        return result;
    }

}
