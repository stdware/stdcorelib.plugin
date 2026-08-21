// SPDX-License-Identifier: MIT

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
                      std::string_view iid = "org.stdcorelib.PluginSystem") {
            const auto pluginDirectory = _path / directoryName;
            std::filesystem::create_directories(pluginDirectory);
            const auto pluginPath = pluginDirectory / source.filename();
            std::filesystem::copy_file(source, pluginPath);

            std::ofstream manifest(pluginDirectory / "plugin.json");
            manifest << R"({"iid":")" << iid << R"(","binary":")" << source.filename().string()
                     << R"(","metadata":)" << metadata << "}";
            return pluginPath;
        }

        const std::filesystem::path &path() const {
            return _path;
        }

    private:
        std::filesystem::path _path;
    };

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

BOOST_AUTO_TEST_CASE(test_directory_layout) {
    checkPluginSystemLayout(stdc::pluginsystem::PluginSystem::Directory);
}

BOOST_AUTO_TEST_CASE(test_concurrent_frozen_queries) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Directory);
    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Directory);
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

BOOST_AUTO_TEST_CASE(test_spec_address_survives_discovery_growth) {
    TemporaryPluginSystemDirectory firstDirectory(stdc::pluginsystem::PluginSystem::Directory,
                                                  false);
    firstDirectory.addPlugin("first", R"({"id":"First","name":"First","version":"1.0"})");
    TemporaryPluginSystemDirectory secondDirectory(stdc::pluginsystem::PluginSystem::Directory,
                                                   false);
    secondDirectory.addPlugin("second", R"({"id":"Second","name":"Second","version":"1.0"})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Directory);
    std::vector<std::filesystem::path> paths{firstDirectory.path()};
    system.setPluginPaths(paths);
    auto firstSpec = findPlugin(system.plugins(), "First");
    BOOST_REQUIRE(firstSpec);

    paths.push_back(secondDirectory.path());
    system.setPluginPaths(paths);
    const auto specs = system.plugins();
    BOOST_CHECK_EQUAL(findPlugin(specs, "First"), firstSpec);
    BOOST_REQUIRE(findPlugin(specs, "Second"));
}

BOOST_AUTO_TEST_CASE(test_constructor_iid_selects_plugins) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Directory, false);
    directory.addPlugin("plugin", R"({"id":"Custom","name":"Custom","version":"1.0"})",
                        TEST_PLUGINSYSTEM_PLUGIN_PATH, "org.example.CustomPluginSystem");

    stdc::pluginsystem::PluginSystem matching("org.example.CustomPluginSystem",
                                              stdc::pluginsystem::PluginSystem::Directory);
    matching.setPluginPaths(directory.path());
    BOOST_REQUIRE_EQUAL(matching.plugins().size(), 1u);

    stdc::pluginsystem::PluginSystem mismatched("org.example.OtherPluginSystem",
                                                stdc::pluginsystem::PluginSystem::Directory);
    mismatched.setPluginPaths(directory.path());
    BOOST_CHECK(mismatched.plugins().empty());
}

BOOST_AUTO_TEST_CASE(test_dependency_metadata) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Directory, false);
    directory.addPlugin(
        "plugin",
        R"({"id":"Consumer","name":"Consumer","version":"1.0","dependencies":[{"id":"Provider","version":"2.0","type":"optional"}]})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Directory);
    system.setPluginPaths(directory.path());
    const auto specs = system.plugins();
    BOOST_REQUIRE_EQUAL(specs.size(), 1u);
    BOOST_REQUIRE_EQUAL(specs.front()->dependencies().size(), 1u);
    BOOST_CHECK_EQUAL(specs.front()->dependencies().front().id(), "Provider");
    BOOST_CHECK_EQUAL(specs.front()->dependencies().front().version(), stdc::VersionNumber(2));
    BOOST_CHECK_EQUAL(specs.front()->dependencies().front().type(),
                      stdc::pluginsystem::PluginDependency::Optional);
}

