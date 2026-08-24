// SPDX-License-Identifier: MIT

#include <stdcorelib/plugin/pluginfactory.h>
#include <stdcorelib/pluginsystem/plugindependency.h>
#include <stdcorelib/pluginsystem/pluginsystem.h>
#include <stdcorelib/support/sharedlibrary.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>

#include <boost/test/unit_test.hpp>

namespace {

    class TemporaryPluginSystemDirectory {
    public:
        explicit TemporaryPluginSystemDirectory(
            stdc::pluginsystem::PluginSystem::PluginLayout layout, bool addDefaultPlugin = true) {
            auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
            _path = std::filesystem::temp_directory_path() /
                    ("stdcorelib-pluginsystem-test-" + std::to_string(suffix));
            std::filesystem::create_directories(_path);

            if (!addDefaultPlugin) {
                return;
            }

            const auto source = std::filesystem::path(TEST_PLUGINSYSTEM_PLUGIN_PATH);
            if (layout == stdc::pluginsystem::PluginSystem::Flat) {
                std::filesystem::copy_file(source, _path / source.filename());
            } else {
                addPlugin(
                    "plugin",
                    R"({"id":"org.stdcorelib.PluginSystemTest","name":"PluginSystem Test","version":"2.1.0"})");
            }
        }

        ~TemporaryPluginSystemDirectory() {
            std::error_code ec;
            std::filesystem::remove_all(_path, ec);
        }

