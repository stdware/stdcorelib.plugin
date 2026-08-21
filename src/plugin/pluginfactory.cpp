// SPDX-License-Identifier: MIT

#include "pluginfactory.h"
#include "pluginfactory_p.h"
#include "pluginloader_p.h"

#include <mutex>
#include <utility>

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>

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

    void PluginFactory::Impl::scanPlugins(std::string_view iid) const {
        auto it = pluginPaths.find(iid);
        if (it == pluginPaths.end()) {
            if (auto dirty = pluginsDirty.find(iid); dirty != pluginsDirty.end()) {
                pluginsDirty.erase(dirty);
            }
            return;
        }

        auto &known = loaders[std::string(iid)];
        for (const auto &root : it->second) {
            std::error_code ec;
            fs::directory_iterator dir(root, ec);
            if (ec) {
                continue;
            }

            // One directory per plugin, holding the manifest and whatever else the plugin needs
            // beside it. A directory without a manifest is not ours to complain about.
            for (const auto &entry : dir) {
                if (!entry.is_directory()) {
                    continue;
                }

                auto manifest = entry.path() / manifestName;
                if (!fs::is_regular_file(manifest)) {
                    continue;
                }

                auto canonical = fs::canonical(manifest, ec);
                if (ec) {
                    continue;
                }
                if (!readManifests.insert(canonical.native()).second) {
                    continue;
                }

                auto loader = createLoader();
                auto &loaderImpl = *loader->_impl;
                if (loaderImpl.read(canonical) && loaderImpl.iid != iid) {
                    // The directory it was found in says one extension point and the manifest
                    // says another. Keep it where it was found so that whoever looks there is
                    // told, rather than filing it where nobody is looking.
                    loaderImpl.reportError(formatN(
                        R"("%1" declares iid "%2", which is not the "%3" it was found under)",
                        canonical, loaderImpl.iid, iid));
                }
                known.emplace_back(std::move(loader));
            }
        }

        if (auto dirty = pluginsDirty.find(iid); dirty != pluginsDirty.end()) {
            pluginsDirty.erase(dirty);
        }
    }

    PluginFactory::PluginFactory() : _impl(new Impl()) {
    }

    PluginFactory::PluginFactory(Impl &impl) : _impl(&impl) {
    }

    PluginFactory::~PluginFactory() = default;

    PluginFactory::PluginFactory(PluginFactory &&RHS) noexcept = default;

    PluginFactory &PluginFactory::operator=(PluginFactory &&RHS) noexcept = default;

    void PluginFactory::addStaticPlugins(std::string_view pluginSet) {
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);

        for (const StaticPlugin &plugin : PluginLoader::staticPlugins(pluginSet)) {
            auto loader = std::make_unique<PluginLoader>(plugin);
            impl.loaders[loader->iid()].emplace_back(std::move(loader));
        }
    }

    void PluginFactory::addRuntimePlugin(Plugin *plugin, const json::Value &metadata) {
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);

        auto loader = std::make_unique<PluginLoader>(plugin, metadata);
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

        if (impl.pluginsDirty.count(iid)) {
            impl.scanPlugins(iid);
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
