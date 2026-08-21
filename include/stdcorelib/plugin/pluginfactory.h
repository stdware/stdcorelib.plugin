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

    /// Finds plugins and hands out what is known about them.
    ///
    /// The factory knows three things about a plugin: which extension point it plugs into, where
    /// it lives, and the metadata blob it carries. It does not know what that blob means, and it
    /// must not learn, because the extension points are not its to enumerate. They are declared
    /// by whoever is being extended, in libraries this one has never heard of, so any rule baked
    /// in here would already be wrong for one of them.
    ///
    /// So the factory hands over the candidates and whoever owns the extension point chooses.
    ///
    /// Plugins reach it three ways:
    ///  - filesystem plugins: shared libraries with embedded manifests in a search directory
    ///  - static plugins    : linked into the program, handing over the same metadata directly
    ///  - runtime plugins   : instances the program supplies, owned by the program
    ///
    /// Changing the search paths only affects later scans. Loaders already discovered are kept,
    /// and a loaded plugin is not unloaded when the paths for its IID change. Programs should set
    /// all plugin paths before they first query or load plugins.
    class STDC_PLUGIN_EXPORT PluginFactory {
    public:
        PluginFactory();
        virtual ~PluginFactory();

        PluginFactory(PluginFactory &&RHS) noexcept;
        PluginFactory &operator=(PluginFactory &&RHS) noexcept;

    public:
        /// Takes the statically linked plugins registered under \a pluginSet into this factory.
        ///
        /// Which extension point each one plugs into comes out of its metadata, exactly as it
        /// would for a plugin on disk.
        void addStaticPlugins(std::string_view pluginSet);

        /// Takes an instance the program already holds. Ownership stays with the program.
        ///
        /// \a metadata says what a plugin.json would have said, \c iid included.
        void addRuntimePlugin(Plugin *plugin, const json::Value &metadata);

    public:
        /// Adds a directory to search for \a iid. Each library carrying embedded metadata is one
        /// plugin.
        void addPluginPath(std::string_view iid, const std::filesystem::path &path);
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
        /// silently ignores files without embedded plugin metadata.
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
        /// The default implementation returns \a path unchanged and clears \a metadataPath, which
        /// makes the loader read embedded metadata.
        ///
        /// \param path A candidate returned by \c scanPluginPaths().
        /// \param pluginPath Receives the plugin library path.
        /// \param metadataPath Receives an optional external metadata JSON path.
        /// \return Whether the candidate was resolved successfully.
        /// \warning This is called while the factory is locked. An override must not call any
        ///          function on this factory.
        virtual bool resolvePluginPath(const std::filesystem::path &path,
                                       std::filesystem::path *pluginPath,
                                       std::optional<std::filesystem::path> *metadataPath) const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;

        explicit PluginFactory(Impl &impl);

        STDC_DISABLE_COPY(PluginFactory)
    };

    /// @}

}

#endif // STDCORELIB_PLUGIN_PLUGINFACTORY_H
