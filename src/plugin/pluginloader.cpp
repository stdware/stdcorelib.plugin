// SPDX-License-Identifier: MIT

#include "pluginloader.h"
#include "pluginloader_p.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

#include <stdcorelib/path.h>
#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>

namespace fs = std::filesystem;

namespace stdc::plugin {

    /// The manifest format this build understands.
    ///
    /// Read before anything else, so that a manifest written for a later version is turned away
    /// rather than half understood.
    static constexpr const char *manifestVersion = "1.0";

    PluginLoader::Impl::Impl() {
    }

    PluginLoader::Impl::~Impl() = default;

    void PluginLoader::Impl::reset() {
        library.reset();
        plugin = nullptr;
        state = PluginLoader::Null;
        hasError = false;
        errorMessage.clear();
        iid.clear();
        filePath.clear();
        metadata = json::Value();
        origin = PluginLoader::FileSystem;
        staticInstance = nullptr;
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

        std::string text;
        std::filesystem::path sourcePath = filePath;
        if (metadataPath) {
            sourcePath = fs::absolute(*metadataPath);
            std::ifstream file(sourcePath);
            if (!file.is_open()) {
                return reportError(formatN(R"(failed to open "%1")", sourcePath));
            }
            std::stringstream ss;
            ss << file.rdbuf();
            text = ss.str();
        } else {
            std::string readError;
            if (!readEmbeddedMetadata(filePath, &text, &readError)) {
                return reportError(formatN(R"(%1: %2)", filePath, readError));
            }
        }
        while (!text.empty() && text.back() == '\0') {
            text.pop_back();
        }

        json::ParseError parseError;
        auto root = json::Value::fromJson(text, true, &parseError);
        if (parseError) {
            return reportError(formatN(R"(%1: %2)", sourcePath, parseError.message()));
        }
        return readMetadata(root, sourcePath, filePath);
    }

    bool PluginLoader::Impl::readMetadata(const json::Value &root,
                                          const std::filesystem::path &sourcePath,
                                          const std::filesystem::path &boundFilePath) {
        if (!root.isObject()) {
            return reportError(formatN(R"(%1: not a JSON object)", sourcePath));
        }
        const auto &obj = root.toObject();

        const auto stringField = [&obj](const std::string_view &key,
                                        std::string_view *out) -> bool {
            auto it = obj.find(key);
            if (it == obj.end() || !it->second.isString()) {
                return false;
            }
            *out = it->second.toString();
            return !out->empty();
        };

        std::string_view version;
        if (!stringField("$version", &version)) {
            return reportError(formatN(R"(%1: missing or invalid "$version" field)", sourcePath));
        }
        if (version != manifestVersion) {
            return reportError(
                formatN(R"(%1: unsupported manifest version "%2")", sourcePath, version));
        }

        std::string_view iid_;
        if (!stringField("iid", &iid_)) {
            return reportError(formatN(R"(%1: missing or invalid "iid" field)", sourcePath));
        }
        iid = iid_;

        std::string_view binary;
        if (boundFilePath.empty()) {
            if (!stringField("binary", &binary)) {
                return reportError(formatN(R"(%1: missing or invalid "binary" field)", sourcePath));
            }
            filePath = sourcePath.parent_path() / path::from_utf8(binary);
        } else {
            filePath = boundFilePath;
        }
        if (!fs::is_regular_file(filePath)) {
            return reportError(
                formatN(R"(%1: "binary" names "%2", which is not there)", sourcePath, filePath));
        }

        // Whatever is in here belongs to the extension point named by iid. Check that it is an
        // object and leave it alone otherwise, an absent one meaning an empty one.
        if (auto it = obj.find("metadata"); it != obj.end()) {
            if (!it->second.isObject()) {
                return reportError(formatN(R"(%1: "metadata" field is not an object)", sourcePath));
            }
            metadata = it->second;
        } else {
            metadata = json::Object();
        }

        state = PluginLoader::Read;
        return true;
    }

    bool PluginLoader::Impl::setStaticPlugin(const StaticPlugin &staticPlugin) {
        reset();
        origin = PluginLoader::Static;
        staticInstance = staticPlugin.instance;
        metadata = staticPlugin.metadata ? staticPlugin.metadata() : json::Value();

        auto staticIid = metadata["iid"];
        if (!staticIid.isString() || staticIid.toString().empty()) {
            return reportError("static plugin declares no iid");
        }

        iid = staticIid.toString();
        state = PluginLoader::Read;
        return true;
    }

    bool PluginLoader::Impl::setRuntimePlugin(Plugin *runtimePlugin,
                                              const json::Value &runtimeMetadata) {
        reset();
        origin = PluginLoader::Runtime;
        metadata = runtimeMetadata;

        auto runtimeIid = metadata["iid"];
        if (!runtimeIid.isString() || runtimeIid.toString().empty()) {
            return reportError("runtime plugin declares no iid");
        }
        iid = runtimeIid.toString();
        if (!runtimePlugin) {
            return reportError("runtime plugin instance is null");
        }

        plugin = runtimePlugin;
        state = PluginLoader::Loaded;
        return true;
    }

    bool PluginLoader::Impl::loadLibrary() {
        if (hasError) {
            return false;
        }
        if (state == PluginLoader::Loaded) {
            return true;
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
            return true;
        }
        if (origin != PluginLoader::FileSystem) {
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
        return true;
    }

    PluginLoader::PluginLoader() : _impl(new Impl()) {
    }

    PluginLoader::PluginLoader(const std::filesystem::path &filePath,
                               const std::optional<std::filesystem::path> &metadataPath)
        : PluginLoader() {
        _impl->readLibrary(filePath, metadataPath);
    }

    PluginLoader::PluginLoader(const StaticPlugin &plugin) : PluginLoader() {
        _impl->setStaticPlugin(plugin);
    }

    PluginLoader::PluginLoader(Plugin *plugin, const json::Value &metadata) : PluginLoader() {
        _impl->setRuntimePlugin(plugin, metadata);
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

    void PluginLoader::setPlugin(Plugin *plugin, const json::Value &metadata) {
        stdc_impl_t;
        impl.setRuntimePlugin(plugin, metadata);
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
