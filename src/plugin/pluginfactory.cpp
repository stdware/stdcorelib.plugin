// SPDX-License-Identifier: MIT

#include "pluginfactory.h"
#include "pluginfactory_p.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <fstream>
#include <mutex>
#include <sstream>
#include <utility>

#include <stdcorelib/path.h>
#include <stdcorelib/pimpl.h>
#include <stdcorelib/stlextra/algorithms.h>
#include <stdcorelib/support/sharedlibrary.h>

#include "pluginloader_p.h"

namespace fs = std::filesystem;

STDC_INSTANTIATE_STATIC_REGISTRY_EXPORT(stdc::plugin::StaticPlugin, STDC_PLUGIN_EXPORT)

namespace stdc::plugin {

    namespace {

        bool is_file_name(const fs::path &path) {
            return !path.empty() && path != "." && path != ".." && path == path.filename();
        }

    }

    PluginFactory::Impl::Impl() {
    }

    PluginFactory::Impl::~Impl() = default;

    std::unique_ptr<PluginLoader> PluginFactory::Impl::createLoader() {
        return std::make_unique<PluginLoader>();
    }

    void PluginFactory::Impl::discardUnloadedFilePlugins(std::string_view iid) const {
        auto found = loaders.find(iid);
        if (found == loaders.end()) {
            return;
        }

        auto &known = found->second;
        auto newEnd = std::remove_if(
            known.begin(), known.end(), [this](const std::unique_ptr<PluginLoader> &loader) {
                if (loader->origin() != PluginLoader::FileSystem || loader->isLoaded()) {
                    return false;
                }
                readPluginFiles.erase(loader->filePath().native());
                return true;
            });
        known.erase(newEnd, known.end());
        if (known.empty()) {
            loaders.erase(found);
        }
    }

    void PluginFactory::Impl::scanPlugins(const PluginFactory &factory,
                                          std::string_view iid) const {
        auto it = pluginPaths.find(iid);
        if (it == pluginPaths.end()) {
            if (auto dirty = pluginsDirty.find(iid); dirty != pluginsDirty.end()) {
                pluginsDirty.erase(dirty);
            }
            return;
        }

        auto &known = loaders[std::string(iid)];
        bool scanSucceeded = true;
        for (const auto &root : it->second) {
            std::vector<std::filesystem::path> candidates;
            if (!factory.scanPluginPaths(root, &candidates)) {
                scanSucceeded = false;
                continue;
            }
            for (const auto &candidate : candidates) {
                std::filesystem::path pluginPath;
                std::optional<std::filesystem::path> manifestPath;
                if (!factory.resolvePluginPath(candidate, &pluginPath, &manifestPath)) {
                    scanSucceeded = false;
                    continue;
                }

                std::error_code ec;
                auto canonical = fs::canonical(pluginPath, ec);
                if (ec) {
                    scanSucceeded = false;
                    continue;
                }
                auto pluginFile = canonical.native();
                if (stdc::contains(readPluginFiles, pluginFile)) {
                    continue;
                }

                auto loader = createLoader();
                loader->setFilePath(canonical, manifestPath);
                if (loader->iid() != iid) {
                    continue;
                }

                readPluginFiles.insert(std::move(pluginFile));
                known.emplace_back(std::move(loader));
            }
        }

        if (scanSucceeded) {
            if (auto dirty = pluginsDirty.find(iid); dirty != pluginsDirty.end()) {
                pluginsDirty.erase(dirty);
            }
        }
    }

    PluginFactory::PluginFactory() : PluginFactory(std::make_unique<Impl>()) {
    }

    PluginFactory::~PluginFactory() = default;

    PluginFactory::PluginFactory(PluginFactory &&RHS) noexcept = default;

    PluginFactory &PluginFactory::operator=(PluginFactory &&RHS) noexcept = default;

    bool PluginFactory::scanPluginPaths(const std::filesystem::path &path,
                                        std::vector<std::filesystem::path> *pluginPaths) const {
        std::error_code ec;
        fs::directory_iterator dir(path, ec);
        if (ec) {
            return false;
        }

        const fs::directory_iterator end;
        while (dir != end) {
            const auto candidate = dir->path();
            if (SharedLibrary::isLibrary(candidate)) {
                std::string manifest;
                std::string errorMessage;
                if (PluginLoader::Impl::readEmbeddedManifest(candidate, &manifest, &errorMessage)) {
                    pluginPaths->push_back(candidate);
                }
            }

            dir.increment(ec);
            if (ec) {
                return false;
            }
        }
        std::sort(pluginPaths->begin(), pluginPaths->end());
        return true;
    }

    bool
        PluginFactory::resolvePluginPath(const std::filesystem::path &path,
                                         std::filesystem::path *pluginPath,
                                         std::optional<std::filesystem::path> *manifestPath) const {
        *pluginPath = path;
        manifestPath->reset();
        return true;
    }

    void PluginFactory::addStaticPlugins(std::string_view pluginSet) {
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);

