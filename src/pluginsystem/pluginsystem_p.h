// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGINSYSTEM_PLUGINSYSTEM_P_H
#define STDCORELIB_PLUGINSYSTEM_PLUGINSYSTEM_P_H

#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>

#include <stdcorelib/adt/linked_map.h>
#include <stdcorelib/adt/vlarray.h>
#include <stdcorelib/pimpl.h>

#include <stdcorelib/plugin/pluginfactory.h>
#include <stdcorelib/pluginsystem/pluginsystem.h>

#include "pluginspec_p.h"

namespace stdc::pluginsystem {

    class PluginSystem::Impl {
    public:
        struct ResolvedDependency {
            PluginDependency::Type type;
            PluginSpecData *data;
        };

        /// Four direct dependencies cover the common case without making every map node large.
        using ResolvedDependencies = vlarray<ResolvedDependency, 4>;

        /// Most applications load fewer than 32 plugins; larger sets spill to dynamic storage.
        using PluginOrder = vlarray<PluginSpecData *, 32>;

        Impl(std::string pluginIID, PluginLayout pluginLayout);
        ~Impl();

        std::string iid;
        PluginLayout layout;
        std::unique_ptr<plugin::PluginFactory> factory;
        mutable linked_map<plugin::PluginLoader *, PluginSpecData> pluginData;
        /// Protects the path configuration, discovery cache, and transition to loadStarted.
        mutable std::shared_mutex configMtx;
        /// Serializes the one startup and shutdown sequence without blocking reentrant queries.
        std::mutex lifecycleMtx;
        bool loadStarted = false;
        bool shutdownFinished = false;

        std::map<PluginSpecData *, ResolvedDependencies> resolvedDependencies;
        PluginOrder loadOrder;

        void resolveDependencies();
        bool requiredDependenciesAtState(PluginSpecData *data, PluginSpec::State state,
                                         std::string *errorMessage) const;
        void shutdownPlugins();
        /// The caller holds configMtx exclusively when \a scan is true, and at least shared when
        /// it is false.
        std::vector<PluginSpec *> plugins(bool scan) const;
    };

}

#endif // STDCORELIB_PLUGINSYSTEM_PLUGINSYSTEM_P_H
