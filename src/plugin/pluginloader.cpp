// SPDX-License-Identifier: MIT

#include "pluginloader.h"
#include "pluginloader_p.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>

namespace fs = std::filesystem;

namespace stdc::plugin {

    PluginLoader::Impl::Impl() {
    }

    PluginLoader::Impl::~Impl() = default;

    void PluginLoader::Impl::reset() {
        library.reset();
        plugin = nullptr;
        state = PluginLoader::Null;
        clearError();
        iid.clear();
        filePath.clear();
        metadata = json::Value();
        origin = PluginLoader::FileSystem;
        staticInstance = nullptr;
    }

    void PluginLoader::Impl::clearError() {
        hasError = false;
        errorMessage.clear();
    }

    bool PluginLoader::Impl::reportError(std::string err, PluginLoader::State errorState) {
        errorMessage = std::move(err);
        hasError = true;
        state = errorState;
        return false;
    }

    bool PluginLoader::Impl::readLibrary(const std::filesystem::path &libraryPath,
                                         const std::optional<std::filesystem::path> &metadataPath) {
        reset();
        filePath = fs::absolute(libraryPath);
        if (!fs::is_regular_file(filePath)) {
            return reportError(formatN(R"(%1: is not a regular file)", filePath));
        }

        std::string embeddedText;
        std::string readError;
        if (!decodeEmbeddedText(filePath, &embeddedText, &readError)) {
            return reportError(formatN(R"(%1: %2)", filePath, readError));
        }
        while (!embeddedText.empty() && embeddedText.back() == '\0') {
            embeddedText.pop_back();
        }

        json::ParseError parseError;
        auto envelope = json::Value::fromJson(embeddedText, true, &parseError);
        if (parseError) {
            return reportError(formatN(R"(%1: %2)", filePath, parseError.message()));
        }

        std::string readIid;
        json::Value readMetadata;
        if (!readEmbeddedEnvelope(envelope, filePath, &readIid, &readMetadata, &readError)) {
            return reportError(std::move(readError));
        }
        if (metadataPath) {
            const auto sourcePath = fs::absolute(*metadataPath);
            std::ifstream file(sourcePath);
            if (!file.is_open()) {
                return reportError(formatN(R"(failed to open "%1")", sourcePath));
            }
            std::stringstream ss;
            ss << file.rdbuf();

            auto externalMetadata = json::Value::fromJson(ss.str(), true, &parseError);
            if (parseError) {
                return reportError(formatN(R"(%1: %2)", sourcePath, parseError.message()));
            }
            if (!validateMetadata(externalMetadata, formatN("%1", sourcePath))) {
                return false;
            }
            readMetadata = std::move(externalMetadata);
        }

        iid = std::move(readIid);
        metadata = std::move(readMetadata);
        state = PluginLoader::Read;
        return true;
    }

    bool PluginLoader::Impl::validateMetadata(const json::Value &root, std::string_view source) {
        if (!root.isObject()) {
            return reportError(formatN(R"(%1: metadata is not a JSON object)", source));
        }
        return true;
    }

    bool PluginLoader::Impl::readEmbeddedEnvelope(const json::Value &root,
                                                  const std::filesystem::path &sourcePath,
                                                  std::string *iid, json::Value *metadata,
                                                  std::string *errorMessage) {
        if (!root.isObject()) {
            *errorMessage = formatN(R"(%1: embedded metadata is not a JSON object)", sourcePath);
            return false;
        }
        const auto &embeddedIid = root["iid"];
        if (!embeddedIid.isString() || embeddedIid.toString().empty()) {
            *errorMessage = formatN(R"(%1: missing or invalid embedded "iid" field)", sourcePath);
            return false;
        }
        const auto &embeddedMetadata = root["metadata"];
        if (!embeddedMetadata.isObject()) {
            *errorMessage = formatN(R"(%1: embedded metadata is not a JSON object)", sourcePath);
            return false;
        }

        *iid = embeddedIid.toString();
        *metadata = embeddedMetadata;
        errorMessage->clear();
        return true;
    }

    bool PluginLoader::Impl::setStaticPlugin(const StaticPlugin &staticPlugin) {
        reset();
        origin = PluginLoader::Static;

        if (staticPlugin.iid.empty()) {
            return reportError("static plugin IID is empty");
        }
        auto staticMetadata =
            staticPlugin.metadata ? staticPlugin.metadata() : json::Value(json::Object());
        if (!validateMetadata(staticMetadata, "static plugin")) {
            return false;
        }

        iid = staticPlugin.iid;
        metadata = std::move(staticMetadata);
        staticInstance = staticPlugin.instance;
        state = PluginLoader::Read;
        return true;
    }

    bool PluginLoader::Impl::setRuntimePlugin(std::string_view runtimeIid, Plugin *runtimePlugin,
                                              const json::Value &runtimeMetadata) {
        reset();
        origin = PluginLoader::Runtime;

        if (runtimeIid.empty()) {
            return reportError("runtime plugin IID is empty");
        }
        if (!validateMetadata(runtimeMetadata, "runtime plugin")) {
            return false;
        }
        if (!runtimePlugin) {
            return reportError("runtime plugin instance is null");
        }

        iid = runtimeIid;
        metadata = runtimeMetadata;
        plugin = runtimePlugin;
        state = PluginLoader::Loaded;
        return true;
    }