        std::filesystem::path
            addPlugin(const std::string &directoryName, std::string_view metadata,
                      const std::filesystem::path &source = TEST_PLUGINSYSTEM_PLUGIN_PATH,
                      std::string_view iid = "org.stdcorelib.PluginSystem",
                      std::string_view manifestData = {}) {
            const auto pluginDirectory = _path / directoryName;
            std::filesystem::create_directories(pluginDirectory);
            const auto pluginPath = pluginDirectory / source.filename();
            std::filesystem::copy_file(source, pluginPath);

            std::ofstream manifest(pluginDirectory / "plugin.json");
            manifest << R"({"iid":")" << iid << R"(","name":")" << source.stem().string()
                     << R"(","metadata":)" << metadata;
            if (!manifestData.empty()) {
                manifest << "," << manifestData;
            }
            manifest << "}";
            return pluginPath;
        }

        std::filesystem::path
            addCustomPlugin(const std::string &directoryName, std::string_view metadata,
                            const std::filesystem::path &source = TEST_PLUGINSYSTEM_PLUGIN_PATH) {
            const auto pluginDirectory = _path / "vendor" / directoryName;
            const auto libraryDirectory = pluginDirectory / "bin";
            std::filesystem::create_directories(libraryDirectory);
            const auto pluginPath = libraryDirectory / source.filename();
            std::filesystem::copy_file(source, pluginPath);

            std::ofstream manifest(pluginDirectory / "extension.json");
            manifest << R"({"iid":"org.stdcorelib.PluginSystem","name":")" << source.stem().string()
                     << R"(","file":")" << source.filename().string() << R"(","metadata":)"
                     << metadata << "}";
            return pluginPath;
        }

        const std::filesystem::path &path() const {
            return _path;
        }

    private:
        std::filesystem::path _path;
    };

    class CustomPluginFactory final : public stdc::plugin::BundlePluginFactory {
    public:
        CustomPluginFactory() : BundlePluginFactory("extension.json") {
        }

    protected:
        bool scanPluginPaths(const std::filesystem::path &path,
                             std::vector<std::filesystem::path> *pluginPaths) const override {
            std::error_code ec;
            std::filesystem::recursive_directory_iterator dir(path, ec);
            if (ec) {
                return false;
            }

            const std::filesystem::recursive_directory_iterator end;
            while (dir != end) {
                if (dir->is_regular_file(ec) && !ec && dir->path().filename() == "extension.json") {
                    pluginPaths->push_back(dir->path().parent_path());
                }
                ec.clear();
                dir.increment(ec);
                if (ec) {
                    return false;
                }
            }
            std::sort(pluginPaths->begin(), pluginPaths->end());
            return true;
        }

        std::optional<std::filesystem::path>
            resolveLibraryPath(const std::filesystem::path &bundlePath,
                               const stdc::json::Value &manifest) const override {
            const auto &file = manifest["file"];
            if (!file.isString()) {
                return std::nullopt;
            }
            const auto pluginPath = bundlePath / "bin" / file.toString();
            if (!stdc::SharedLibrary::isLibrary(pluginPath)) {
                return std::nullopt;
            }
            return pluginPath;
        }
    };

    class ScanPluginFactory final : public stdc::plugin::PluginFactory {
    public:
        bool scan(const std::filesystem::path &path,
                  std::vector<std::filesystem::path> *pluginPaths) const {
            return scanPluginPaths(path, pluginPaths);
        }
    };

    class ScanBundlePluginFactory final : public stdc::plugin::BundlePluginFactory {
    public:
        bool scan(const std::filesystem::path &path,
                  std::vector<std::filesystem::path> *pluginPaths) const {
            return scanPluginPaths(path, pluginPaths);
        }
    };

    class RuntimePlugin final : public stdc::plugin::Plugin {};

    stdc::pluginsystem::PluginSpec *
        findPlugin(const std::vector<stdc::pluginsystem::PluginSpec *> &specs,
                   std::string_view id) {
        auto it = std::find_if(specs.begin(), specs.end(),
                               [id](const auto spec) { return spec->id() == id; });
        return it == specs.end() ? nullptr : *it;
    }

    std::vector<std::string> lifecycleEvents;
    stdc::pluginsystem::PluginSystem *activePluginSystem = nullptr;

    void recordLifecycleEvent(const char *plugin, const char *event) {
        lifecycleEvents.push_back(std::string(plugin) + "." + event);
        if (activePluginSystem) {
            activePluginSystem->plugins();
            activePluginSystem->hasError();
        }
    }

    void setLifecycleCallback(stdc::SharedLibrary *library,
                              const std::filesystem::path &pluginPath) {
        BOOST_REQUIRE_MESSAGE(library->open(pluginPath), library->errorMessage());
        using SetCallback = void (*)(void (*)(const char *, const char *));
        auto setter =
            reinterpret_cast<SetCallback>(library->resolve("test_pluginsystem_set_event_callback"));
        BOOST_REQUIRE_MESSAGE(setter, library->errorMessage());
        setter(&recordLifecycleEvent);
    }

    void checkPluginSystemLayout(stdc::pluginsystem::PluginSystem::PluginLayout layout) {
        TemporaryPluginSystemDirectory directory(layout);
        stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem", layout);
        BOOST_CHECK_EQUAL(system.pluginLayout(), layout);
        BOOST_CHECK_EQUAL(system.iid(), "org.stdcorelib.PluginSystem");
        system.setPluginPaths(directory.path());

        const auto specs = system.plugins();
        BOOST_REQUIRE_EQUAL(specs.size(), 1u);
        BOOST_CHECK(!specs.front()->plugin());
        BOOST_CHECK_EQUAL(specs.front()->state(), stdc::pluginsystem::PluginSpec::Read);
        BOOST_CHECK(!specs.front()->hasError());
        BOOST_CHECK_EQUAL(specs.front()->id(), "org.stdcorelib.PluginSystemTest");
        BOOST_CHECK_EQUAL(specs.front()->name(), "PluginSystem Test");
        BOOST_CHECK_EQUAL(specs.front()->version(), stdc::VersionNumber(2, 1));
        BOOST_CHECK_EQUAL(specs.front()->compatVersion(), specs.front()->version());

        const auto rescanned = system.plugins();
        BOOST_REQUIRE_EQUAL(rescanned.size(), 1u);
        BOOST_CHECK_EQUAL(rescanned.front(), specs.front());

        system.shutdownPlugins();
        BOOST_CHECK_EQUAL(specs.front()->state(), stdc::pluginsystem::PluginSpec::Read);

        const auto pathsBeforeLoad = system.pluginPaths();
        system.loadPlugins();
        BOOST_CHECK_EQUAL(specs.front()->state(), stdc::pluginsystem::PluginSpec::Running);
        BOOST_CHECK(specs.front()->plugin());
        BOOST_CHECK(!system.hasError());

        const std::vector<std::filesystem::path> noPaths;
        system.setPluginPaths(noPaths);
        const auto pathsAfterSet = system.pluginPaths();
        BOOST_CHECK_EQUAL_COLLECTIONS(pathsAfterSet.begin(), pathsAfterSet.end(),
                                      pathsBeforeLoad.begin(), pathsBeforeLoad.end());

        system.setPluginPaths(directory.path());
        BOOST_CHECK_EQUAL(system.pluginPaths().size(), pathsBeforeLoad.size());

        system.loadPlugins();
        BOOST_CHECK_EQUAL(specs.front()->state(), stdc::pluginsystem::PluginSpec::Running);

        system.shutdownPlugins();
        BOOST_CHECK_EQUAL(specs.front()->state(), stdc::pluginsystem::PluginSpec::Stopped);
        BOOST_CHECK(!specs.front()->plugin());
        system.shutdownPlugins();
        BOOST_CHECK_EQUAL(specs.front()->state(), stdc::pluginsystem::PluginSpec::Stopped);
    }

}

