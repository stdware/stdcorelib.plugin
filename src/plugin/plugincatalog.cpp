// SPDX-License-Identifier: MIT

#include "plugincatalog.h"
#include "plugincatalog_p.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include <stdcorelib/pimpl.h>

namespace stdc::plugin {

    PluginCatalog::Impl::Impl(std::string pluginIID, std::unique_ptr<PluginFactory> pluginFactory)
        : iid(std::move(pluginIID)), factory(std::move(pluginFactory)) {
    }

    void PluginCatalog::Impl::updateIndex(const PluginCatalog &catalog) const {
        if (!indexed || !factory->isIndexed(iid)) {
            rebuildIndex(catalog);
        }
    }

    void PluginCatalog::Impl::rebuildIndex(const PluginCatalog &catalog) const {
        loaders.clear();
        keys.clear();
        loadersByKey.clear();

        for (auto loader : factory->plugins(iid)) {
            if (!loader || loader->iid() != iid) {
                continue;
            }
            loaders.push_back(loader);

            auto pluginKeys = catalog.keysFromMetadata(loader->metadata());
            for (auto &key : pluginKeys) {
                if (key.empty()) {
                    continue;
                }
                auto [found, inserted] = loadersByKey.try_emplace(key);
                auto &keyLoaders = found->second;
                if (std::find(keyLoaders.begin(), keyLoaders.end(), loader) != keyLoaders.end()) {
                    continue;
                }
                keyLoaders.push_back(loader);
                if (inserted) {
                    keys.push_back(std::move(key));
                }
            }
        }
        indexed = true;
    }

    PluginCatalog::PluginCatalog(std::string_view iid, std::unique_ptr<PluginFactory> factory)
        : _impl(std::make_unique<Impl>(std::string(iid), std::move(factory))) {
        stdc_impl_t;
        assert(!impl.iid.empty());
        assert(impl.factory);
    }

    PluginCatalog::~PluginCatalog() = default;

    PluginCatalog::PluginCatalog(PluginCatalog &&RHS) noexcept = default;

    PluginCatalog &PluginCatalog::operator=(PluginCatalog &&RHS) noexcept = default;

    const std::string &PluginCatalog::iid() const {
        stdc_impl_t;
        return impl.iid;
    }

    PluginFactory *PluginCatalog::factory() const {
        stdc_impl_t;
        return impl.factory.get();
    }

    const std::vector<PluginLoader *> &PluginCatalog::loaders() const {
        stdc_impl_t;
        impl.updateIndex(*this);
        return impl.loaders;
    }

    const std::vector<std::string> &PluginCatalog::keys() const {
        stdc_impl_t;
        impl.updateIndex(*this);
        return impl.keys;
    }

    PluginLoader *PluginCatalog::loader(std::string_view key) const {
        stdc_impl_t;
        impl.updateIndex(*this);
        auto found = impl.loadersByKey.find(key);
        return found == impl.loadersByKey.end() ? nullptr : found->second.front();
    }

    std::vector<PluginLoader *> PluginCatalog::loaders(std::string_view key) const {
        stdc_impl_t;
        impl.updateIndex(*this);
        auto found = impl.loadersByKey.find(key);
        if (found == impl.loadersByKey.end()) {
            return {};
        }
        return {found->second.begin(), found->second.end()};
    }

    std::vector<std::string> PluginCatalog::keysFromMetadata(const json::Value &metadata) const {
        std::vector<std::string> result;
        const auto keys = metadata["keys"].asArray();
        if (!keys) {
            return result;
        }
        result.reserve(keys->size());
        for (const auto &key : *keys) {
            if (key.isString() && !key.toString().empty()) {
                result.push_back(key.toString());
            }
        }
        return result;
    }

}
