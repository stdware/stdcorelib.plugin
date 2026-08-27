// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGIN_PLUGINCATALOG_P_H
#define STDCORELIB_PLUGIN_PLUGINCATALOG_P_H

#include <map>
#include <mutex>
#include <shared_mutex>

#include <stdcorelib/adt/vlarray.h>
#include <stdcorelib/plugin/plugincatalog.h>

#include "pluginfactory_p.h"

namespace stdc::plugin {

    class PluginCatalog::Impl {
    public:
        Impl(std::string pluginIID, std::unique_ptr<PluginFactory> pluginFactory);

        void updateIndex(const PluginCatalog &catalog, PluginFactory::Impl &factoryImpl) const;
        void rebuildIndex(const PluginCatalog &catalog, PluginFactory::Impl &factoryImpl) const;

        template <class F>
        auto readIndex(const PluginCatalog &catalog, F &&read) const -> decltype(read()) {
            auto &factoryImpl = *factory->_impl;
            {
                std::shared_lock<std::shared_mutex> lock(factoryImpl.plugins_mtx);
                if (indexed && factoryImpl.isIndexed(iid)) {
                    return read();
                }
            }

            std::unique_lock<std::shared_mutex> lock(factoryImpl.plugins_mtx);
            updateIndex(catalog, factoryImpl);
            return read();
        }

        std::string iid;
        std::unique_ptr<PluginFactory> factory;
        mutable bool indexed = false;
        mutable std::vector<PluginLoader *> loaders;
        mutable std::vector<std::string> keys;
        /// Four factories for one key cover the common case without a heap allocation.
        mutable std::map<std::string, vlarray<PluginLoader *, 4>, std::less<>> loadersByKey;
    };

}

#endif // STDCORELIB_PLUGIN_PLUGINCATALOG_P_H