BOOST_AUTO_TEST_SUITE(test_pluginsystem)

BOOST_AUTO_TEST_CASE(test_dependency_value) {
    const stdc::pluginsystem::PluginDependency dependency(
        "org.stdcorelib.Dependency", stdc::VersionNumber(1, 2),
        stdc::pluginsystem::PluginDependency::Optional);

    BOOST_CHECK_EQUAL(dependency.id(), "org.stdcorelib.Dependency");
    BOOST_CHECK_EQUAL(dependency.version(), stdc::VersionNumber(1, 2));
    BOOST_CHECK_EQUAL(dependency.type(), stdc::pluginsystem::PluginDependency::Optional);
}

BOOST_AUTO_TEST_CASE(test_flat_layout) {
    checkPluginSystemLayout(stdc::pluginsystem::PluginSystem::Flat);
}

BOOST_AUTO_TEST_CASE(test_bundle_layout) {
    checkPluginSystemLayout(stdc::pluginsystem::PluginSystem::Bundle);
}

BOOST_AUTO_TEST_CASE(test_default_factory_scans_sort_paths) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Flat, false);

    std::vector<std::filesystem::path> pluginPaths{directory.path() / "z", directory.path() / "a"};
    ScanPluginFactory flatFactory;
    BOOST_REQUIRE(flatFactory.scan(directory.path(), &pluginPaths));
    BOOST_CHECK(std::is_sorted(pluginPaths.begin(), pluginPaths.end()));

    pluginPaths = {directory.path() / "z", directory.path() / "a"};
    ScanBundlePluginFactory bundleFactory;
    BOOST_REQUIRE(bundleFactory.scan(directory.path(), &pluginPaths));
    BOOST_CHECK(std::is_sorted(pluginPaths.begin(), pluginPaths.end()));
}

BOOST_AUTO_TEST_CASE(test_custom_layout_factory) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::CustomLayout, false);
    directory.addCustomPlugin("plugin", R"({"id":"Custom","name":"Custom","version":"1.0"})");

    RuntimePlugin runtimePlugin;
    auto factory = std::make_unique<CustomPluginFactory>();
    BOOST_CHECK_EQUAL(factory->manifestFileName(), std::filesystem::path("extension.json"));
    factory->addRuntimePlugin(
        &runtimePlugin,
        stdc::json::Object{
            {"iid",      "org.stdcorelib.PluginSystem"                                     },
            {"metadata",
             stdc::json::Object{{"id", "Runtime"}, {"name", "Runtime"}, {"version", "1.0"}}},
    });

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem", std::move(factory));
    BOOST_CHECK_EQUAL(system.pluginLayout(), stdc::pluginsystem::PluginSystem::CustomLayout);
    system.setPluginPaths(directory.path());

    const auto specs = system.plugins();
    BOOST_REQUIRE_EQUAL(specs.size(), 1u);
    BOOST_CHECK_EQUAL(specs.front()->id(), "Custom");

    system.loadPlugins();
    BOOST_CHECK_EQUAL(specs.front()->state(), stdc::pluginsystem::PluginSpec::Running);
}

BOOST_AUTO_TEST_CASE(test_bundle_layout_resolves_library_prefix) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Bundle, false);
    const auto original =
        directory.addPlugin("plugin", R"({"id":"Plugin","name":"Plugin","version":"1.0"})");
    const auto prefixed = original.parent_path() / ("lib" + original.filename().string());
    std::filesystem::rename(original, prefixed);

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Bundle);
    system.setPluginPaths(directory.path());

    const auto specs = system.plugins();
    BOOST_REQUIRE_EQUAL(specs.size(), 1u);
    BOOST_CHECK_EQUAL(specs.front()->id(), "Plugin");
}

