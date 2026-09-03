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

namespace fs = std::filesystem;

STDC_STATIC_REGISTRY(stdc::plugin::StaticPlugin)

namespace stdc::plugin {

    namespace {

        bool is_relative_file_path(const fs::path &path) {
            if (path.empty() || path.is_absolute() || path.has_root_name() ||
                path.has_root_directory() || path.filename().empty() || path.filename() == ".") {
                return false;
            }
            return std::none_of(path.begin(), path.end(),
                                [](const auto &component) { return component == ".."; });
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
            if (!factory.scanPluginPaths(iid, root, &candidates)) {
                scanSucceeded = false;
                continue;
            }
            for (const auto &candidate : candidates) {
                std::filesystem::path pluginPath;
                std::optional<std::filesystem::path> metadataPath;
                if (!factory.resolvePluginPath(iid, candidate, &pluginPath, &metadataPath)) {
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
                loader->setFilePath(canonical);
                if (loader->iid() != iid) {
                    continue;
                }
                if (metadataPath) {
                    loader->setFilePath(canonical, metadataPath);
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

    bool PluginFactory::scanPluginPaths(std::string_view iid, const std::filesystem::path &path,
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
                const PluginLoader loader(candidate);
                if (loader.iid() == iid) {
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
        PluginFactory::resolvePluginPath(std::string_view, const std::filesystem::path &path,
                                         std::filesystem::path *pluginPath,
                                         std::optional<std::filesystem::path> *metadataPath) const {
        *pluginPath = path;
        metadataPath->reset();
        return true;
    }

    void PluginFactory::addStaticPlugins(std::string_view iid) {
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);

        for (const StaticPlugin &plugin : PluginLoader::staticPlugins(iid)) {
            auto loader = std::make_unique<PluginLoader>(plugin);
            if (loader->iid().empty()) {
                continue;
            }
            impl.loaders[loader->iid()].emplace_back(std::move(loader));
        }
        impl.pluginsDirty.insert(std::string(iid));
    }

    void PluginFactory::addRuntimePlugin(std::string_view iid, Plugin *plugin,
                                         const json::Value &metadata) {
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);

        auto loader = std::make_unique<PluginLoader>(iid, plugin, metadata);
        if (loader->iid().empty()) {
            return;
        }
        impl.loaders[loader->iid()].emplace_back(std::move(loader));
        impl.pluginsDirty.insert(std::string(iid));
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

    BundlePluginFactory::BundlePluginFactory(fs::path metadataFileName)
        : PluginFactory(std::make_unique<Impl>(std::move(metadataFileName))) {
        stdc_impl_t;
        assert(is_relative_file_path(impl.metadataFileName));
    }

    BundlePluginFactory::~BundlePluginFactory() = default;

    BundlePluginFactory::BundlePluginFactory(BundlePluginFactory &&RHS) noexcept = default;

    BundlePluginFactory &
        BundlePluginFactory::operator=(BundlePluginFactory &&RHS) noexcept = default;

    const fs::path &BundlePluginFactory::metadataFileName() const {
        stdc_impl_t;
        return impl.metadataFileName;
    }

    bool BundlePluginFactory::scanPluginPaths(std::string_view, const fs::path &path,
                                              std::vector<fs::path> *pluginPaths) const {
        if (!is_relative_file_path(metadataFileName())) {
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
                fs::is_regular_file(dir->path() / metadataFileName(), ec) && !ec) {
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

    bool BundlePluginFactory::resolvePluginPath(std::string_view, const fs::path &path,
                                                fs::path *pluginPath,
                                                std::optional<fs::path> *metadataPath) const {
        if (!is_relative_file_path(metadataFileName())) {
            return false;
        }

        const auto metadataFile = path / metadataFileName();
        std::ifstream file(metadataFile);
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
        *metadataPath = metadataFile;
        return true;
    }

    std::optional<fs::path>
        BundlePluginFactory::resolveLibraryPath(const fs::path &bundlePath,
                                                const json::Value &metadata) const {
        const auto &name = metadata["name"];
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
