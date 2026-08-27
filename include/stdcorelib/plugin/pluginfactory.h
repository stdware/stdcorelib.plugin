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
    class PluginCatalog;

    /// Discovers plugins and owns their loaders.
    ///
    /// Filesystem plugins are discovered lazily by IID. Static plugins and instances supplied by
    /// the program can be added explicitly.
    ///
    /// The interface is thread-safe.
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
        /// Adds the static plugins registered for \a iid.
        void addStaticPlugins(std::string_view iid);

        /// Adds an instance the program already owns.
        ///
        /// \a iid identifies the extension point. Ownership of \a plugin stays with the caller.
        void addRuntimePlugin(std::string_view iid, Plugin *plugin,
                              const json::Value &metadata = json::Object());

    public:
        /// Adds a directory to search for \a iid.
        ///
        /// Each library carrying embedded plugin metadata is one plugin.
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
        /// silently ignores files without embedded plugin metadata or whose IID differs from
        /// \a iid.
        ///
        /// \param iid The IID currently being discovered.
        /// \param path The directory registered with \c addPluginPath().
        /// \param pluginPaths Receives the candidate paths to resolve.
        /// \return Whether the directory was scanned successfully.
        /// \warning An override must not call any function on this factory.
        virtual bool scanPluginPaths(std::string_view iid, const std::filesystem::path &path,
                                     std::vector<std::filesystem::path> *pluginPaths) const;

        /// Resolves a candidate into the paths passed to \c PluginLoader::setFilePath().
        ///
        /// The default implementation returns \a path unchanged and clears \a metadataPath, which
        /// makes the loader use its embedded metadata.
        ///
        /// \param iid The IID currently being discovered.
        /// \param path A candidate returned by \c scanPluginPaths().
        /// \param pluginPath Receives the plugin library path.
        /// \param metadataPath Receives an optional external metadata JSON path.
        /// \return Whether the candidate was resolved successfully.
        /// \warning An override must not call any function on this factory.
        virtual bool resolvePluginPath(std::string_view iid, const std::filesystem::path &path,
                                       std::filesystem::path *pluginPath,
                                       std::optional<std::filesystem::path> *metadataPath) const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;

        explicit PluginFactory(std::unique_ptr<Impl> impl);

        friend class PluginCatalog;

        STDC_DISABLE_COPY(PluginFactory)
    };

    /// Discovers plugin bundles stored one per child directory.
    ///
    /// Each bundle has external metadata and a plugin library. The metadata's root \c name
    /// field gives the library's platform-independent name. For example, \c editor can resolve to
    /// \c editor.dll, \c libeditor.so, or \c libeditor.dylib.
    class STDC_PLUGIN_EXPORT BundlePluginFactory : public PluginFactory {
    public:
        /// Creates a bundle factory with a configurable metadata path.
        ///
        /// \param metadataFileName The metadata path relative to each bundle.
        ///
        /// \pre \a metadataFileName is nonempty, relative, and does not contain \c .. components.
        explicit BundlePluginFactory(std::filesystem::path metadataFileName = "plugin.json");
        ~BundlePluginFactory() override;

        BundlePluginFactory(BundlePluginFactory &&RHS) noexcept;
        BundlePluginFactory &operator=(BundlePluginFactory &&RHS) noexcept;

        /// The metadata path relative to each bundle.
        const std::filesystem::path &metadataFileName() const;

    protected:
        bool scanPluginPaths(std::string_view iid, const std::filesystem::path &path,
                             std::vector<std::filesystem::path> *pluginPaths) const override;
        bool resolvePluginPath(std::string_view iid, const std::filesystem::path &path,
                               std::filesystem::path *pluginPath,
                               std::optional<std::filesystem::path> *metadataPath) const override;

        /// Resolves the plugin library described by bundle metadata.
        ///
        /// The default implementation reads the root \c name field, searches the bundle root, and
        /// accepts the platform's library prefix and suffix around that name.
        ///
        /// \param bundlePath The bundle directory returned by scanPluginPaths().
        /// \param metadata The complete root object read from the bundle metadata file.
        /// \return The library path, or nothing when no matching library exists.
        virtual std::optional<std::filesystem::path>
            resolveLibraryPath(const std::filesystem::path &bundlePath,
                               const json::Value &metadata) const;

    private:
        class Impl;

        STDC_DISABLE_COPY(BundlePluginFactory)
    };

    /// @}

}

#endif // STDCORELIB_PLUGIN_PLUGINFACTORY_H