BOOST_AUTO_TEST_CASE(test_replacing_paths_discards_unloaded_specs) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Flat);
    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem");
    system.setPluginPaths(directory.path());
    BOOST_REQUIRE_EQUAL(system.plugins().size(), 1u);

    const std::vector<std::filesystem::path> noPaths;
    system.setPluginPaths(noPaths);
    BOOST_CHECK(system.plugins().empty());

    system.setPluginPaths(directory.path());
    BOOST_CHECK_EQUAL(system.plugins().size(), 1u);
}

BOOST_AUTO_TEST_CASE(test_concurrent_frozen_queries) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Bundle);
    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Bundle);
    system.setPluginPaths(directory.path());
    system.loadPlugins();

    std::atomic<int> readyThreads = 0;
    std::atomic<bool> start = false;
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&] {
            ++readyThreads;
            while (!start) {
                std::this_thread::yield();
            }
            for (int j = 0; j < 50; ++j) {
                system.plugins();
                system.hasError();
            }
        });
    }
    while (readyThreads != 4) {
        std::this_thread::yield();
    }
    start = true;
    for (auto &thread : threads) {
        thread.join();
    }

    BOOST_CHECK_EQUAL(system.plugins().front()->state(), stdc::pluginsystem::PluginSpec::Running);
}

BOOST_AUTO_TEST_CASE(test_constructor_iid_selects_plugins) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Bundle, false);
    directory.addPlugin("plugin", R"({"id":"Custom","name":"Custom","version":"1.0"})",
                        TEST_PLUGINSYSTEM_PLUGIN_PATH, "org.example.CustomPluginSystem");

    stdc::pluginsystem::PluginSystem matching("org.example.CustomPluginSystem",
                                              stdc::pluginsystem::PluginSystem::Bundle);
    matching.setPluginPaths(directory.path());
    BOOST_REQUIRE_EQUAL(matching.plugins().size(), 1u);

    stdc::pluginsystem::PluginSystem mismatched("org.example.OtherPluginSystem",
                                                stdc::pluginsystem::PluginSystem::Bundle);
    mismatched.setPluginPaths(directory.path());
    BOOST_CHECK(mismatched.plugins().empty());
}

BOOST_AUTO_TEST_CASE(test_dependency_metadata) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Bundle, false);
    directory.addPlugin(
        "plugin",
        R"({"id":"Consumer","name":"Consumer","version":"1.0","dependencies":[{"id":"Provider","version":"2.0","type":"optional"}]})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Bundle);
    system.setPluginPaths(directory.path());
    const auto specs = system.plugins();
    BOOST_REQUIRE_EQUAL(specs.size(), 1u);
    BOOST_REQUIRE_EQUAL(specs.front()->dependencies().size(), 1u);
    BOOST_CHECK_EQUAL(specs.front()->dependencies().front().id(), "Provider");
    BOOST_CHECK_EQUAL(specs.front()->dependencies().front().version(), stdc::VersionNumber(2));
    BOOST_CHECK_EQUAL(specs.front()->dependencies().front().type(),
                      stdc::pluginsystem::PluginDependency::Optional);
}

BOOST_AUTO_TEST_CASE(test_spec_exposes_complete_manifest) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Bundle, false);
    directory.addPlugin("plugin", R"({"id":"Plugin","name":"Plugin","version":"1.0"})",
                        TEST_PLUGINSYSTEM_PLUGIN_PATH, "org.stdcorelib.PluginSystem",
                        R"("applicationData":{"answer":42})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Bundle);
    system.setPluginPaths(directory.path());
    auto spec = findPlugin(system.plugins(), "Plugin");
    BOOST_REQUIRE(spec);

    const auto &manifest = spec->manifest();
    BOOST_CHECK_EQUAL(manifest["iid"].toString(), "org.stdcorelib.PluginSystem");
    BOOST_CHECK_EQUAL(manifest["applicationData"]["answer"].toInt(), 42);
    BOOST_CHECK(!manifest["name"].toString().empty());
}

