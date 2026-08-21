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

    /// User overrides for plugin enabled states, keyed by stable plugin ID.
    class STDC_PLUGIN_EXPORT PluginSettings {
    public:
        /// Records an explicit enabled state for \a id, replacing its previous override.
        ///
        /// \pre \a id is not empty.
        void setPluginEnabled(std::string id, bool enabled);

        /// Removes the override for \a id so that plugin metadata decides its state again.
        void resetPlugin(std::string_view id);

        /// Returns the explicit override for \a id, or nothing when metadata decides its state.
        std::optional<bool> pluginEnabled(std::string_view id) const;

        /// Applies the override for \a id to its metadata default.
        bool isPluginEnabled(std::string_view id, bool enabledByDefault) const;

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