BOOST_AUTO_TEST_CASE(test_global_and_local_settings_precedence_and_freeze_at_load) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Directory, false);
    directory.addPlugin(
        "plugin", R"({"id":"Plugin","name":"Plugin","version":"1.0","enabledByDefault":false})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Directory);
    system.setPluginPaths(directory.path());
    auto spec = findPlugin(system.plugins(), "Plugin");
    BOOST_REQUIRE(spec);
    BOOST_CHECK(!spec->enabledByDefault());
    BOOST_CHECK(!spec->isEnabled());

    stdc::pluginsystem::PluginSettings globalSettings;
    globalSettings.setPluginEnabled("Plugin", true);
    stdc::pluginsystem::PluginSettings localSettings;
    localSettings.setPluginEnabled("Plugin", false);
    system.setGlobalPluginSettings(globalSettings);
    system.setLocalPluginSettings(localSettings);
    BOOST_CHECK(spec->enabledByDefault());
    BOOST_CHECK(!spec->isEnabled());

    localSettings.resetPlugin("Plugin");
    system.setLocalPluginSettings(localSettings);
    BOOST_CHECK(spec->enabledByDefault());
    BOOST_CHECK(spec->isEnabled());

    globalSettings.setPluginEnabled("Plugin", false);
    localSettings.setPluginEnabled("Plugin", true);
    system.setGlobalPluginSettings(globalSettings);
    system.setLocalPluginSettings(localSettings);
    BOOST_CHECK(!spec->enabledByDefault());
    BOOST_CHECK(spec->isEnabled());

    system.loadPlugins();
    BOOST_CHECK_EQUAL(spec->state(), stdc::pluginsystem::PluginSpec::Running);

    globalSettings.setPluginEnabled("Plugin", true);
    localSettings.setPluginEnabled("Plugin", false);
    system.setGlobalPluginSettings(globalSettings);
    system.setLocalPluginSettings(localSettings);
    BOOST_CHECK(!spec->enabledByDefault());
    BOOST_CHECK(spec->isEnabled());
    BOOST_REQUIRE(system.globalPluginSettings().pluginEnabled("Plugin"));
    BOOST_CHECK(!*system.globalPluginSettings().pluginEnabled("Plugin"));
    BOOST_REQUIRE(system.localPluginSettings().pluginEnabled("Plugin"));
    BOOST_CHECK(*system.localPluginSettings().pluginEnabled("Plugin"));
}

BOOST_AUTO_TEST_CASE(test_disabled_dependencies) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Directory, false);
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
                                            stdc::pluginsystem::PluginSystem::Directory);
    system.setPluginPaths(directory.path());
    system.setLocalPluginSettings(settings);
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

BOOST_AUTO_TEST_CASE(test_lifecycle_dependency_order_and_reentrant_queries) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Directory, false);
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
                                            stdc::pluginsystem::PluginSystem::Directory);
    system.setPluginPaths(directory.path());
    lifecycleEvents.clear();
    activePluginSystem = &system;
    system.loadPlugins();
    system.shutdownPlugins();
    activePluginSystem = nullptr;

    const std::vector<std::string> expected{
        "provider.initialize",        "consumer.initialize",      "consumer.pluginInitialized",
        "provider.pluginInitialized", "consumer.aboutToShutdown", "provider.aboutToShutdown",
    };
    BOOST_CHECK_EQUAL_COLLECTIONS(lifecycleEvents.begin(), lifecycleEvents.end(), expected.begin(),
                                  expected.end());

    for (auto spec : system.plugins()) {
        BOOST_CHECK_EQUAL(spec->state(), stdc::pluginsystem::PluginSpec::Stopped);
    }
}

