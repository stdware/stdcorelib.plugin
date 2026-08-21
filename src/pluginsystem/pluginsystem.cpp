// SPDX-License-Identifier: MIT

#include "pluginsystem.h"

#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <utility>

#include <stdcorelib/path.h>
#include <stdcorelib/stlextra/algorithms.h>

#include <stdcorelib/plugin/pluginfactory.h>

namespace fs = std::filesystem;

namespace stdc::pluginsystem {

    namespace {

        constexpr std::string_view pluginSystemIID = "org.stdcorelib.PluginSystem";
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

    class PluginSystem::Impl {
    public:
        explicit Impl(PluginLayout pluginLayout)
            : layout(pluginLayout == Directory ? Directory : Flat) {
            if (layout == Directory) {
                factory = std::make_unique<DirectoryPluginFactory>();
            } else {
                factory = std::make_unique<plugin::PluginFactory>();
            }
        }

        PluginLayout layout;
        std::unique_ptr<plugin::PluginFactory> factory;
        mutable std::vector<std::unique_ptr<PluginSpec>> specs;
        mutable std::map<plugin::PluginLoader *, PluginSpec *> specsByLoader;
        mutable std::mutex specs_mtx;
    };

    PluginSystem::PluginSystem(PluginLayout layout) : _impl(std::make_unique<Impl>(layout)) {
    }

    PluginSystem::~PluginSystem() = default;

    PluginSystem::PluginSystem(PluginSystem &&RHS) noexcept = default;

    PluginSystem &PluginSystem::operator=(PluginSystem &&RHS) noexcept = default;

    PluginSystem::PluginLayout PluginSystem::pluginLayout() const {
        return _impl->layout;
    }

    void PluginSystem::addPluginPath(const std::filesystem::path &path) {
        _impl->factory->addPluginPath(pluginSystemIID, path);
    }

    void PluginSystem::setPluginPaths(array_view<std::filesystem::path> paths) {
        _impl->factory->setPluginPaths(pluginSystemIID, paths);
    }

    std::vector<std::filesystem::path> PluginSystem::pluginPaths() const {
        return _impl->factory->pluginPaths(pluginSystemIID);
    }

    void PluginSystem::addStaticPlugins() {
        _impl->factory->addStaticPlugins(pluginSystemIID);
    }

    void PluginSystem::addRuntimePlugin(IPlugin *plugin, const json::Value &metadata) {
        _impl->factory->addRuntimePlugin(plugin, metadata);
    }

    std::vector<PluginSpec *> PluginSystem::plugins() const {
        auto loaders = _impl->factory->plugins(pluginSystemIID);

        std::lock_guard<std::mutex> lock(_impl->specs_mtx);
        for (auto *loader : loaders) {
            if (stdc::contains(_impl->specsByLoader, loader)) {
                continue;
            }
            auto spec = std::make_unique<PluginSpec>(*loader);
            _impl->specsByLoader.emplace(loader, spec.get());
            _impl->specs.emplace_back(std::move(spec));
        }

        std::vector<PluginSpec *> result;
        result.reserve(_impl->specs.size());
        for (const auto &spec : _impl->specs) {
            result.push_back(spec.get());
        }
        return result;
    }

    std::string_view PluginSystem::pluginIID() {
        return pluginSystemIID;
    }

}
