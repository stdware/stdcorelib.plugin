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

        PluginLoader::Origin origin = PluginLoader::FileSystem;

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
        bool readLibrary(const std::filesystem::path &libraryPath,
                         const std::optional<std::filesystem::path> &metadataPath = {});
        bool readMetadata(const json::Value &root, const std::filesystem::path &sourcePath,
                          const std::filesystem::path &boundFilePath = {});
        bool setStaticPlugin(const StaticPlugin &plugin);
        bool setRuntimePlugin(Plugin *plugin, const json::Value &metadata);

        static bool readEmbeddedMetadata(const std::filesystem::path &filePath,
                                         std::string *metadata, std::string *errorMessage);

        void reset();
        void clearError();

        bool loadLibrary();
        bool unloadLibrary();

        /// Records \a err and returns false, so a check can be written as one line.
        bool reportError(std::string err, PluginLoader::State errorState = PluginLoader::Invalid);
    };

}

#endif // STDCORELIB_PLUGIN_PLUGINLOADER_P_H
