// SPDX-License-Identifier: MIT

#include "pluginloader.h"
#include "pluginloader_p.h"
#include "pluginmetadata_p.h"

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
        state = PluginLoader::Invalid;
        hasError = false;
        errorMessage.clear();
        iid.clear();
        filePath.clear();
        metadata = json::Value();
        origin = FileSystem;
        staticInstance = nullptr;
    }

    bool PluginLoader::Impl::reportError(std::string err) {
        errorMessage = std::move(err);
        hasError = true;
        state = PluginLoader::Invalid;
        return false;
    }

    bool PluginLoader::Impl::read(const std::filesystem::path &manifestPath) {
        reset();
        std::ifstream file(manifestPath);
        if (!file.is_open()) {
            return reportError(formatN(R"(failed to open "%1")", manifestPath));
        }

        std::stringstream ss;
        ss << file.rdbuf();

        json::ParseError parseError;
        auto root = json::Value::fromJson(ss.str(), true, &parseError);
        if (parseError) {
            return reportError(formatN(R"(%1: %2)", manifestPath, parseError.message()));
        }
        return readMetadata(root, manifestPath);
    }

    bool PluginLoader::Impl::readLibrary(const std::filesystem::path &libraryPath) {
        reset();
        filePath = fs::absolute(libraryPath);
        if (!fs::is_regular_file(filePath)) {
            return reportError(formatN(R"(%1: is not a regular file)", filePath));
        }

#ifdef _WIN32
        auto overridePath = filePath.parent_path() / (filePath.stem().native() + L".plugin.json");
#else
        auto overridePath = filePath.parent_path() / (filePath.stem().string() + ".plugin.json");
#endif
        std::string text;
        std::filesystem::path sourcePath = filePath;
        if (fs::is_regular_file(overridePath)) {
            std::ifstream file(overridePath);
            std::stringstream ss;
            ss << file.rdbuf();
            text = ss.str();
            sourcePath = overridePath;
        } else {
            std::string readError;
            if (!read_embedded_metadata(filePath, &text, &readError)) {
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

    bool PluginLoader::Impl::loadLibrary() {
        if (hasError) {
            return false;
        }
        if (state == PluginLoader::Loaded) {
            return true;
        }

        if (origin == Static) {
            plugin = staticInstance ? staticInstance() : nullptr;
            if (!plugin) {
                return reportError("static plugin produced no instance");
            }
            state = PluginLoader::Loaded;
            return true;
        }

        library.emplace();
        if (!library->open(filePath, SharedLibrary::SearchLibraryLoadDirectoryHint)) {
            auto message = library->errorMessage();
            library.reset();
            return reportError(formatN(R"(%1: %2)", filePath, message));
        }

        auto getter =
            reinterpret_cast<Plugin *(*) ()>(library->resolve(STDC_PLUGIN_INSTANCE_SYMBOL));
        if (!getter) {
            library.reset();
            return reportError(
                formatN(R"(%1: does not export "%2")", filePath, STDC_PLUGIN_INSTANCE_SYMBOL));
        }

        plugin = getter();
        if (!plugin) {
            library.reset();
            return reportError(formatN(R"(%1: exported no instance)", filePath));
        }

        state = PluginLoader::Loaded;
        return true;
    }

    bool PluginLoader::Impl::unloadLibrary() {
        if (state != PluginLoader::Loaded || origin != FileSystem) {
            return state != PluginLoader::Loaded;
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

    PluginLoader::PluginLoader() : _impl(new Impl) {
    }

    PluginLoader::PluginLoader(const std::filesystem::path &filePath) : PluginLoader() {
        _impl->readLibrary(filePath);
    }

    PluginLoader::~PluginLoader() = default;

    PluginLoader::PluginLoader(PluginLoader &&RHS) noexcept = default;

    PluginLoader &PluginLoader::operator=(PluginLoader &&RHS) noexcept = default;

    void PluginLoader::setFilePath(const std::filesystem::path &filePath) {
        stdc_impl_t;
        impl.readLibrary(filePath);
    }

    PluginLoader::State PluginLoader::state() const {
        stdc_impl_t;
        return impl.state;
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

    bool PluginLoader::isLoaded() const {
        stdc_impl_t;
        return impl.state == Loaded;
    }

    Plugin *PluginLoader::plugin() const {
        stdc_impl_t;
        return impl.plugin;
    }

}
