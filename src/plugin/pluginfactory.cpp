// SPDX-License-Identifier: MIT

#include "pluginfactory.h"
#include "pluginfactory_p.h"

#include <fstream>
#include <mutex>
#include <sstream>
#include <utility>

#include <stdcorelib/path.h>
#include <stdcorelib/pimpl.h>
#include <stdcorelib/stlextra/algorithms.h>

namespace fs = std::filesystem;

STDC_INSTANTIATE_STATIC_REGISTRY_EXPORT(stdc::plugin::StaticPlugin, STDC_PLUGIN_EXPORT)

namespace stdc::plugin {

    /// The file that says what a plugin is. One per plugin directory.
    static constexpr const char *manifestName = "plugin.json";

    PluginFactory::Impl::Impl() {
    }

    PluginFactory::Impl::~Impl() = default;

    std::unique_ptr<PluginLoader> PluginFactory::Impl::createLoader() {
        return std::make_unique<PluginLoader>();
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
                std::optional<std::filesystem::path> metadataPath;
                if (!factory.resolvePluginPath(candidate, &pluginPath, &metadataPath)) {
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
                loader->setFilePath(canonical, metadataPath);
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

    PluginFactory::PluginFactory() : _impl(std::make_unique<Impl>()) {
    }

    PluginFactory::PluginFactory(Impl &impl) : _impl(&impl) {
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
        for (const auto &entry : dir) {
            if (entry.is_directory() && fs::is_regular_file(entry.path() / manifestName)) {
                pluginPaths->push_back(entry.path());
            }
        }
        return true;
    }

    bool
        PluginFactory::resolvePluginPath(const std::filesystem::path &path,
                                         std::filesystem::path *pluginPath,
                                         std::optional<std::filesystem::path> *metadataPath) const {
        auto manifest = path / manifestName;
        std::ifstream file(manifest);
        if (!file.is_open()) {
            return false;
        }

        std::stringstream ss;
        ss << file.rdbuf();
        json::ParseError parseError;
        auto root = json::Value::fromJson(ss.str(), true, &parseError);
        if (parseError || !root.isObject()) {
            return false;
        }

        auto binary = root["binary"];
        if (!binary.isString() || binary.toString().empty()) {
            return false;
        }
        *pluginPath = path / stdc::path::from_utf8(binary.toString());
        *metadataPath = std::move(manifest);
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

    void PluginFactory::addRuntimePlugin(Plugin *plugin, const json::Value &metadata) {
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);

        auto loader = std::make_unique<PluginLoader>(plugin, metadata);
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
        if (realPaths.empty()) {
            if (auto it = impl.pluginPaths.find(iid); it != impl.pluginPaths.end()) {
                impl.pluginPaths.erase(it);
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

}
