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

    /// Reads metadata and manages one plugin.
    ///
    /// Filesystem metadata is read without executing plugin code. The library is not loaded until
    /// \c load() is called.
    class STDC_PLUGIN_EXPORT PluginLoader {
    public:
        PluginLoader();
        explicit PluginLoader(const std::filesystem::path &filePath,
                              const std::optional<std::filesystem::path> &metadataPath = {});
        explicit PluginLoader(const StaticPlugin &plugin);
        PluginLoader(std::string_view iid, Plugin *plugin,
                     const json::Value &metadata = json::Object());
        ~PluginLoader();

        PluginLoader(PluginLoader &&RHS) noexcept;
        PluginLoader &operator=(PluginLoader &&RHS) noexcept;

        /// Where the selected plugin instance comes from.
        enum Origin {
            /// A dynamic plugin on the filesystem.
            FileSystem,
            /// A static plugin.
            Static,
            /// A live instance supplied by the program.
            Runtime,
        };

        /// The current metadata reading and plugin loading state.
        enum State {
            /// No plugin has been selected.
            Null,
            /// The IID or metadata could not be read or validated.
            Invalid,
            /// The metadata has been read, but no plugin instance is live.
            Read,
            /// The metadata was read, but the plugin could not be loaded.
            LoadFailed,
            /// The plugin instance is live.
            Loaded,
        };

    public:
        /// Selects another plugin library and reads its metadata without loading its code.
        ///
        /// An already loaded library is unloaded first.
        ///
        /// \param filePath The plugin library to select.
        /// \param metadataPath An external metadata JSON file, or empty to read user metadata
        ///                     embedded in the plugin library. The IID always comes from the
        ///                     library.
        void setFilePath(const std::filesystem::path &filePath,
                         const std::optional<std::filesystem::path> &metadataPath = {});

        /// Selects a statically registered plugin without creating its instance.
        ///
        /// \param plugin The registered plugin descriptor and metadata provider.
        void setStaticPlugin(const StaticPlugin &plugin);

        /// Selects a plugin instance that the program already owns.
        ///
        /// \param iid The extension point implemented by \a plugin.
        /// \param plugin The live plugin instance. Ownership stays with the caller.
        /// \param metadata The user metadata object.
        void setPlugin(std::string_view iid, Plugin *plugin,
                       const json::Value &metadata = json::Object());

        State state() const;

        /// Where the selected plugin comes from.
        ///
        /// \note This has no meaning while \c state() is \c Null.
        Origin origin() const;

        /// Whether the loader currently holds an error.
        bool hasError() const;

        /// The current error message, or empty when \c hasError() is false.
        const std::string &errorMessage() const;

    public:
        /// The extension point this plugin implements, such as \c org.foo.bar.
        const std::string &iid() const;

        /// The dynamic plugin path, or empty for a static or runtime plugin.
        const std::filesystem::path &filePath() const;

        /// The complete user metadata object.
        ///
        /// \warning A later metadata consumer may reserve fields of its own. Supplying an
        ///          incompatible value for such a field is undefined behavior.
        const json::Value &metadata() const;

    public:
        /// Makes the selected plugin instance live.
        ///
        /// \return Whether \c plugin() is now non-null. On failure, \c errorMessage() contains the
        ///         reason.
        bool load();

        /// Unloads a filesystem plugin, invalidating the pointer returned by \c plugin().
        ///
        /// Returns true if the plugin is already unloaded. A loaded static or runtime plugin
        /// cannot be unloaded because its lifetime is not controlled by this loader, so calling
        /// this function on one returns false with the reason in \c errorMessage().
        bool unload();

        inline bool isLoaded() const {
            return state() == Loaded;
        }

        /// The loaded instance, or null unless \c state() is \c Loaded.
        Plugin *plugin() const;

    public:
        /// Returns the IIDs that have static plugins registered for them.
        static std::vector<std::string> staticPluginSets();

        /// Returns the static plugins registered for \a iid.
        static std::vector<StaticPlugin> staticPlugins(std::string_view iid);

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;

        STDC_DISABLE_COPY(PluginLoader)
    };

    /// @}

}

#endif // STDCORELIB_PLUGIN_PLUGINLOADER_H
