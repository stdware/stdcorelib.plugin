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

    /// One source of plugin enabled-state overrides and application-defined data.
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

        /// Returns the explicitly enabled plugin IDs in sorted order.
        std::vector<std::string> enabledPlugins() const;

        /// Returns the explicitly disabled plugin IDs in sorted order.
        std::vector<std::string> disabledPlugins() const;

        /// Returns the application-defined settings data.
        inline json::Object &userData();

        /// Returns the application-defined settings data.
        inline const json::Object &userData() const;

        /// Serializes the data and sorted \c enabledPlugins and \c disabledPlugins ID arrays.
        json::Value toJson() const;

        /// Reads application data and overrides from \c userData, \c enabledPlugins, and
        /// \c disabledPlugins.
        ///
        /// The \c userData field may be omitted, but must be an object when present. Unknown
        /// plugin IDs are retained. Duplicate, empty, non-string, or conflicting IDs are rejected.
        /// \param value The JSON object to read.
        /// \param errorMessage Receives why the value was rejected, or is cleared on success.
        static std::optional<PluginSettings> fromJson(const json::Value &value,
                                                      std::string *errorMessage = nullptr);

    private:
        std::map<std::string, bool, std::less<>> _overrides;
        json::Object _userData;
    };

    inline json::Object &PluginSettings::userData() {
        return _userData;
    }

    inline const json::Object &PluginSettings::userData() const {
        return _userData;
    }

}

#endif // STDCORELIB_PLUGINSYSTEM_PLUGINSETTINGS_H