    bool PluginLoader::Impl::loadLibrary() {
        if (state == PluginLoader::Loaded) {
            clearError();
            return true;
        }
        if (hasError) {
            return false;
        }

        if (origin == PluginLoader::Static) {
            plugin = staticInstance ? staticInstance() : nullptr;
            if (!plugin) {
                return reportError("static plugin produced no instance", PluginLoader::LoadFailed);
            }
            state = PluginLoader::Loaded;
            return true;
        }

        library.emplace();
        if (!library->open(filePath, SharedLibrary::SearchLibraryLoadDirectoryHint)) {
            auto message = library->errorMessage();
            library.reset();
            return reportError(formatN(R"(%1: %2)", filePath, message), PluginLoader::LoadFailed);
        }

        auto getter =
            reinterpret_cast<Plugin *(*) ()>(library->resolve(STDC_PLUGIN_INSTANCE_SYMBOL));
        if (!getter) {
            library.reset();
            return reportError(
                formatN(R"(%1: does not export "%2")", filePath, STDC_PLUGIN_INSTANCE_SYMBOL),
                PluginLoader::LoadFailed);
        }

        plugin = getter();
        if (!plugin) {
            library.reset();
            return reportError(formatN(R"(%1: exported no instance)", filePath),
                               PluginLoader::LoadFailed);
        }

        state = PluginLoader::Loaded;
        return true;
    }

    bool PluginLoader::Impl::unloadLibrary() {
        if (state != PluginLoader::Loaded) {
            clearError();
            return true;
        }
        if (origin != PluginLoader::FileSystem) {
            errorMessage = "static and runtime plugins cannot be unloaded";
            hasError = true;
            return false;
        }
        if (!library->close()) {
            errorMessage = library->errorMessage();
            hasError = true;
            return false;
        }
        library.reset();
        plugin = nullptr;
        state = PluginLoader::Read;
        clearError();
        return true;
    }

    PluginLoader::PluginLoader() : _impl(std::make_unique<Impl>()) {
    }

    PluginLoader::PluginLoader(const std::filesystem::path &filePath,
                               const std::optional<std::filesystem::path> &metadataPath)
        : PluginLoader() {
        _impl->readLibrary(filePath, metadataPath);
    }

    PluginLoader::PluginLoader(const StaticPlugin &plugin) : PluginLoader() {
        _impl->setStaticPlugin(plugin);
    }

    PluginLoader::PluginLoader(std::string_view iid, Plugin *plugin, const json::Value &metadata)
        : PluginLoader() {
        _impl->setRuntimePlugin(iid, plugin, metadata);
    }

    PluginLoader::~PluginLoader() = default;

    PluginLoader::PluginLoader(PluginLoader &&RHS) noexcept = default;

    PluginLoader &PluginLoader::operator=(PluginLoader &&RHS) noexcept = default;

    void PluginLoader::setFilePath(const std::filesystem::path &filePath,
                                   const std::optional<std::filesystem::path> &metadataPath) {
        stdc_impl_t;
        impl.readLibrary(filePath, metadataPath);
    }

    void PluginLoader::setStaticPlugin(const StaticPlugin &plugin) {
        stdc_impl_t;
        impl.setStaticPlugin(plugin);
    }

    void PluginLoader::setPlugin(std::string_view iid, Plugin *plugin,
                                 const json::Value &metadata) {
        stdc_impl_t;
        impl.setRuntimePlugin(iid, plugin, metadata);
    }

    PluginLoader::State PluginLoader::state() const {
        stdc_impl_t;
        return impl.state;
    }

    PluginLoader::Origin PluginLoader::origin() const {
        stdc_impl_t;
        return impl.origin;
    }

    bool PluginLoader::hasError() const {
        stdc_impl_t;
        return impl.hasError;
    }

    const std::string &PluginLoader::errorMessage() const {
        stdc_impl_t;
        return impl.errorMessage;
    }

    const std::string &PluginLoader::iid() const {
        stdc_impl_t;
        return impl.iid;
    }

    const std::filesystem::path &PluginLoader::filePath() const {
        stdc_impl_t;
        return impl.filePath;
    }

    const json::Value &PluginLoader::metadata() const {
        stdc_impl_t;
        return impl.metadata;
    }

    bool PluginLoader::load() {
        stdc_impl_t;
        return impl.loadLibrary();
    }

    bool PluginLoader::unload() {
        stdc_impl_t;
        return impl.unloadLibrary();
    }

    Plugin *PluginLoader::plugin() const {
        stdc_impl_t;
        return impl.plugin;
    }

    std::vector<std::string> PluginLoader::staticPluginSets() {
        std::vector<std::string> pluginSets;
        for (const auto &entry : StaticPluginRegistry::entries()) {
            auto pluginSet = std::string(entry.name());
            if (std::find(pluginSets.begin(), pluginSets.end(), pluginSet) == pluginSets.end()) {
                pluginSets.push_back(std::move(pluginSet));
            }
        }
        return pluginSets;
    }

    std::vector<StaticPlugin> PluginLoader::staticPlugins(std::string_view iid) {
        std::vector<StaticPlugin> plugins;
        for (const auto &entry : StaticPluginRegistry::entries()) {
            if (entry.name() == iid) {
                plugins.push_back(entry.instantiate());
            }
        }
        return plugins;
    }

}
