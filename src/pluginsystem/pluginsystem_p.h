// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGINSYSTEM_PLUGINSYSTEM_P_H
#define STDCORELIB_PLUGINSYSTEM_PLUGINSYSTEM_P_H

#include <map>
#include <set>
#include <shared_mutex>

#include <stdcorelib/adt/linked_map.h>
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

        Impl(std::string pluginIID, PluginLayout pluginLayout);

        std::string iid;
        PluginLayout layout;
        std::unique_ptr<plugin::PluginFactory> factory;
        mutable linked_map<plugin::PluginLoader *, PluginSpecData> pluginData;
        /// Protects the path configuration, discovery cache, and transition to loadStarted.
        mutable std::shared_mutex configMtx;
        bool loadStarted = false;

        std::map<PluginSpecData *, std::vector<ResolvedDependency>> resolvedDependencies;
        std::vector<PluginSpecData *> loadOrder;

        void resolveDependencies();
        bool requiredDependenciesAtState(PluginSpecData *data, PluginSpec::State state,
                                         std::string *errorMessage) const;
        /// The caller holds configMtx exclusively when \a scan is true, and at least shared when
        /// it is false.
        std::vector<PluginSpec *> plugins(bool scan) const;
    };

}

#endif // STDCORELIB_PLUGINSYSTEM_PLUGINSYSTEM_P_H
