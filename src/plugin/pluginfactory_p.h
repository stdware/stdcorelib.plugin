// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGIN_PLUGINFACTORY_P_H
#define STDCORELIB_PLUGIN_PLUGINFACTORY_P_H

#include <map>
#include <set>
#include <shared_mutex>

#include <stdcorelib/adt/vlarray.h>
#include <stdcorelib/stlextra/algorithms.h>

#include <stdcorelib/plugin/pluginfactory.h>

namespace stdc::plugin {

    class PluginFactory::Impl {
    public:
        struct Observer {
            explicit Observer(std::string pluginIID) : iid(std::move(pluginIID)) {
            }

            std::string iid;
            bool dirty = true;
        };

        Impl();
        virtual ~Impl();

    public:
        /// Builds an empty loader this factory owns.
        static std::unique_ptr<PluginLoader> createLoader();

        /// Discards filesystem loaders for \a iid that do not own a live plugin instance.
        void discardUnloadedFilePlugins(std::string_view iid) const;

        /// Reads any plugin under the directories registered for \a iid that has not been read
        /// already.
        void scanPlugins(const PluginFactory &factory, std::string_view iid) const;

        void addObserver(Observer *observer) const;
        void removeObserver(Observer *observer) const;

        /// Marks observers of \a iid for rebuilding after the loader set changes.
        ///
        /// The caller must hold \c plugins_mtx exclusively.
        void notifyObservers(std::string_view iid) const;

        /// Whether the search directories for \a iid must be scanned again.
        ///
        /// The caller must hold \c plugins_mtx.
        bool needsScan(std::string_view iid) const {
            return stdc::contains(pendingScans, iid);
        }

        std::map<std::string, vlarray<std::filesystem::path>, std::less<>> pluginPaths;

        /// Every loader, by the extension point it plugs into. The factory owns them all, which is
        /// what lets \c plugins() hand out bare pointers.
        mutable std::map<std::string, vlarray<std::unique_ptr<PluginLoader>>, std::less<>> loaders;

        /// Plugin files already turned into a loader, so that a rescan skips them.
        mutable std::set<std::filesystem::path::string_type> readPluginFiles;

        mutable std::set<std::string, std::less<>> pendingScans;
        mutable vlarray<Observer *, 1> observers;
        mutable std::shared_mutex plugins_mtx;
    };

    class BundlePluginFactory::Impl : public PluginFactory::Impl {
    public:
        explicit Impl(std::filesystem::path metadataFileName)
            : metadataFileName(std::move(metadataFileName)) {
        }

        std::filesystem::path metadataFileName;
    };

}

#endif // STDCORELIB_PLUGIN_PLUGINFACTORY_P_H