        for (const StaticPlugin &plugin : PluginLoader::staticPlugins(pluginSet)) {
            auto loader = std::make_unique<PluginLoader>(plugin);
            if (loader->iid().empty()) {
                continue;
            }
            impl.loaders[loader->iid()].emplace_back(std::move(loader));
        }
    }

    void PluginFactory::addRuntimePlugin(Plugin *plugin, const json::Value &manifest) {
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);

        auto loader = std::make_unique<PluginLoader>(plugin, manifest);
        if (loader->iid().empty()) {
            return;
        }
        impl.loaders[loader->iid()].emplace_back(std::move(loader));
    }

    void PluginFactory::addPluginPath(std::string_view iid, const std::filesystem::path &path) {
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);
        if (!fs::is_directory(path)) {
            return;
        }
        impl.pluginPaths[std::string(iid)].push_back(fs::canonical(path));
        impl.pluginsDirty.insert(std::string(iid));
    }

    void PluginFactory::setPluginPaths(std::string_view iid,
                                       array_view<std::filesystem::path> paths) {
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);

        vlarray<std::filesystem::path> realPaths;
        realPaths.reserve(paths.size());
        for (const auto &path : paths) {
            if (!fs::is_directory(path)) {
                continue;
            }
            realPaths.push_back(fs::canonical(path));
        }

        auto current = impl.pluginPaths.find(iid);
        const bool unchanged =
            realPaths.empty()
                ? current == impl.pluginPaths.end()
                : current != impl.pluginPaths.end() && current->second.size() == realPaths.size() &&
                      std::equal(current->second.begin(), current->second.end(), realPaths.begin());
        if (unchanged) {
            return;
        }

        impl.discardUnloadedFilePlugins(iid);
        if (realPaths.empty()) {
            if (current != impl.pluginPaths.end()) {
                impl.pluginPaths.erase(current);
            }
        } else {
            impl.pluginPaths[std::string(iid)] = std::move(realPaths);
        }
        impl.pluginsDirty.insert(std::string(iid));
    }

    std::vector<std::filesystem::path> PluginFactory::pluginPaths(std::string_view iid) const {
        stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.plugins_mtx);
        auto it = impl.pluginPaths.find(iid);
        if (it == impl.pluginPaths.end()) {
            return {};
        }
        return {it->second.begin(), it->second.end()};
    }

    std::vector<PluginLoader *> PluginFactory::plugins(std::string_view iid) const {
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);

        if (stdc::contains(impl.pluginsDirty, iid)) {
            impl.scanPlugins(*this, iid);
        }

        auto it = impl.loaders.find(iid);
        if (it == impl.loaders.end()) {
            return {};
        }

        std::vector<PluginLoader *> result;
        result.reserve(it->second.size());
        for (const auto &loader : it->second) {
            result.push_back(loader.get());
        }
        return result;
    }

    PluginFactory::PluginFactory(std::unique_ptr<Impl> impl) : _impl(std::move(impl)) {
    }

    BundlePluginFactory::BundlePluginFactory(fs::path manifestFileName)
        : PluginFactory(std::make_unique<Impl>(std::move(manifestFileName))) {
        stdc_impl_t;
        assert(is_file_name(impl.manifestFileName));
    }

    BundlePluginFactory::~BundlePluginFactory() = default;

    BundlePluginFactory::BundlePluginFactory(BundlePluginFactory &&RHS) noexcept = default;

    BundlePluginFactory &
        BundlePluginFactory::operator=(BundlePluginFactory &&RHS) noexcept = default;

    const fs::path &BundlePluginFactory::manifestFileName() const {
        stdc_impl_t;
        return impl.manifestFileName;
    }

    bool BundlePluginFactory::scanPluginPaths(const fs::path &path,
                                              std::vector<fs::path> *pluginPaths) const {
        if (!is_file_name(manifestFileName())) {
            return false;
        }

        std::error_code ec;
        fs::directory_iterator dir(path, ec);
        if (ec) {
            return false;
        }

        const fs::directory_iterator end;
        while (dir != end) {
            if (dir->is_directory(ec) && !ec &&
                fs::is_regular_file(dir->path() / manifestFileName(), ec) && !ec) {
                pluginPaths->push_back(dir->path());
            }
            ec.clear();
            dir.increment(ec);
            if (ec) {
                return false;
            }
        }
        std::sort(pluginPaths->begin(), pluginPaths->end());
        return true;
    }

    bool BundlePluginFactory::resolvePluginPath(const fs::path &path, fs::path *pluginPath,
                                                std::optional<fs::path> *manifestPath) const {
        if (!is_file_name(manifestFileName())) {
            return false;
        }

        const auto manifest = path / manifestFileName();
        std::ifstream file(manifest);
        if (!file.is_open()) {
            return false;
        }

        std::stringstream stream;
        stream << file.rdbuf();
        json::ParseError parseError;
        auto root = json::Value::fromJson(stream.str(), true, &parseError);
        if (parseError || !root.isObject()) {
            return false;
        }

        auto resolvedPath = resolveLibraryPath(path, root);
        if (!resolvedPath) {
            return false;
        }

        *pluginPath = std::move(*resolvedPath);
        *manifestPath = manifest;
        return true;
    }

    std::optional<fs::path>
        BundlePluginFactory::resolveLibraryPath(const fs::path &bundlePath,
                                                const json::Value &manifest) const {
        const auto &name = manifest["name"];
        if (!name.isString() || name.toString().empty()) {
            return std::nullopt;
        }

        const auto requestedPath = stdc::path::from_utf8(name.toString());
        if (requestedPath.empty() || requestedPath.is_absolute() ||
            requestedPath.has_parent_path()) {
            return std::nullopt;
        }

        constexpr std::array<std::string_view, 2> prefixes{"", "lib"};
        constexpr std::array<std::string_view, 2> suffixes{
            "",
#ifdef _WIN32
            ".dll",
#elif defined(__APPLE__)
            ".dylib",
#else
            ".so",
#endif
        };

        for (const auto prefix : prefixes) {
            for (const auto suffix : suffixes) {
                fs::path candidateName = std::string(prefix);
                candidateName += requestedPath.native();
                candidateName += std::string(suffix);
                const auto candidate = bundlePath / candidateName;
                std::error_code ec;
                if (fs::is_regular_file(candidate, ec) && SharedLibrary::isLibrary(candidate)) {
                    return candidate;
                }
            }
        }
        return std::nullopt;
    }

}
