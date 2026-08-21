// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGIN_PLUGINLOADER_H
#define STDCORELIB_PLUGIN_PLUGINLOADER_H

#include <filesystem>
#include <memory>
#include <string>

#include <stdcorelib/stdc_global.h>
#include <stdcorelib/support/json.h>

#include <stdcorelib/stdc_plugin_global.h>

namespace stdc {

    /// \addtogroup plugin
    /// @{

    class Plugin;

    class PluginFactory;

    /// Reads, loads, and owns one plugin library.
    ///
    /// A loader is created for every plugin the factory finds, including the ones it cannot use. A
    /// plugin whose manifest is malformed, or whose library refuses to load, keeps its loader and
    /// answers \c hasError(), so the reason can be reported instead of the plugin quietly not
    /// being there.
    ///
    /// Reading a loader costs one small file. Nothing is loaded until \c load() is called, so the
    /// factory can describe every installed plugin without pulling in the libraries they depend
    /// on.
    class STDC_PLUGIN_EXPORT PluginLoader {
    public:
        ~PluginLoader();

        enum State {
            /// The manifest could not be read. Only \c location() and \c errorString() are
            /// meaningful.
            Invalid,

            /// The manifest has been read. Everything but \c plugin() is meaningful.
            Read,

            /// The library is loaded and \c plugin() is live.
            Loaded,
        };

    public:
        State state() const;
        bool hasError() const;

        /// Why this plugin is unusable, empty if it is not.
        const std::string &errorString() const;

    public:
        /// The extension point this plugin plugs into, such as \c org.openvpi.InferenceInterpreter.
        ///
        /// \note This is the only part of the manifest the factory interprets. Whoever owns the
        ///       extension point decides what the rest of it means.
        const std::string &iid() const;

        /// The directory holding the plugin, which is also where its own resources live.
        const std::filesystem::path &location() const;

        /// The shared library, or empty for a static or runtime plugin.
        const std::filesystem::path &filePath() const;

        /// What this plugin says about itself, in whatever shape its extension point defines.
        ///
        /// The library never looks inside.
        const json::Value &metadata() const;

    public:
        /// Loads the library on first call, returning whether \c plugin() is now live.
        ///
        /// \note Why it failed is on \c errorString(), which is where it has to be anyway for a
        ///       plugin that is installed but unusable to be able to say so.
        bool load();

        /// The loaded instance, or null while \c state() is below \c Loaded.
        Plugin *plugin() const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;

        explicit PluginLoader(Impl &impl);

        friend class PluginFactory;

        STDC_DISABLE_COPY_MOVE(PluginLoader)
    };

    /// @}

}

#endif // STDCORELIB_PLUGIN_PLUGINLOADER_H
