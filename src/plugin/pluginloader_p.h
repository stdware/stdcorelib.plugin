// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGIN_PLUGINLOADER_P_H
#define STDCORELIB_PLUGIN_PLUGINLOADER_P_H

#include <optional>

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

        PluginLoader::State state = PluginLoader::Null;
        bool hasError = false;
        std::string errorMessage;

        std::string iid;
        std::filesystem::path filePath;
        json::Value metadata;

        /// Set for \c Static, which needs a way back to the instance it was registered with.
        StaticPlugin::PluginInstanceFunction staticInstance = nullptr;

        Plugin *plugin = nullptr;
        std::optional<SharedLibrary> library;

    public:
        /// Reads \a manifestPath and fills everything but the instance.
        ///
        /// Failure is recorded rather than thrown away, so that a plugin which is installed but
        /// unusable still shows up with a reason attached.
        bool read(const std::filesystem::path &manifestPath);
        bool readLibrary(const std::filesystem::path &libraryPath,
                         const std::optional<std::filesystem::path> &metadataPath = {});
        bool readMetadata(const json::Value &root, const std::filesystem::path &sourcePath,
                          const std::filesystem::path &boundFilePath = {});

        static bool readEmbeddedMetadata(const std::filesystem::path &filePath,
                                         std::string *metadata, std::string *errorMessage);

        void reset();

        bool loadLibrary();
        bool unloadLibrary();

        /// Records \a err and returns false, so a check can be written as one line.
        bool reportError(std::string err, PluginLoader::State errorState = PluginLoader::Invalid);
    };

}

#endif // STDCORELIB_PLUGIN_PLUGINLOADER_P_H
