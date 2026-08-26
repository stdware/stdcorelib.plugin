// SPDX-License-Identifier: MIT

#include "pluginsystem.h"
#include "pluginsystem_p.h"

#include <algorithm>
#include <cassert>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <utility>

#include <stdcorelib/stlextra/algorithms.h>

#include "pluginfactory.h"

namespace fs = std::filesystem;

namespace stdc::pluginsystem {

    PluginSystem::Impl::Impl(std::string pluginIID, PluginLayout pluginLayout)
        : iid(std::move(pluginIID)), layout(pluginLayout == Bundle ? Bundle : Flat) {
        assert(pluginLayout != CustomLayout);
        if (layout == Bundle) {
            factory = std::make_unique<plugin::BundlePluginFactory>();
        } else {
            factory = std::make_unique<plugin::PluginFactory>();
        }
    }

    PluginSystem::Impl::Impl(std::string pluginIID,
                             std::unique_ptr<plugin::PluginFactory> pluginFactory)
        : iid(std::move(pluginIID)), layout(CustomLayout), factory(std::move(pluginFactory)) {
        assert(factory);
    }

    PluginSystem::Impl::~Impl() {
        shutdownPlugins();
    }

    void PluginSystem::Impl::applySettings() const {
        for (auto &item : pluginData) {
            auto &data = item.second;
            data.enabledByGlobalSettings =
                globalSettings.pluginEnabled(data.id).value_or(data.enabledByMetadata);
            data.enabled =
                localSettings.pluginEnabled(data.id).value_or(data.enabledByGlobalSettings);
        }
    }

    void PluginSystem::Impl::selectPlugins() {
        // Most applications load fewer than 32 plugins. Keep predicate calls outside configMtx so
        // they can make reentrant read-only queries, then commit the results under its write lock.
        vlarray<std::pair<PluginSpecData *, bool>, 32> selections;
        for (auto &item : pluginData) {
            auto &data = item.second;
            if (data.state != PluginSpec::Read) {
                continue;
            }
            selections.emplace_back(&data, !loadPredicate || loadPredicate(data.spec));
        }

        std::unique_lock<std::shared_mutex> lock(configMtx);
        for (const auto &[data, selected] : selections) {
            data->selectedForLoad = selected;
        }
    }

    void PluginSystem::Impl::resolveDependencies() {
        resolvedDependencies.clear();
        loadOrder.clear();

        // Two inline entries detect the only interesting duplicate-ID case without a separate
        // buffer allocation.
        std::map<std::string, vlarray<PluginSpecData *, 2>, std::less<>> dataById;
        for (auto &item : pluginData) {
            auto &data = item.second;
            if (!data.id.empty()) {
                dataById[data.id].push_back(&data);
            }
        }

        for (const auto &[id, matchingData] : dataById) {
            if (matchingData.size() < 2) {
                continue;
            }
            for (auto data : matchingData) {
                data->reportError("duplicate plugin id \"" + id + "\"");
            }
        }

        for (auto &item : pluginData) {
            auto &data = item.second;
            if (data.state != PluginSpec::Read || !data.enabled || !data.selectedForLoad) {
                continue;
            }

            auto &resolved = resolvedDependencies[&data];
            for (const auto &dependency : data.dependencies) {
                auto it = dataById.find(dependency.id());
                PluginSpecData *candidate =
                    it != dataById.end() && it->second.size() == 1 ? it->second.front() : nullptr;
                if (!candidate || candidate->state == PluginSpec::Invalid) {
                    if (dependency.type() == PluginDependency::Required) {
                        data.reportError("could not resolve required dependency \"" +
                                         dependency.id() + "\"");
                        break;
                    }
                    continue;
                }
                if (!candidate->enabled) {
                    if (dependency.type() == PluginDependency::Required) {
                        data.reportError("required dependency \"" + dependency.id() +
                                         "\" is disabled");
                        break;
                    }
                    continue;
                }
                if (!candidate->selectedForLoad) {
                    if (dependency.type() == PluginDependency::Required) {
                        data.reportError("required dependency \"" + dependency.id() +
                                         "\" was not selected for loading");
                        break;
                    }
                    continue;
                }

                if (!(candidate->compatVersion <= dependency.version() &&
                      dependency.version() <= candidate->version)) {
                    if (dependency.type() == PluginDependency::Required) {
                        data.reportError("incompatible required dependency \"" + dependency.id() +
                                         "\"");
                        break;
                    }
                    continue;
                }
                resolved.push_back({dependency.type(), candidate});
            }
            if (data.state == PluginSpec::Read) {
                data.state = PluginSpec::Resolved;
            }
        }

        enum class VisitState {
            NotVisited,
            Visiting,
            Visited,
        };
        std::map<PluginSpecData *, VisitState> visitStates;
        // Dependency chains are normally shallow. Sixteen pointers keep recursive DFS off-heap.
        vlarray<PluginSpecData *, 16> stack;
        std::map<PluginSpecData *, std::string> cycleErrors;
        std::function<void(PluginSpecData *)> findCycles = [&](PluginSpecData *data) {
            visitStates[data] = VisitState::Visiting;
            stack.push_back(data);
            for (const auto &dependency : resolvedDependencies[data]) {
                auto next = dependency.data;
                if (next->state != PluginSpec::Resolved) {
                    continue;
                }
                if (visitStates[next] == VisitState::NotVisited) {
                    findCycles(next);
                    continue;
                }
                if (visitStates[next] != VisitState::Visiting) {
                    continue;
                }

                auto first = std::find(stack.begin(), stack.end(), next);
                std::string path = "circular dependency: ";
                for (auto it = first; it != stack.end(); ++it) {
                    if (it != first) {
                        path += " -> ";
                    }
                    path += (*it)->id;
                }
                path += " -> " + next->id;
                for (auto it = first; it != stack.end(); ++it) {
                    cycleErrors[*it] = path;
                }
            }
            stack.pop_back();
            visitStates[data] = VisitState::Visited;
        };

        for (auto &item : pluginData) {
            auto &data = item.second;
            if (data.state == PluginSpec::Resolved &&
                visitStates[&data] == VisitState::NotVisited) {
                findCycles(&data);
            }
        }
        for (auto &[data, message] : cycleErrors) {
            data->reportError(std::move(message));
        }

        bool changed;
        do {
            changed = false;
            for (auto &item : pluginData) {
                auto &data = item.second;
                if (data.state != PluginSpec::Resolved) {
                    continue;
                }
                for (const auto &dependency : resolvedDependencies[&data]) {
                    if (dependency.type == PluginDependency::Required &&
                        dependency.data->state != PluginSpec::Resolved) {
                        data.reportError("required dependency \"" + dependency.data->id +
                                         "\" is invalid");
                        changed = true;
                        break;
                    }
                }
            }
        } while (changed);

        std::set<PluginSpecData *> added;
        std::function<void(PluginSpecData *)> append = [&](PluginSpecData *data) {
            if (stdc::contains(added, data) || !data->enabled || !data->selectedForLoad ||
                data->state != PluginSpec::Resolved) {
                return;
            }
            for (const auto &dependency : resolvedDependencies[data]) {
                append(dependency.data);
            }
            added.insert(data);
            loadOrder.push_back(data);
        };
        for (auto &item : pluginData) {
            auto &data = item.second;
            append(&data);
        }
    }

