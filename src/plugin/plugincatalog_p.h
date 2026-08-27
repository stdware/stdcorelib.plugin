// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGIN_PLUGINCATALOG_P_H
#define STDCORELIB_PLUGIN_PLUGINCATALOG_P_H

#include <map>
#include <stdcorelib/adt/vlarray.h>
#include <stdcorelib/plugin/plugincatalog.h>

namespace stdc::plugin {

    class PluginCatalog::Impl {
    public:
        Impl(std::string pluginIID, std::unique_ptr<PluginFactory> pluginFactory);

        void updateIndex(const PluginCatalog &catalog) const;
        void rebuildIndex(const PluginCatalog &catalog) const;

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