BOOST_AUTO_TEST_CASE(test_global_and_local_settings_precedence_and_freeze_at_load) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Bundle, false);
    directory.addPlugin(
        "plugin", R"({"id":"Plugin","name":"Plugin","version":"1.0","enabledByDefault":false})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Bundle);
    system.setPluginPaths(directory.path());
    auto spec = findPlugin(system.plugins(), "Plugin");
    BOOST_REQUIRE(spec);
    BOOST_CHECK(!spec->enabledByGlobalSettings());
    BOOST_CHECK(!spec->isEnabled());

    stdc::pluginsystem::PluginSettings globalSettings;
    globalSettings.setPluginEnabled("Plugin", true);
    stdc::pluginsystem::PluginSettings localSettings;
    localSettings.setPluginEnabled("Plugin", false);
    system.setPluginSettings(stdc::pluginsystem::PluginSystem::Global, globalSettings);
    system.setPluginSettings(stdc::pluginsystem::PluginSystem::Local, localSettings);
    BOOST_CHECK(spec->enabledByGlobalSettings());
    BOOST_CHECK(!spec->isEnabled());

    localSettings.setPluginEnabled("Plugin", std::nullopt);
    system.setPluginSettings(stdc::pluginsystem::PluginSystem::Local, localSettings);
    BOOST_CHECK(spec->enabledByGlobalSettings());
    BOOST_CHECK(spec->isEnabled());

    globalSettings.setPluginEnabled("Plugin", false);
    localSettings.setPluginEnabled("Plugin", true);
    system.setPluginSettings(stdc::pluginsystem::PluginSystem::Global, globalSettings);
    system.setPluginSettings(stdc::pluginsystem::PluginSystem::Local, localSettings);
    BOOST_CHECK(!spec->enabledByGlobalSettings());
    BOOST_CHECK(spec->isEnabled());

    system.loadPlugins();
    BOOST_CHECK_EQUAL(spec->state(), stdc::pluginsystem::PluginSpec::Running);

    globalSettings.setPluginEnabled("Plugin", true);
    localSettings.setPluginEnabled("Plugin", false);
    system.setPluginSettings(stdc::pluginsystem::PluginSystem::Global, globalSettings);
    system.setPluginSettings(stdc::pluginsystem::PluginSystem::Local, localSettings);
    BOOST_CHECK(!spec->enabledByGlobalSettings());
    BOOST_CHECK(spec->isEnabled());
    const auto frozenGlobal =
        system.pluginSettings(stdc::pluginsystem::PluginSystem::Global).pluginEnabled("Plugin");
    const auto frozenLocal =
        system.pluginSettings(stdc::pluginsystem::PluginSystem::Local).pluginEnabled("Plugin");
    BOOST_REQUIRE(frozenGlobal);
    BOOST_CHECK(!*frozenGlobal);
    BOOST_REQUIRE(frozenLocal);
    BOOST_CHECK(*frozenLocal);
}

BOOST_AUTO_TEST_CASE(test_disabled_dependencies) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Bundle, false);
    directory.addPlugin("provider", R"({"id":"Provider","name":"Provider","version":"1.0"})");
    directory.addPlugin(
        "required",
        R"({"id":"Required","name":"Required","version":"1.0","dependencies":[{"id":"Provider","version":"1.0","type":"required"}]})");
    directory.addPlugin(
        "optional",
        R"({"id":"Optional","name":"Optional","version":"1.0","dependencies":[{"id":"Provider","version":"1.0","type":"optional"}]})");

    stdc::pluginsystem::PluginSettings settings;
    settings.setPluginEnabled("Provider", false);

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Bundle);
    system.setPluginPaths(directory.path());
    system.setPluginSettings(stdc::pluginsystem::PluginSystem::Local, settings);
    system.loadPlugins();

    const auto specs = system.plugins();
    auto provider = findPlugin(specs, "Provider");
    auto required = findPlugin(specs, "Required");
    auto optional = findPlugin(specs, "Optional");
    BOOST_REQUIRE(provider);
    BOOST_REQUIRE(required);
    BOOST_REQUIRE(optional);
    BOOST_CHECK(!provider->isEnabled());
    BOOST_CHECK_EQUAL(provider->state(), stdc::pluginsystem::PluginSpec::Read);
    BOOST_CHECK(!provider->hasError());
    BOOST_CHECK_EQUAL(required->state(), stdc::pluginsystem::PluginSpec::Invalid);
    BOOST_CHECK(required->errorMessage().find("disabled") != std::string::npos);
    BOOST_CHECK_EQUAL(optional->state(), stdc::pluginsystem::PluginSpec::Running);
}