    bool PluginSystem::Impl::requiredDependenciesAtState(PluginSpecData *data,
                                                         PluginSpec::State state,
                                                         std::string *errorMessage) const {
        auto it = resolvedDependencies.find(data);
        if (it == resolvedDependencies.end()) {
            return true;
        }
        for (const auto &dependency : it->second) {
            if (dependency.type != PluginDependency::Required) {
                continue;
            }
            if (dependency.data->state != state) {
                *errorMessage = "required dependency \"" + dependency.data->id +
                                "\" did not reach the required state";
                return false;
            }
        }
        return true;
    }

    std::vector<PluginSpec *> PluginSystem::Impl::plugins(bool scan) const {
        std::vector<plugin::PluginLoader *> loaders;
        if (scan) {
            loaders = factory->plugins(iid);
            loaders.erase(std::remove_if(loaders.begin(), loaders.end(),
                                         [](const auto loader) {
                                             return loader->origin() !=
                                                    plugin::PluginLoader::FileSystem;
                                         }),
                          loaders.end());
            for (auto it = pluginData.begin(); it != pluginData.end();) {
                if (std::find(loaders.begin(), loaders.end(), it->first) == loaders.end()) {
                    it = pluginData.erase(it);
                } else {
                    ++it;
                }
            }
            for (auto loader : loaders) {
                pluginData.try_emplace(loader, *loader);
            }
            // Scanning is always performed under configMtx exclusively. Do not rewrite enabled
            // states on the shared-lock query path after the plugin snapshot has frozen.
            applySettings();
        }

        std::vector<PluginSpec *> result;
        result.reserve(pluginData.size());
        for (auto &item : pluginData) {
            auto &data = item.second;
            result.push_back(&data.spec);
        }
        return result;
    }

    void PluginSystem::Impl::shutdownPlugins() {
        std::lock_guard<std::mutex> lifecycleLock(lifecycleMtx);
        if (!loadStarted || shutdownFinished) {
            return;
        }
        shutdownFinished = true;

        for (auto it = loadOrder.rbegin(); it != loadOrder.rend(); ++it) {
            auto data = *it;
            if (data->state == PluginSpec::Running) {
                data->plugin->aboutToShutdown();
            }
        }

        for (auto it = loadOrder.rbegin(); it != loadOrder.rend(); ++it) {
            auto data = *it;
            if (!data->loader->isLoaded()) {
                continue;
            }
            if (!data->loader->unload()) {
                data->reportError(data->loader->errorMessage(), data->state);
                continue;
            }
            data->plugin = nullptr;
            data->state = PluginSpec::Stopped;
        }
    }

    PluginSystem::PluginSystem(std::string_view iid, PluginLayout layout)
        : _impl(std::make_unique<Impl>(std::string(iid), layout)) {
    }

