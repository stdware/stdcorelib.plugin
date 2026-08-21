// SPDX-License-Identifier: MIT

#include "pluginsystem.h"
#include "pluginsystem_p.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <utility>

#include <stdcorelib/path.h>
#include <stdcorelib/stlextra/algorithms.h>

#include <stdcorelib/plugin/pluginfactory.h>

namespace fs = std::filesystem;

namespace stdc::pluginsystem {

    namespace {

        constexpr const char *manifestName = "plugin.json";

        class DirectoryPluginFactory final : public plugin::PluginFactory {
        protected:
            bool scanPluginPaths(const fs::path &path,
                                 std::vector<fs::path> *pluginPaths) const override {
                std::error_code ec;
                fs::directory_iterator dir(path, ec);
                if (ec) {
                    return false;
                }

                const fs::directory_iterator end;
                while (dir != end) {
                    if (dir->is_directory(ec) && !ec &&
                        fs::is_regular_file(dir->path() / manifestName, ec) && !ec) {
                        pluginPaths->push_back(dir->path());
                    }
                    ec.clear();
                    dir.increment(ec);
                    if (ec) {
                        return false;
                    }
                }
                return true;
            }

            bool resolvePluginPath(const fs::path &path, fs::path *pluginPath,
                                   std::optional<fs::path> *metadataPath) const override {
                auto manifest = path / manifestName;
                std::ifstream file(manifest);
                if (!file.is_open()) {
                    return false;
                }

                std::stringstream stream;
                stream << file.rdbuf();
                json::ParseError parseError;
                auto root = json::Value::fromJson(stream.str(), true, &parseError);
                if (parseError || !root.isObject()) {
                    return false;
                }

                const auto &binary = root["binary"];
                if (!binary.isString() || binary.toString().empty()) {
                    return false;
                }

                *pluginPath = path / stdc::path::from_utf8(binary.toString());
                *metadataPath = std::move(manifest);
                return true;
            }
        };

    }

    PluginSystem::Impl::Impl(std::string pluginIID, PluginLayout pluginLayout)
        : iid(std::move(pluginIID)), layout(pluginLayout == Directory ? Directory : Flat) {
        if (layout == Directory) {
            factory = std::make_unique<DirectoryPluginFactory>();
        } else {
            factory = std::make_unique<plugin::PluginFactory>();
        }
    }

    void PluginSystem::Impl::resolveDependencies() {
        resolvedDependencies.clear();
        loadOrder.clear();

        std::map<std::string, std::vector<PluginSpecData *>, std::less<>> dataById;
        for (auto &data : pluginData) {
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

        for (auto &data : pluginData) {
            if (data.state != PluginSpec::Read) {
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

        std::map<PluginSpecData *, int> colors;
        std::vector<PluginSpecData *> stack;
        std::map<PluginSpecData *, std::string> cycleErrors;
        std::function<void(PluginSpecData *)> findCycles = [&](PluginSpecData *data) {
            colors[data] = 1;
            stack.push_back(data);
            for (const auto &dependency : resolvedDependencies[data]) {
                auto next = dependency.data;
                if (next->state != PluginSpec::Resolved) {
                    continue;
                }
                if (colors[next] == 0) {
                    findCycles(next);
                    continue;
                }
                if (colors[next] != 1) {
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
            colors[data] = 2;
        };

        for (auto &data : pluginData) {
            if (data.state == PluginSpec::Resolved && colors[&data] == 0) {
                findCycles(&data);
            }
        }
        for (auto &[data, message] : cycleErrors) {
            data->reportError(std::move(message));
        }

        bool changed;
        do {
            changed = false;
            for (auto &data : pluginData) {
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
            if (stdc::contains(added, data) || data->state != PluginSpec::Resolved) {
                return;
            }
            for (const auto &dependency : resolvedDependencies[data]) {
                append(dependency.data);
            }
            added.insert(data);
            loadOrder.push_back(data);
        };
        for (auto &data : pluginData) {
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
        }

        std::lock_guard<std::mutex> lock(specsMtx);
        for (auto loader : loaders) {
            if (stdc::contains(dataByLoader, loader)) {
                continue;
            }
            pluginData.emplace_back(*loader);
            dataByLoader.emplace(loader, &pluginData.back());
        }

        std::vector<PluginSpec *> result;
        result.reserve(pluginData.size());
        for (auto &data : pluginData) {
            result.push_back(&data.spec);
        }
        return result;
    }

    PluginSystem::PluginSystem(std::string_view iid, PluginLayout layout)
        : _impl(std::make_unique<Impl>(std::string(iid), layout)) {
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
        std::lock_guard<std::mutex> lock(impl.configMtx);
        if (impl.loadStarted) {
            return;
        }
        impl.factory->setPluginPaths(impl.iid, paths);
    }

    std::vector<std::filesystem::path> PluginSystem::pluginPaths() const {
        stdc_impl_t;
        std::lock_guard<std::mutex> lock(impl.configMtx);
        return impl.factory->pluginPaths(impl.iid);
    }

    std::vector<PluginSpec *> PluginSystem::plugins() const {
        stdc_impl_t;
        std::lock_guard<std::mutex> lock(impl.configMtx);
        return impl.plugins(!impl.loadStarted);
    }

    void PluginSystem::loadPlugins() {
        stdc_impl_t;
        {
            std::lock_guard<std::mutex> lock(impl.configMtx);
            if (impl.loadStarted) {
                return;
            }
            impl.plugins(true);
            impl.loadStarted = true;
        }

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
            data->plugin->pluginInitialized();
            data->state = PluginSpec::Running;
        }
    }

    bool PluginSystem::hasError() const {
        stdc_impl_t;
        std::lock_guard<std::mutex> lock(impl.configMtx);
        for (auto spec : impl.plugins(!impl.loadStarted)) {
            if (spec->hasError()) {
                return true;
            }
        }
        return false;
    }

}
