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

    /// Reads a manifest and manages one plugin.
    ///
    /// A filesystem manifest is read without executing plugin code. The library is not loaded until
    /// \c load() is called.
    class STDC_PLUGIN_EXPORT PluginLoader {
    public:
        PluginLoader();
        explicit PluginLoader(const std::filesystem::path &filePath,
                              const std::optional<std::filesystem::path> &manifestPath = {});
        explicit PluginLoader(const StaticPlugin &plugin);
        PluginLoader(Plugin *plugin, const json::Value &manifest);
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

        /// The current manifest reading and plugin loading state.
        enum State {
            /// No plugin has been selected.
            Null,
            /// The manifest could not be read or validated.
            Invalid,
            /// The manifest has been read, but no plugin instance is live.
            Read,
            /// The manifest was read, but the plugin could not be loaded.
            LoadFailed,
            /// The plugin instance is live.
            Loaded,
        };

    public:
        /// Selects another plugin library and reads its manifest without loading its code.
        ///
        /// An already loaded library is unloaded first.
        ///
        /// \param filePath The plugin library to select.
        /// \param manifestPath An external manifest JSON file, or empty to read the manifest
        ///                     embedded in the plugin library.
        void setFilePath(const std::filesystem::path &filePath,
                         const std::optional<std::filesystem::path> &manifestPath = {});

        /// Selects a statically registered plugin without creating its instance.
        ///
        /// \param plugin The registered plugin descriptor and manifest provider.
        void setStaticPlugin(const StaticPlugin &plugin);

        /// Selects a plugin instance that the program already owns.
        ///
        /// \param plugin The live plugin instance. Ownership stays with the caller.
        /// \param manifest The complete plugin manifest, including its \c iid.
        void setPlugin(Plugin *plugin, const json::Value &manifest);

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

        /// The complete manifest, including its \c iid.
        const json::Value &manifest() const;

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

        friend class PluginFactory;

        STDC_DISABLE_COPY(PluginLoader)
    };

    /// @}

}

#endif // STDCORELIB_PLUGIN_PLUGINLOADER_H
