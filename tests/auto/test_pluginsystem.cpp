// SPDX-License-Identifier: MIT

#include <stdcorelib/pluginsystem/plugindependency.h>
#include <stdcorelib/pluginsystem/pluginsystem.h>

#include <chrono>
#include <fstream>

#include <boost/test/unit_test.hpp>

namespace {

    class RuntimePlugin : public stdc::pluginsystem::IPlugin {
    public:
        bool initialize(std::string *) override {
            return true;
        }
    };

    class TemporaryPluginSystemDirectory {
    public:
        explicit TemporaryPluginSystemDirectory(
            stdc::pluginsystem::PluginSystem::PluginLayout layout) {
            auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
            _path = std::filesystem::temp_directory_path() /
                    ("stdcorelib-pluginsystem-test-" + std::to_string(suffix));
            std::filesystem::create_directories(_path);

            const auto source = std::filesystem::path(TEST_PLUGINSYSTEM_PLUGIN_PATH);
            if (layout == stdc::pluginsystem::PluginSystem::Flat) {
                std::filesystem::copy_file(source, _path / source.filename());
                return;
            }

            const auto pluginDirectory = _path / "plugin";
            std::filesystem::create_directories(pluginDirectory);
            std::filesystem::copy_file(source, pluginDirectory / source.filename());

            std::ofstream manifest(pluginDirectory / "plugin.json");
            manifest
                << R"({"$version":"1.0","iid":"org.stdcorelib.PluginSystem","binary":")"
                << source.filename().string()
                << R"(","metadata":{"id":"org.stdcorelib.PluginSystemTest","name":"PluginSystem Test","version":"2.1.0"}})";
        }

        ~TemporaryPluginSystemDirectory() {
            std::error_code ec;
            std::filesystem::remove_all(_path, ec);
        }

        const std::filesystem::path &path() const {
            return _path;
        }

    private:
        std::filesystem::path _path;
    };

    void checkPluginSystemLayout(stdc::pluginsystem::PluginSystem::PluginLayout layout) {
        TemporaryPluginSystemDirectory directory(layout);
        stdc::pluginsystem::PluginSystem system(layout);
        BOOST_CHECK_EQUAL(system.pluginLayout(), layout);
        system.addPluginPath(directory.path());

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

BOOST_AUTO_TEST_CASE(test_runtime_plugin) {
    RuntimePlugin plugin;
    stdc::pluginsystem::PluginSystem system;
    system.addRuntimePlugin(&plugin,
                            stdc::json::Object{
                                {"iid",      std::string(stdc::pluginsystem::PluginSystem::pluginIID())},
                                {"metadata",
                                 stdc::json::Object{
                                     {"id", "org.stdcorelib.RuntimePluginSystemTest"},
                                     {"name", "Runtime PluginSystem Test"},
                                     {"version", "1.0"},
                                 }                                                                     },
    });

    const auto specs = system.plugins();
    BOOST_REQUIRE_EQUAL(specs.size(), 1u);
    BOOST_CHECK_EQUAL(specs.front()->state(), stdc::pluginsystem::PluginSpec::Read);
    BOOST_CHECK_EQUAL(specs.front()->id(), "org.stdcorelib.RuntimePluginSystemTest");
    BOOST_CHECK(specs.front()->filePath().empty());
}

BOOST_AUTO_TEST_SUITE_END()
