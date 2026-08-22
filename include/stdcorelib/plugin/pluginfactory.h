// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGIN_PLUGINFACTORY_H
#define STDCORELIB_PLUGIN_PLUGINFACTORY_H

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <stdcorelib/adt/array_view.h>
#include <stdcorelib/stdc_global.h>

#include <stdcorelib/plugin/pluginloader.h>

namespace stdc::plugin {

    /// \addtogroup plugin
    /// @{

    class Plugin;

    /// Discovers plugins and owns their loaders.
    ///
    /// Filesystem plugins are discovered lazily by IID. Static plugins and instances supplied by
    /// the program can be added explicitly.
    ///
    /// Replacing the search paths discards unloaded filesystem plugins. Loaded plugins, static
    /// plugins, and runtime plugins stay in the factory. Programs should set all plugin paths
    /// before they first query or load plugins.
    class STDC_PLUGIN_EXPORT PluginFactory {
    public:
        PluginFactory();
        virtual ~PluginFactory();

        PluginFactory(PluginFactory &&RHS) noexcept;
        PluginFactory &operator=(PluginFactory &&RHS) noexcept;

    public:
        /// Adds the static plugins registered for the IID in \a pluginSet.
        ///
        /// Which extension point each one plugs into comes out of its manifest, exactly as it
        /// would for a plugin on disk.
        void addStaticPlugins(std::string_view pluginSet);

        /// Adds an instance the program already owns.
        ///
        /// \a manifest is the complete JSON object, including its \c iid. Ownership of \a plugin
        /// stays with the caller.
        void addRuntimePlugin(Plugin *plugin, const json::Value &manifest);

    public:
        /// Adds a directory to search for \a iid. Each library carrying an embedded manifest is one
        /// plugin.
        void addPluginPath(std::string_view iid, const std::filesystem::path &path);

        /// Replaces the directories searched for \a iid.
        ///
        /// This invalidates pointers to unloaded filesystem plugins previously returned by
        /// \c plugins(). Loaded filesystem plugins and explicitly added plugins remain valid.
        void setPluginPaths(std::string_view iid, array_view<std::filesystem::path> paths);
        std::vector<std::filesystem::path> pluginPaths(std::string_view iid) const;

    public:
        /// Everything found for \a iid, scanning the registered directories if they have not been
        /// scanned since they last changed.
        ///
        /// Nothing is loaded. A candidate without the requested \c iid is ignored. Once its IID
        /// has been read, later errors stay on its loader so the caller can report why that plugin
        /// is unusable.
        std::vector<PluginLoader *> plugins(std::string_view iid) const;

    protected:
        /// Finds candidate plugin paths under a registered search directory.
        ///
        /// The default implementation examines each library file directly under \a path and
        /// silently ignores files without an embedded plugin manifest.
        ///
        /// \param path The directory registered with \c addPluginPath().
        /// \param pluginPaths Receives the candidate paths to resolve.
        /// \return Whether the directory was scanned successfully.
        /// \warning This is called while the factory is locked. An override must not call any
        ///          function on this factory.
        virtual bool scanPluginPaths(const std::filesystem::path &path,
                                     std::vector<std::filesystem::path> *pluginPaths) const;

        /// Resolves a candidate into the paths passed to \c PluginLoader::setFilePath().
        ///
        /// The default implementation returns \a path unchanged and clears \a manifestPath, which
        /// makes the loader read the embedded manifest.
        ///
        /// \param path A candidate returned by \c scanPluginPaths().
        /// \param pluginPath Receives the plugin library path.
        /// \param manifestPath Receives an optional external manifest JSON path.
        /// \return Whether the candidate was resolved successfully.
        /// \warning This is called while the factory is locked. An override must not call any
        ///          function on this factory.
        virtual bool resolvePluginPath(const std::filesystem::path &path,
                                       std::filesystem::path *pluginPath,
                                       std::optional<std::filesystem::path> *manifestPath) const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;

        STDC_DISABLE_COPY(PluginFactory)
    };

    /// @}

}

#endif // STDCORELIB_PLUGIN_PLUGINFACTORY_H
