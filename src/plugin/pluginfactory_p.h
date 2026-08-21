// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGIN_PLUGINFACTORY_P_H
#define STDCORELIB_PLUGIN_PLUGINFACTORY_P_H

#include <map>
#include <set>
#include <shared_mutex>

#include <stdcorelib/adt/vlarray.h>

#include <stdcorelib/plugin/pluginfactory.h>

namespace stdc {

    class STDC_PLUGIN_EXPORT PluginFactory::Impl {
    public:
        explicit Impl(PluginFactory *decl);
        virtual ~Impl();

        using Decl = PluginFactory;
        PluginFactory *_decl;

    public:
        /// Builds an empty spec this factory owns.
        static PluginSpec *createSpec();

        /// Reads any manifest under the directories registered for \a iid that has not been read
        /// already.
        ///
        /// Scanning only ever adds. A spec that exists may have been handed out and may have been
        /// loaded, so rescanning must not take it away.
        void scanPlugins(const char *iid) const;

        std::map<std::string, vlarray<std::filesystem::path>, std::less<>> pluginPaths;

        /// Every spec, by the extension point it plugs into. The factory owns them all, which is
        /// what lets \c plugins() hand out bare pointers.
        mutable std::map<std::string, vlarray<std::unique_ptr<PluginSpec>>, std::less<>> specs;

        /// Manifests already turned into a spec, so that a rescan skips them.
        mutable std::set<std::filesystem::path::string_type> readManifests;

        mutable std::set<std::string, std::less<>> pluginsDirty;
        mutable std::shared_mutex plugins_mtx;
    };

}

#endif // STDCORELIB_PLUGIN_PLUGINFACTORY_P_H