BOOST_AUTO_TEST_CASE(test_required_and_optional_dependencies) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Directory, false);
    directory.addPlugin(
        "consumer",
        R"({"id":"Consumer","name":"Consumer","version":"1.0","dependencies":[{"id":"Provider","version":"1.5","type":"required"},{"id":"Absent","version":"1.0","type":"optional"}]})");
    directory.addPlugin(
        "provider", R"({"id":"Provider","name":"Provider","version":"2.0","compatVersion":"1.0"})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Directory);
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
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Directory, false);
    directory.addPlugin(
        "broken",
        R"({"id":"Broken","name":"Broken","version":"1.0","dependencies":[{"id":"Absent","version":"1.0","type":"required"}]})");
    directory.addPlugin("working", R"({"id":"Working","name":"Working","version":"1.0"})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Directory);
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
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Directory, false);
    directory.addPlugin(
        "top",
        R"({"id":"Top","name":"Top","version":"1.0","dependencies":[{"id":"Middle","version":"1.0","type":"required"}]})");
    directory.addPlugin(
        "middle",
        R"({"id":"Middle","name":"Middle","version":"1.0","dependencies":[{"id":"Absent","version":"1.0","type":"required"}]})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Directory);
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
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Directory, false);
    directory.addPlugin("provider", R"({"id":"Provider","name":"Provider","version":"1.0"})",
                        TEST_PLUGINSYSTEM_FAILING_PLUGIN_PATH);
    directory.addPlugin(
        "consumer",
        R"({"id":"Consumer","name":"Consumer","version":"1.0","dependencies":[{"id":"Provider","version":"1.0","type":"required"}]})");
    directory.addPlugin("working", R"({"id":"Working","name":"Working","version":"1.0"})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Directory);
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
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Directory, false);
    directory.addPlugin(
        "consumer",
        R"({"id":"Consumer","name":"Consumer","version":"1.0","dependencies":[{"id":"Provider","version":"3.0","type":"required"}]})");
    directory.addPlugin("provider", R"({"id":"Provider","name":"Provider","version":"2.0"})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Directory);
    system.setPluginPaths(directory.path());
    system.loadPlugins();

    auto consumer = findPlugin(system.plugins(), "Consumer");
    BOOST_REQUIRE(consumer);
    BOOST_CHECK_EQUAL(consumer->state(), stdc::pluginsystem::PluginSpec::Invalid);
    BOOST_CHECK(consumer->errorMessage().find("incompatible") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(test_duplicate_ids) {
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Directory, false);
    directory.addPlugin("first", R"({"id":"Duplicate","name":"First","version":"1.0"})");
    directory.addPlugin("second", R"({"id":"Duplicate","name":"Second","version":"1.0"})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Directory);
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
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Directory, false);
    directory.addPlugin("first", R"({"id":"First","name":"Same Name","version":"1.0"})");
    directory.addPlugin("second", R"({"id":"Second","name":"Same Name","version":"1.0"})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Directory);
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
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Directory, false);
    directory.addPlugin(
        "a",
        R"({"id":"A","name":"A","version":"1.0","dependencies":[{"id":"B","version":"1.0","type":"required"}]})");
    directory.addPlugin(
        "b",
        R"({"id":"B","name":"B","version":"1.0","dependencies":[{"id":"A","version":"1.0","type":"required"}]})");

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Directory);
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
    TemporaryPluginSystemDirectory directory(stdc::pluginsystem::PluginSystem::Directory, false);
    directory.addPlugin("plugin", R"({"id":"WrongType","name":"Wrong Type","version":"1.0"})",
                        TEST_PLUGINLOADER_PLUGIN_PATH);

    stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem",
                                            stdc::pluginsystem::PluginSystem::Directory);
    system.setPluginPaths(directory.path());
    system.loadPlugins();

    auto spec = findPlugin(system.plugins(), "WrongType");
    BOOST_REQUIRE(spec);
    BOOST_CHECK(spec->hasError());
    BOOST_CHECK(spec->errorMessage().find("IPlugin") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
