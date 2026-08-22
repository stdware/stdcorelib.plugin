// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGINSYSTEM_PLUGINSETTINGS_H
#define STDCORELIB_PLUGINSYSTEM_PLUGINSETTINGS_H

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <stdcorelib/support/json.h>

#include <stdcorelib/stdc_plugin_global.h>

namespace stdc::pluginsystem {

    /// One source of plugin enabled-state overrides, keyed by stable plugin ID.
    class STDC_PLUGIN_EXPORT PluginSettings {
    public:
        /// Replaces the enabled-state override for \a id.
        ///
        /// \pre \a id is not empty.
        /// \param id The stable plugin identifier to update.
        /// \param enabled The new override, or nothing to remove the existing override.
        void setPluginEnabled(std::string id, std::optional<bool> enabled);

        /// Returns the explicit override for \a id, or nothing when a lower-priority source
        /// decides its state.
        std::optional<bool> pluginEnabled(std::string_view id) const;

        std::vector<std::string> enabledPlugins() const;
        std::vector<std::string> disabledPlugins() const;

        /// Serializes the overrides as sorted \c enabled and \c disabled ID arrays.
        json::Value toJson() const;

        /// Reads overrides from \c enabled and \c disabled ID arrays.
        ///
        /// Unknown plugin IDs are retained. Duplicate, empty, non-string, or conflicting IDs are
        /// rejected.
        /// \param value The JSON object to read.
        /// \param errorMessage Receives why the value was rejected, or is cleared on success.
        static std::optional<PluginSettings> fromJson(const json::Value &value,
                                                      std::string *errorMessage = nullptr);

    private:
        std::map<std::string, bool, std::less<>> _overrides;
    };

}

#endif // STDCORELIB_PLUGINSYSTEM_PLUGINSETTINGS_H
