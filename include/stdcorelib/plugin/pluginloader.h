// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGIN_PLUGINLOADER_H
#define STDCORELIB_PLUGIN_PLUGINLOADER_H

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <stdcorelib/stdc_global.h>
#include <stdcorelib/support/json.h>

#include <stdcorelib/plugin/plugin.h>

namespace stdc::plugin {

    /// \addtogroup plugin
    /// @{

    class PluginFactory;

    /// Reads, loads, and owns one plugin library.
    ///
    /// A loader is created for every plugin the factory finds, including the ones it cannot use. A
    /// plugin whose manifest is malformed, or whose library refuses to load, keeps its loader and
    /// answers \c hasError(), so the reason can be reported instead of the plugin quietly not
    /// being there.
    ///
    /// Metadata is read from the library without executing its code. Nothing is loaded until
    /// \c load() is called, so installed plugins can be described without pulling in the
    /// libraries they depend on.
    class STDC_PLUGIN_EXPORT PluginLoader {
    public:
        PluginLoader();
        explicit PluginLoader(const std::filesystem::path &filePath,
                              const std::optional<std::filesystem::path> &metadataPath = {});
        PluginLoader(Plugin *plugin, const json::Value &metadata);
        ~PluginLoader();

        PluginLoader(PluginLoader &&RHS) noexcept;
        PluginLoader &operator=(PluginLoader &&RHS) noexcept;

        enum Origin {
            /// A shared library on the filesystem.
            FileSystem,
            /// A plugin linked into the program.
            Static,
            /// A live instance supplied by the program.
            Runtime,
        };

        enum State {
            /// No plugin has been selected.
            Null,
            /// The manifest could not be read. Only \c errorMessage() is meaningful.
            Invalid,
            /// The metadata has been read. Everything but \c plugin() is meaningful.
            Read,
            /// The metadata was read, but the plugin could not be loaded.
            LoadFailed,
            /// The library is loaded and \c plugin() is live.
            Loaded,
        };

    public:
        /// Selects another plugin library and reads its metadata without loading its code.
        ///
        /// An already loaded library is unloaded first.
        ///
        /// \param filePath The plugin library to select.
        /// \param metadataPath An external metadata JSON file, or empty to read metadata embedded
        ///                     in the plugin library.
        void setFilePath(const std::filesystem::path &filePath,
                         const std::optional<std::filesystem::path> &metadataPath = {});

        /// Selects a plugin instance that the program already owns.
        ///
        /// \param plugin The live plugin instance. Ownership stays with the caller.
        /// \param metadata The plugin metadata, including its \c iid.
        void setPlugin(Plugin *plugin, const json::Value &metadata);

        State state() const;
        Origin origin() const;
        bool hasError() const;

        /// Why this plugin is unusable, empty if it is not.
        const std::string &errorMessage() const;

    public:
        /// The extension point this plugin plugs into, such as \c org.foo.bar.
        ///
        /// \note This is the only part of the manifest the factory interprets. Whoever owns the
        ///       extension point decides what the rest of it means.
        const std::string &iid() const;

        /// The shared library, or empty for a static or runtime plugin.
        const std::filesystem::path &filePath() const;

        /// What this plugin says about itself, in whatever shape its extension point defines.
        ///
        /// The library never looks inside.
        const json::Value &metadata() const;

    public:
        /// Loads the library on first call, returning whether \c plugin() is now live.
        ///
        /// \note Why it failed is on \c errorMessage(), which is where it has to be anyway for a
        ///       plugin that is installed but unusable to be able to say so.
        bool load();

        /// Unloads the library, invalidating the pointer returned by \c plugin().
        bool unload();

        bool isLoaded() const;

        /// The loaded instance, or null while \c state() is below \c Loaded.
        Plugin *plugin() const;

    public:
        static std::vector<std::string> staticPluginSets();
        static std::vector<StaticPlugin> staticPlugins(std::string_view iid);

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;

        friend class PluginFactory;

        PluginLoader(const PluginLoader &) = delete;
        PluginLoader &operator=(const PluginLoader &) = delete;
    };

    /// @}

}

#endif // STDCORELIB_PLUGIN_PLUGINLOADER_H
