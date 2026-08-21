// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGIN_PLUGINLOADER_P_H
#define STDCORELIB_PLUGIN_PLUGINLOADER_P_H

#include <stdcorelib/support/sharedlibrary.h>

#include <stdcorelib/plugin/plugin.h>
#include <stdcorelib/plugin/pluginloader.h>

namespace stdc::plugin {

    class PluginLoader::Impl {
    public:
        Impl();
        ~Impl();

    public:
        /// Where the instance is meant to come from.
        ///
        /// The three differ only in what \c loadLibrary() has to do, so everything downstream of
        /// a loader treats them alike.
        enum Origin {
            /// A shared library sitting next to the manifest that described it.
            FileSystem,

            /// Linked into the program, handing over its metadata rather than writing it down.
            Static,

            /// An instance the program built and handed over, already live.
            Runtime,
        };

        Origin origin = FileSystem;

        PluginLoader::State state = PluginLoader::Invalid;
        bool hasError = false;
        std::string errorMessage;

        std::string iid;
        std::filesystem::path location;
        std::filesystem::path filePath;
        json::Value metadata;

        /// Set for \c Static, which needs a way back to the instance it was registered with.
        StaticPlugin::PluginInstanceFunction staticInstance = nullptr;

        Plugin *plugin = nullptr;
        SharedLibrary *library = nullptr;

    public:
        /// Reads \a manifestPath and fills everything but the instance.
        ///
        /// Failure is recorded rather than thrown away, so that a plugin which is installed but
        /// unusable still shows up with a reason attached.
        bool read(const std::filesystem::path &manifestPath);

        bool loadLibrary();

        /// Records \a err and returns false, so a check can be written as one line.
        bool reportError(std::string err);
    };

}

#endif // STDCORELIB_PLUGIN_PLUGINLOADER_P_H
