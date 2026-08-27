// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGINSYSTEM_PLUGINSYSTEM_H
#define STDCORELIB_PLUGINSYSTEM_PLUGINSYSTEM_H

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <stdcorelib/adt/array_view.h>

#include <stdcorelib/pluginsystem/iplugin.h>
#include <stdcorelib/pluginsystem/pluginsettings.h>
#include <stdcorelib/pluginsystem/pluginspec.h>

namespace stdc::plugin {

    class PluginFactory;

}

namespace stdc::pluginsystem {

    /// \addtogroup plugin
    /// @{

    /// Discovers and manages plugins implementing the PluginSystem lifecycle.
    class STDC_PLUGIN_EXPORT PluginSystem {
    public:
        /// How filesystem plugins are arranged below each search path.
        enum PluginLayout {
            /// Plugin libraries are directly below the search path and carry embedded metadata.
            Flat,
            /// Each child directory is a bundle containing a plugin library and external metadata.
            Bundle,
            /// Plugin discovery is provided by a custom PluginFactory.
            CustomLayout,
        };

        /// Selects whether a discovered plugin participates in loading and dependency resolution.
        using PluginLoadPredicate = std::function<bool(const PluginSpec &)>;

        /// Which plugin settings source to access.
        enum SettingsScope {
            /// System-wide settings that override plugin metadata defaults.
            Global,
            /// Per-user settings that override global settings.
            Local,
        };

        /// Creates a system that accepts only \a iid.
        ///
        /// \pre \a iid is not empty.
        /// \pre \a layout is \c Flat or \c Bundle.
        explicit PluginSystem(std::string_view iid, PluginLayout layout = Flat);

        /// Creates a system that discovers plugins through \a factory.
        ///
        /// The system takes ownership of \a factory and reports its layout as \c CustomLayout.
        /// Only filesystem plugins returned by the factory are accepted.
        ///
        /// \pre \a iid is not empty.
        /// \pre \a factory is not null and has not been used by another plugin system.
        PluginSystem(std::string_view iid, std::unique_ptr<plugin::PluginFactory> factory);
        ~PluginSystem();

        PluginSystem(PluginSystem &&RHS) noexcept;
        PluginSystem &operator=(PluginSystem &&RHS) noexcept;

    public:
        /// Returns the IID this system discovers plugins for.
        const std::string &iid() const;

        /// Returns the layout of the plugins this system discovers.
        PluginLayout pluginLayout() const;

        /// Replaces the filesystem search paths before loadPlugins() starts.
        ///
        /// This invalidates previously returned specs for unloaded plugins. Calls after
        /// loadPlugins() starts have no effect.
        void setPluginPaths(array_view<std::filesystem::path> paths);
        std::vector<std::filesystem::path> pluginPaths() const;

        /// Replaces enabled-state overrides in \a scope before loadPlugins() starts.
        ///
        /// Local settings override global settings, which override plugin metadata. Calls after
        /// loadPlugins() starts have no effect.
        void setPluginSettings(SettingsScope scope, PluginSettings settings);

        /// Replaces the predicate that selects plugins for loading.
        ///
        /// - The predicate is called once for each valid spec when loadPlugins() starts.
        /// - Returning false leaves the spec visible without treating it as an error.
        /// - Required dependencies on an unselected plugin are invalid.
        /// - Optional dependencies treat an unselected plugin as absent.
        /// - The predicate may make reentrant read-only queries on this system.
        /// - Calls after loading starts have no effect.
        /// - An empty predicate selects every plugin.
        void setPluginLoadPredicate(PluginLoadPredicate predicate);

        /// Returns the settings stored in \a scope.
        PluginSettings pluginSettings(SettingsScope scope) const;

        /// Returns every discovered spec.
        ///
        /// Before loadPlugins() starts, this scans the current search paths. The discovered set is
        /// frozen when loading starts. Replacing the paths before then invalidates pointers from
        /// earlier calls.
        std::vector<PluginSpec *> plugins() const;

        /// Loads and initializes every enabled, valid plugin once.
        ///
        /// Dependencies are loaded and initialized before their dependents. Errors remain on each
        /// PluginSpec.
        void loadPlugins();

        /// Shuts down and unloads plugins in reverse dependency order.
        ///
        /// Repeated calls and calls before loadPlugins() have no effect. The destructor calls this
        /// function automatically.
        void shutdownPlugins();

        /// Whether any plugin has an error.
        ///
        /// This function is thread-safe after loadPlugins() returns, except while
        /// shutdownPlugins() is running.
        bool hasError() const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;

        STDC_DISABLE_COPY(PluginSystem)
    };

    /// @}

}

#endif // STDCORELIB_PLUGINSYSTEM_PLUGINSYSTEM_H
