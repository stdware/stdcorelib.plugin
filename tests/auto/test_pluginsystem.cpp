// SPDX-License-Identifier: MIT

#include <stdcorelib/pluginsystem/plugindependency.h>
#include <stdcorelib/pluginsystem/pluginsystem.h>

#include <algorithm>
#include <chrono>
#include <fstream>

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

        void addPlugin(const std::string &directoryName, std::string_view metadata,
                       const std::filesystem::path &source = TEST_PLUGINSYSTEM_PLUGIN_PATH,
                       std::string_view iid = "org.stdcorelib.PluginSystem") {
            const auto pluginDirectory = _path / directoryName;
            std::filesystem::create_directories(pluginDirectory);
            std::filesystem::copy_file(source, pluginDirectory / source.filename());

            std::ofstream manifest(pluginDirectory / "plugin.json");
            manifest << R"({"iid":")" << iid << R"(","binary":")" << source.filename().string()
                     << R"(","metadata":)" << metadata << "}";
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

    void checkPluginSystemLayout(stdc::pluginsystem::PluginSystem::PluginLayout layout) {
        TemporaryPluginSystemDirectory directory(layout);
        stdc::pluginsystem::PluginSystem system("org.stdcorelib.PluginSystem", layout);
        BOOST_CHECK_EQUAL(system.pluginLayout(), layout);
        BOOST_CHECK_EQUAL(system.iid(), "org.stdcorelib.PluginSystem");
        system.setPluginPaths(directory.path());

        const auto specs = system.plugins();
        BOOST_REQUIRE_EQUAL(specs.size(), 1u);
        BOOST_CHECK_EQUAL(specs.front()->state(), stdc::pluginsystem::PluginSpec::Read);
        BOOST_CHECK(!specs.front()->hasError());
        BOOST_CHECK_EQUAL(specs.front()->id(), "org.stdcorelib.PluginSystemTest");
        BOOST_CHECK_EQUAL(specs.front()->name(), "PluginSystem Test");
        BOOST_CHECK_EQUAL(specs.front()->version(), stdc::VersionNumber(2, 1));
        BOOST_CHECK_EQUAL(specs.front()->compatVersion(), specs.front()->version());

        const auto rescanned = system.plugins();
        BOOST_REQUIRE_EQUAL(rescanned.size(), 1u);
        BOOST_CHECK_EQUAL(rescanned.front(), specs.front());

        const auto pathsBeforeLoad = system.pluginPaths();
        system.loadPlugins();
        BOOST_CHECK_EQUAL(specs.front()->state(), stdc::pluginsystem::PluginSpec::Running);
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