BOOST_AUTO_TEST_CASE(test_load_predicate_selection_and_dependencies) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Bundle, false);
    directory.addPlugin("provider", R"({"id":"Provider","name":"Provider","version":"1.0"})");
    directory.addPlugin(
        "required",
        R"({"id":"Required","name":"Required","version":"1.0","dependencies":[{"id":"Provider","version":"1.0","type":"required"}]})");
    directory.addPlugin(
        "optional",
        R"({"id":"Optional","name":"Optional","version":"1.0","dependencies":[{"id":"Provider","version":"1.0","type":"optional"}]})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Bundle);
    system.setPluginPaths(directory.path());
    for (const auto spec : system.plugins()) {
        BOOST_CHECK(spec->isSelectedForLoad());
    }

    int predicateCalls = 0;
    system.setPluginLoadPredicate([&](const stdc::pluginsystem::PluginSpec &spec) {
        ++predicateCalls;
        system.plugins();
        system.hasError();
        return spec.id() != "Provider";
    });
    system.loadPlugins();

    const auto specs = system.plugins();
    auto provider = findPlugin(specs, "Provider");
    auto required = findPlugin(specs, "Required");
    auto optional = findPlugin(specs, "Optional");
    BOOST_REQUIRE(provider);
    BOOST_REQUIRE(required);
    BOOST_REQUIRE(optional);
    BOOST_CHECK_EQUAL(predicateCalls, 3);
    BOOST_CHECK(provider->isEnabled());
    BOOST_CHECK(!provider->isSelectedForLoad());
    BOOST_CHECK_EQUAL(provider->state(), stdc::pluginsystem::PluginSpec::Read);
    BOOST_CHECK(!provider->hasError());
    BOOST_CHECK(required->isSelectedForLoad());
    BOOST_CHECK_EQUAL(required->state(), stdc::pluginsystem::PluginSpec::Invalid);
    BOOST_CHECK(required->errorMessage().find("selected") != std::string::npos);
    BOOST_CHECK(optional->isSelectedForLoad());
    BOOST_CHECK_EQUAL(optional->state(), stdc::pluginsystem::PluginSpec::Running);

    system.setPluginLoadPredicate([](const auto &) { return true; });
    BOOST_CHECK(!provider->isSelectedForLoad());
}

BOOST_AUTO_TEST_CASE(test_lifecycle_dependency_order_and_reentrant_queries) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Bundle, false);
    const auto providerPath =
        directory.addPlugin("provider", R"({"id":"Provider","name":"Provider","version":"1.0"})",
                            TEST_PLUGINSYSTEM_LIFECYCLE_PROVIDER_PATH);
    const auto consumerPath = directory.addPlugin(
        "consumer",
        R"({"id":"Consumer","name":"Consumer","version":"1.0","dependencies":[{"id":"Provider","version":"1.0","type":"required"}]})",
        TEST_PLUGINSYSTEM_LIFECYCLE_CONSUMER_PATH);

    stdc::SharedLibrary providerLibrary;
    stdc::SharedLibrary consumerLibrary;
    setLifecycleCallback(&providerLibrary, providerPath);
    setLifecycleCallback(&consumerLibrary, consumerPath);

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Bundle);
    system.setPluginPaths(directory.path());
    lifecycleEvents.clear();
    activePluginSystem = &system;
    system.loadPlugins();
    system.shutdownPlugins();
    activePluginSystem = nullptr;

    const std::vector<std::string> expected{
        "provider.initialize",         "consumer.initialize",      "consumer.pluginsInitialized",
        "provider.pluginsInitialized", "consumer.aboutToShutdown", "provider.aboutToShutdown",
    };
    BOOST_CHECK_EQUAL_COLLECTIONS(lifecycleEvents.begin(), lifecycleEvents.end(), expected.begin(),
                                  expected.end());

    for (auto spec : system.plugins()) {
        BOOST_CHECK_EQUAL(spec->state(), stdc::pluginsystem::PluginSpec::Stopped);
    }
}

