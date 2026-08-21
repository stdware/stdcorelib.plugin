// SPDX-License-Identifier: MIT

#include "pluginfactory.h"
#include "pluginfactory_p.h"
#include "pluginspec_p.h"

#include <algorithm>
#include <mutex>
#include <utility>

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>

namespace fs = std::filesystem;

STDC_INSTANTIATE_STATIC_REGISTRY_EXPORT(stdc::StaticPlugin, STDC_PLUGIN_EXPORT)

namespace stdc {

    /// The file that says what a plugin is. One per plugin directory.
    static constexpr const char *manifestName = "plugin.json";

    PluginFactory::Impl::Impl(PluginFactory *decl) : _decl(decl) {
    }

    PluginFactory::Impl::~Impl() = default;

    PluginSpec *PluginFactory::Impl::createSpec() {
        auto specImpl = new PluginSpec::Impl(nullptr);
        auto spec = new PluginSpec(*specImpl);
        specImpl->_decl = spec;
        return spec;
    }

    void PluginFactory::Impl::scanPlugins(const char *iid) const {
        auto it = pluginPaths.find(iid);
        if (it == pluginPaths.end()) {
            pluginsDirty.erase(iid);
            return;
        }

        auto &known = specs[iid];
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

                auto spec = createSpec();
                auto &specImpl = *spec->_impl;
                if (specImpl.read(canonical) && specImpl.iid != iid) {
                    // The directory it was found in says one extension point and the manifest
                    // says another. Keep it where it was found so that whoever looks there is
                    // told, rather than filing it where nobody is looking.
                    specImpl.reportError(formatN(
                        R"("%1" declares iid "%2", which is not the "%3" it was found under)",
                        canonical, specImpl.iid, iid));
                }
                known.emplace_back(spec);
            }
        }

        pluginsDirty.erase(iid);
    }

    PluginFactory::PluginFactory() : _impl(new Impl(this)) {
    }

    PluginFactory::PluginFactory(Impl &impl) : _impl(&impl) {
    }

    PluginFactory::~PluginFactory() = default;

    std::vector<std::string> PluginFactory::staticPluginSets() {
        std::vector<std::string> pluginSets;
        for (const auto &entry : StaticPluginRegistry::entries()) {
            auto pluginSet = std::string(entry.name());
            if (std::find(pluginSets.begin(), pluginSets.end(), pluginSet) == pluginSets.end()) {
                pluginSets.push_back(std::move(pluginSet));
            }
        }
        return pluginSets;
    }

    std::vector<StaticPlugin> PluginFactory::staticPlugins(const char *pluginSet) {
        std::vector<StaticPlugin> plugins;
        for (const auto &entry : StaticPluginRegistry::entries()) {
            if (entry.name() == pluginSet) {
                plugins.push_back(*entry.instantiate());
            }
        }
        return plugins;
    }

    void PluginFactory::addStaticPlugins(const char *pluginSet) {
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);

        for (const StaticPlugin &plugin : staticPlugins(pluginSet)) {
            auto spec = Impl::createSpec();
            auto &specImpl = *spec->_impl;
            specImpl.origin = PluginSpec::Impl::Static;
            specImpl.staticInstance = plugin.instance;
            specImpl.metadata = plugin.metadata ? plugin.metadata() : json::Value();
            specImpl.state = PluginSpec::Read;

            auto iid = specImpl.metadata["iid"];
            if (!iid.isString() || iid.toString().empty()) {
                specImpl.reportError(
                    formatN(R"(static plugin in set "%1" declares no iid)", pluginSet));
            } else {
                specImpl.iid = iid.toString();
            }
            impl.specs[specImpl.iid].emplace_back(spec);
        }
    }

    void PluginFactory::addRuntimePlugin(Plugin *plugin, const json::Value &metadata) {
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);

        auto spec = Impl::createSpec();
        auto &specImpl = *spec->_impl;
        specImpl.origin = PluginSpec::Impl::Runtime;
        specImpl.metadata = metadata;
        specImpl.plugin = plugin;
        specImpl.state = PluginSpec::Loaded;

        auto iid = metadata["iid"];
        if (!iid.isString() || iid.toString().empty()) {
            specImpl.reportError("runtime plugin declares no iid");
        } else {
            specImpl.iid = iid.toString();
        }
        impl.specs[specImpl.iid].emplace_back(spec);
    }

    void PluginFactory::addPluginPath(const char *iid, const std::filesystem::path &path) {
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);
        if (!fs::is_directory(path)) {
            return;
        }
        impl.pluginPaths[iid].push_back(fs::canonical(path));
        impl.pluginsDirty.insert(iid);
    }

    void PluginFactory::setPluginPaths(const char *iid, array_view<std::filesystem::path> paths) {
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
            impl.pluginPaths.erase(iid);
        } else {
            impl.pluginPaths[iid] = std::move(realPaths);
        }
        impl.pluginsDirty.insert(iid);
    }

    std::vector<std::filesystem::path> PluginFactory::pluginPaths(const char *iid) const {
        stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.plugins_mtx);
        auto it = impl.pluginPaths.find(iid);
        if (it == impl.pluginPaths.end()) {
            return {};
        }
        return {it->second.begin(), it->second.end()};
    }

    std::vector<PluginSpec *> PluginFactory::plugins(const char *iid) const {
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);

        if (impl.pluginsDirty.count(iid)) {
            impl.scanPlugins(iid);
        }

        auto it = impl.specs.find(iid);
        if (it == impl.specs.end()) {
            return {};
        }

        std::vector<PluginSpec *> result;
        result.reserve(it->second.size());
        for (const auto &spec : it->second) {
            result.push_back(spec.get());
        }
        return result;
    }

}