    PluginSystem::PluginSystem(std::string_view iid, std::unique_ptr<plugin::PluginFactory> factory)
        : _impl(std::make_unique<Impl>(std::string(iid), std::move(factory))) {
    }

    PluginSystem::~PluginSystem() = default;

    PluginSystem::PluginSystem(PluginSystem &&RHS) noexcept = default;

    PluginSystem &PluginSystem::operator=(PluginSystem &&RHS) noexcept = default;

    const std::string &PluginSystem::iid() const {
        stdc_impl_t;
        return impl.iid;
    }

    PluginSystem::PluginLayout PluginSystem::pluginLayout() const {
        stdc_impl_t;
        return impl.layout;
    }

    void PluginSystem::setPluginPaths(array_view<std::filesystem::path> paths) {
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.configMtx);
        if (impl.loadStarted) {
            return;
        }
        impl.factory->setPluginPaths(impl.iid, paths);
    }

    std::vector<std::filesystem::path> PluginSystem::pluginPaths() const {
        stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.configMtx);
        return impl.factory->pluginPaths(impl.iid);
    }

    void PluginSystem::setPluginSettings(SettingsScope scope, PluginSettings settings) {
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.configMtx);
        if (impl.loadStarted) {
            return;
        }
        if (scope == Local) {
            impl.localSettings = std::move(settings);
        } else {
            impl.globalSettings = std::move(settings);
        }
        impl.applySettings();
    }

    void PluginSystem::setPluginLoadPredicate(PluginLoadPredicate predicate) {
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.configMtx);
        if (impl.loadStarted) {
            return;
        }
        impl.loadPredicate = std::move(predicate);
    }

    PluginSettings PluginSystem::pluginSettings(SettingsScope scope) const {
        stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.configMtx);
        return scope == Local ? impl.localSettings : impl.globalSettings;
    }

    std::vector<PluginSpec *> PluginSystem::plugins() const {
        stdc_impl_t;
        std::shared_lock<std::shared_mutex> readLock(impl.configMtx);
        if (impl.loadStarted) {
            return impl.plugins(false);
        }
        readLock.unlock();

        std::unique_lock<std::shared_mutex> writeLock(impl.configMtx);
        return impl.plugins(!impl.loadStarted);
    }

    void PluginSystem::loadPlugins() {
        stdc_impl_t;
        std::lock_guard<std::mutex> lifecycleLock(impl.lifecycleMtx);
        {
            std::unique_lock<std::shared_mutex> lock(impl.configMtx);
            if (impl.loadStarted) {
                return;
            }
            impl.plugins(true);
            impl.loadStarted = true;
        }

        impl.selectPlugins();
        impl.resolveDependencies();

        for (auto data : impl.loadOrder) {
            std::string errorMessage;
            if (!impl.requiredDependenciesAtState(data, PluginSpec::Loaded, &errorMessage)) {
                data->reportError(std::move(errorMessage), data->state);
                continue;
            }
            if (!data->loader->load()) {
                data->reportError(data->loader->errorMessage(), data->state);
                continue;
            }
            data->plugin = dynamic_cast<IPlugin *>(data->loader->plugin());
            if (!data->plugin) {
                data->reportError("loaded plugin does not implement IPlugin", data->state);
                continue;
            }
            data->state = PluginSpec::Loaded;
        }

        for (auto data : impl.loadOrder) {
            if (data->state != PluginSpec::Loaded || data->spec.hasError()) {
                continue;
            }
            std::string errorMessage;
            if (!impl.requiredDependenciesAtState(data, PluginSpec::Initialized, &errorMessage)) {
                data->reportError(std::move(errorMessage), data->state);
                continue;
            }
            if (!data->plugin->initialize(&errorMessage)) {
                if (errorMessage.empty()) {
                    errorMessage = "plugin initialization failed";
                }
                data->reportError(std::move(errorMessage), data->state);
                continue;
            }
            data->state = PluginSpec::Initialized;
        }

        for (auto it = impl.loadOrder.rbegin(); it != impl.loadOrder.rend(); ++it) {
            auto data = *it;
            if (data->state != PluginSpec::Initialized || data->spec.hasError()) {
                continue;
            }
            data->plugin->pluginsInitialized();
            data->state = PluginSpec::Running;
        }
    }

    void PluginSystem::shutdownPlugins() {
        stdc_impl_t;
        impl.shutdownPlugins();
    }

    bool PluginSystem::hasError() const {
        stdc_impl_t;
        const auto containsError = [](const std::vector<PluginSpec *> &specs) {
            for (auto spec : specs) {
                if (spec->hasError()) {
                    return true;
                }
            }
            return false;
        };

        std::shared_lock<std::shared_mutex> readLock(impl.configMtx);
        if (impl.loadStarted) {
            return containsError(impl.plugins(false));
        }
        readLock.unlock();

        std::unique_lock<std::shared_mutex> writeLock(impl.configMtx);
        return containsError(impl.plugins(!impl.loadStarted));
    }

}