BOOST_AUTO_TEST_CASE(test_required_and_optional_dependencies) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Bundle, false);
    directory.addPlugin(
        "consumer",
        R"({"id":"Consumer","name":"Consumer","version":"1.0","dependencies":[{"id":"Provider","version":"1.5","type":"required"},{"id":"Absent","version":"1.0","type":"optional"}]})");
    directory.addPlugin(
        "provider", R"({"id":"Provider","name":"Provider","version":"2.0","compatVersion":"1.0"})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Bundle);
    system.setPluginPaths(directory.path());
    system.loadPlugins();

    const auto specs = system.plugins();
    BOOST_REQUIRE_EQUAL(specs.size(), 2u);
    BOOST_REQUIRE(findPlugin(specs, "Consumer"));
    BOOST_REQUIRE(findPlugin(specs, "Provider"));
    BOOST_CHECK_EQUAL(findPlugin(specs, "Consumer")->state(),
                      stdc::pluginsystem::PluginSpec::Running);
    BOOST_CHECK_EQUAL(findPlugin(specs, "Provider")->state(),
                      stdc::pluginsystem::PluginSpec::Running);
    BOOST_CHECK(!system.hasError());
}

BOOST_AUTO_TEST_CASE(test_required_dependency_failure_is_isolated) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Bundle, false);
    directory.addPlugin(
        "broken",
        R"({"id":"Broken","name":"Broken","version":"1.0","dependencies":[{"id":"Absent","version":"1.0","type":"required"}]})");
    directory.addPlugin("working", R"({"id":"Working","name":"Working","version":"1.0"})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Bundle);
    system.setPluginPaths(directory.path());
    system.loadPlugins();

    const auto specs = system.plugins();
    BOOST_REQUIRE(findPlugin(specs, "Broken"));
    BOOST_REQUIRE(findPlugin(specs, "Working"));
    BOOST_CHECK_EQUAL(findPlugin(specs, "Broken")->state(),
                      stdc::pluginsystem::PluginSpec::Invalid);
    BOOST_CHECK(findPlugin(specs, "Broken")->hasError());
    BOOST_CHECK_EQUAL(findPlugin(specs, "Working")->state(),
                      stdc::pluginsystem::PluginSpec::Running);
    BOOST_CHECK(system.hasError());
}

BOOST_AUTO_TEST_CASE(test_required_dependency_failure_propagates) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Bundle, false);
    directory.addPlugin(
        "top",
        R"({"id":"Top","name":"Top","version":"1.0","dependencies":[{"id":"Middle","version":"1.0","type":"required"}]})");
    directory.addPlugin(
        "middle",
        R"({"id":"Middle","name":"Middle","version":"1.0","dependencies":[{"id":"Absent","version":"1.0","type":"required"}]})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Bundle);
    system.setPluginPaths(directory.path());
    system.loadPlugins();

    const auto specs = system.plugins();
    BOOST_REQUIRE(findPlugin(specs, "Top"));
    BOOST_REQUIRE(findPlugin(specs, "Middle"));
    BOOST_CHECK_EQUAL(findPlugin(specs, "Top")->state(), stdc::pluginsystem::PluginSpec::Invalid);
    BOOST_CHECK_EQUAL(findPlugin(specs, "Middle")->state(),
                      stdc::pluginsystem::PluginSpec::Invalid);
}

BOOST_AUTO_TEST_CASE(test_initialization_failure_is_isolated_and_propagates) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Bundle, false);
    directory.addPlugin("provider", R"({"id":"Provider","name":"Provider","version":"1.0"})",
                        TEST_PLUGINSYSTEM_FAILING_PLUGIN_PATH);
    directory.addPlugin(
        "consumer",
        R"({"id":"Consumer","name":"Consumer","version":"1.0","dependencies":[{"id":"Provider","version":"1.0","type":"required"}]})");
    directory.addPlugin("working", R"({"id":"Working","name":"Working","version":"1.0"})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Bundle);
    system.setPluginPaths(directory.path());
    system.loadPlugins();

    const auto specs = system.plugins();
    auto provider = findPlugin(specs, "Provider");
    auto consumer = findPlugin(specs, "Consumer");
    auto working = findPlugin(specs, "Working");
    BOOST_REQUIRE(provider);
    BOOST_REQUIRE(consumer);
    BOOST_REQUIRE(working);
    BOOST_CHECK_EQUAL(provider->state(), stdc::pluginsystem::PluginSpec::Loaded);
    BOOST_CHECK(provider->errorMessage().find("intentional") != std::string::npos);
    BOOST_CHECK_EQUAL(consumer->state(), stdc::pluginsystem::PluginSpec::Loaded);
    BOOST_CHECK(consumer->hasError());
    BOOST_CHECK_EQUAL(working->state(), stdc::pluginsystem::PluginSpec::Running);
}

BOOST_AUTO_TEST_CASE(test_incompatible_dependency) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Bundle, false);
    directory.addPlugin(
        "consumer",
        R"({"id":"Consumer","name":"Consumer","version":"1.0","dependencies":[{"id":"Provider","version":"3.0","type":"required"}]})");
    directory.addPlugin("provider", R"({"id":"Provider","name":"Provider","version":"2.0"})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Bundle);
    system.setPluginPaths(directory.path());
    system.loadPlugins();

    auto consumer = findPlugin(system.plugins(), "Consumer");
    BOOST_REQUIRE(consumer);
    BOOST_CHECK_EQUAL(consumer->state(), stdc::pluginsystem::PluginSpec::Invalid);
    BOOST_CHECK(consumer->errorMessage().find("incompatible") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(test_duplicate_ids) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Bundle, false);
    directory.addPlugin("first", R"({"id":"Duplicate","name":"First","version":"1.0"})");
    directory.addPlugin("second", R"({"id":"Duplicate","name":"Second","version":"1.0"})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Bundle);
    system.setPluginPaths(directory.path());
    system.loadPlugins();

    const auto specs = system.plugins();
    BOOST_REQUIRE_EQUAL(specs.size(), 2u);
    for (const auto spec : specs) {
        BOOST_CHECK_EQUAL(spec->state(), stdc::pluginsystem::PluginSpec::Invalid);
        BOOST_CHECK(spec->errorMessage().find("duplicate") != std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(test_duplicate_display_names_are_allowed) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Bundle, false);
    directory.addPlugin("first", R"({"id":"First","name":"Same Name","version":"1.0"})");
    directory.addPlugin("second", R"({"id":"Second","name":"Same Name","version":"1.0"})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Bundle);
    system.setPluginPaths(directory.path());
    system.loadPlugins();

    const auto specs = system.plugins();
    BOOST_REQUIRE_EQUAL(specs.size(), 2u);
    BOOST_CHECK(!system.hasError());
    for (const auto spec : specs) {
        BOOST_CHECK_EQUAL(spec->state(), stdc::pluginsystem::PluginSpec::Running);
        BOOST_CHECK_EQUAL(spec->name(), "Same Name");
    }
}

BOOST_AUTO_TEST_CASE(test_circular_dependencies) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Bundle, false);
    directory.addPlugin(
        "a",
        R"({"id":"A","name":"A","version":"1.0","dependencies":[{"id":"B","version":"1.0","type":"required"}]})");
    directory.addPlugin(
        "b",
        R"({"id":"B","name":"B","version":"1.0","dependencies":[{"id":"A","version":"1.0","type":"required"}]})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Bundle);
    system.setPluginPaths(directory.path());
    system.loadPlugins();

    const auto specs = system.plugins();
    BOOST_REQUIRE_EQUAL(specs.size(), 2u);
    for (const auto spec : specs) {
        BOOST_CHECK_EQUAL(spec->state(), stdc::pluginsystem::PluginSpec::Invalid);
        BOOST_CHECK(spec->errorMessage().find("circular dependency") != std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(test_loaded_plugin_must_implement_iplugin) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Bundle, false);
    directory.addPlugin("plugin", R"({"id":"WrongType","name":"Wrong Type","version":"1.0"})",
                        TEST_PLUGINLOADER_PLUGIN_PATH);

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Bundle);
    system.setPluginPaths(directory.path());
    system.loadPlugins();

    auto spec = findPlugin(system.plugins(), "WrongType");
    BOOST_REQUIRE(spec);
    BOOST_CHECK(spec->hasError());
    BOOST_CHECK(spec->errorMessage().find("IPlugin") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
