// SPDX-License-Identifier: MIT

#include <chrono>
#include <fstream>
#include <string>
#include <type_traits>

#include <stdcorelib/plugin/pluginfactory.h>

#include <boost/test/unit_test.hpp>

namespace {

    class RuntimePlugin : public stdc::plugin::Plugin {};

    class DefaultPluginFactoryProbe : public stdc::plugin::PluginFactory {
    public:
        bool scan(std::string_view iid, const std::filesystem::path &path,
                  std::vector<std::filesystem::path> *pluginPaths) const {
            return scanPluginPaths(iid, path, pluginPaths);
        }

        bool resolve(std::string_view iid, const std::filesystem::path &path,
                     std::filesystem::path *pluginPath,
                     std::optional<std::filesystem::path> *metadataPath) const {
            return resolvePluginPath(iid, path, pluginPath, metadataPath);
        }
    };

    class TestPluginFactory : public stdc::plugin::PluginFactory {
    protected:
        bool scanPluginPaths(std::string_view iid, const std::filesystem::path &,
                             std::vector<std::filesystem::path> *pluginPaths) const override {
            scannedIid = iid;
            pluginPaths->push_back("candidate");
            return true;
        }

        bool resolvePluginPath(std::string_view iid, const std::filesystem::path &,
                               std::filesystem::path *pluginPath,
                               std::optional<std::filesystem::path> *metadataPath) const override {
            resolvedIid = iid;
            *pluginPath = TEST_PLUGINLOADER_PLUGIN_PATH;
            *metadataPath = TEST_PLUGINLOADER_METADATA_PATH;
            return true;
        }

    public:
        mutable std::string scannedIid;
        mutable std::string resolvedIid;
    };

    class RetryPluginFactory : public TestPluginFactory {
    protected:
        bool scanPluginPaths(std::string_view iid, const std::filesystem::path &path,
                             std::vector<std::filesystem::path> *pluginPaths) const override {
            if (_firstScan) {
                _firstScan = false;
                return false;
            }
            return TestPluginFactory::scanPluginPaths(iid, path, pluginPaths);
        }

    private:
        mutable bool _firstScan = true;
    };

    class InvalidMetadataPluginFactory : public TestPluginFactory {
    protected:
        bool resolvePluginPath(std::string_view iid, const std::filesystem::path &,
                               std::filesystem::path *pluginPath,
                               std::optional<std::filesystem::path> *metadataPath) const override {
            resolvedIid = iid;
            *pluginPath = TEST_PLUGINLOADER_PLUGIN_PATH;
            *metadataPath = TEST_PLUGINLOADER_LIBRARY_PATH;
            return true;
        }
    };

    class TemporaryPluginDirectory {
    public:
        TemporaryPluginDirectory() {
            auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
            _path = std::filesystem::temp_directory_path() /
                    ("stdcorelib-plugin-factory-test-" + std::to_string(suffix));
            std::filesystem::create_directories(_path);

            const auto plugin = std::filesystem::path(TEST_PLUGINLOADER_PLUGIN_PATH);
            std::filesystem::copy_file(plugin, _path / plugin.filename());

            const auto library = std::filesystem::path(TEST_PLUGINLOADER_LIBRARY_PATH);
            std::filesystem::copy_file(library, _path / library.filename());
        }

        ~TemporaryPluginDirectory() {
            std::error_code ec;
            std::filesystem::remove_all(_path, ec);
        }

        std::filesystem::path addBundle() const {
            const auto source = std::filesystem::path(TEST_PLUGINLOADER_PLUGIN_PATH);
            const auto bundle = _path / "bundle";
            std::filesystem::create_directory(bundle);
            std::filesystem::copy_file(source, bundle / source.filename());

            std::ofstream metadata(bundle / "plugin.json");
            metadata << R"({"name":")" << source.stem().string() << R"(","answer":42})";
            return bundle;
        }

        const std::filesystem::path &path() const {
            return _path;
        }

    private:
        std::filesystem::path _path;
    };

}

BOOST_AUTO_TEST_SUITE(test_pluginfactory)

BOOST_AUTO_TEST_CASE(test_type_traits) {
    static_assert(std::is_move_constructible_v<stdc::plugin::PluginFactory>);
    static_assert(std::is_move_assignable_v<stdc::plugin::PluginFactory>);
    static_assert(!std::is_copy_constructible_v<stdc::plugin::PluginFactory>);
    static_assert(!std::is_copy_assignable_v<stdc::plugin::PluginFactory>);
    static_assert(std::is_move_constructible_v<stdc::plugin::BundlePluginFactory>);
    static_assert(std::is_move_assignable_v<stdc::plugin::BundlePluginFactory>);
    static_assert(!std::is_copy_constructible_v<stdc::plugin::BundlePluginFactory>);
    static_assert(!std::is_copy_assignable_v<stdc::plugin::BundlePluginFactory>);
}

BOOST_AUTO_TEST_CASE(test_scan_hooks) {
    const auto root = std::filesystem::path(TEST_PLUGINLOADER_METADATA_PATH).parent_path();

    TestPluginFactory factory;
    factory.addPluginPath("org.stdcorelib.LoaderTest", root);
    const auto plugins = factory.plugins("org.stdcorelib.LoaderTest");
    BOOST_REQUIRE_EQUAL(plugins.size(), 1u);
    BOOST_CHECK_EQUAL(factory.scannedIid, "org.stdcorelib.LoaderTest");
    BOOST_CHECK_EQUAL(factory.resolvedIid, "org.stdcorelib.LoaderTest");
    BOOST_CHECK_EQUAL(plugins.front()->filePath(), TEST_PLUGINLOADER_PLUGIN_PATH);

    factory.addPluginPath("org.stdcorelib.LoaderTest", root);
    BOOST_CHECK_EQUAL(factory.plugins("org.stdcorelib.LoaderTest").size(), 1u);

    TestPluginFactory mismatchedFactory;
    mismatchedFactory.addPluginPath("org.stdcorelib.Other", root);
    BOOST_CHECK(mismatchedFactory.plugins("org.stdcorelib.Other").empty());
}

BOOST_AUTO_TEST_CASE(test_retries_failed_scan) {
    const auto root = std::filesystem::path(TEST_PLUGINLOADER_METADATA_PATH).parent_path();

    RetryPluginFactory factory;
    factory.addPluginPath("org.stdcorelib.LoaderTest", root);
    BOOST_CHECK(factory.plugins("org.stdcorelib.LoaderTest").empty());
    BOOST_CHECK_EQUAL(factory.plugins("org.stdcorelib.LoaderTest").size(), 1u);
}

BOOST_AUTO_TEST_CASE(test_keeps_matching_plugin_with_invalid_external_metadata) {
    const auto root = std::filesystem::path(TEST_PLUGINLOADER_METADATA_PATH).parent_path();

    InvalidMetadataPluginFactory factory;
    factory.addPluginPath("org.stdcorelib.LoaderTest", root);
    const auto plugins = factory.plugins("org.stdcorelib.LoaderTest");

    BOOST_REQUIRE_EQUAL(plugins.size(), 1u);
    BOOST_CHECK_EQUAL(plugins.front()->state(), stdc::plugin::PluginLoader::Invalid);
    BOOST_CHECK(plugins.front()->hasError());
    BOOST_CHECK(plugins.front()->iid().empty());
    BOOST_CHECK(plugins.front()->metadata().isNull());
}

BOOST_AUTO_TEST_CASE(test_replaces_paths) {
    const auto root = std::filesystem::path(TEST_PLUGINLOADER_METADATA_PATH).parent_path();
    const std::vector<std::filesystem::path> paths{root};
    const std::vector<std::filesystem::path> noPaths;

    TestPluginFactory factory;
    factory.setPluginPaths("org.stdcorelib.LoaderTest", paths);
    const auto plugins = factory.plugins("org.stdcorelib.LoaderTest");
    BOOST_REQUIRE_EQUAL(plugins.size(), 1u);

    factory.setPluginPaths("org.stdcorelib.LoaderTest", paths);
    BOOST_CHECK_EQUAL(factory.plugins("org.stdcorelib.LoaderTest").front(), plugins.front());

    factory.setPluginPaths("org.stdcorelib.LoaderTest", noPaths);
    BOOST_CHECK(factory.plugins("org.stdcorelib.LoaderTest").empty());
}

BOOST_AUTO_TEST_CASE(test_keeps_loaded_plugin_when_replacing_paths) {
    const auto root = std::filesystem::path(TEST_PLUGINLOADER_METADATA_PATH).parent_path();
    const std::vector<std::filesystem::path> paths{root};
    const std::vector<std::filesystem::path> noPaths;

    TestPluginFactory factory;
    factory.setPluginPaths("org.stdcorelib.LoaderTest", paths);
    const auto plugins = factory.plugins("org.stdcorelib.LoaderTest");
    BOOST_REQUIRE_EQUAL(plugins.size(), 1u);
    BOOST_REQUIRE_MESSAGE(plugins.front()->load(), plugins.front()->errorMessage());

    factory.setPluginPaths("org.stdcorelib.LoaderTest", noPaths);
    const auto retained = factory.plugins("org.stdcorelib.LoaderTest");
    BOOST_REQUIRE_EQUAL(retained.size(), 1u);
    BOOST_CHECK_EQUAL(retained.front(), plugins.front());
    BOOST_CHECK(retained.front()->isLoaded());
}

BOOST_AUTO_TEST_CASE(test_default_scan_and_resolve) {
    TemporaryPluginDirectory directory;
    DefaultPluginFactoryProbe factory;

    std::vector<std::filesystem::path> candidates;
    BOOST_REQUIRE(factory.scan("org.stdcorelib.LoaderTest", directory.path(), &candidates));
    BOOST_REQUIRE_EQUAL(candidates.size(), 1u);
    const auto candidate = candidates.front();
    BOOST_CHECK_EQUAL(candidate.filename(),
                      std::filesystem::path(TEST_PLUGINLOADER_PLUGIN_PATH).filename());

    candidates.clear();
    BOOST_REQUIRE(factory.scan("org.stdcorelib.Other", directory.path(), &candidates));
    BOOST_CHECK(candidates.empty());

    std::filesystem::path pluginPath;
    std::optional<std::filesystem::path> metadataPath = TEST_PLUGINLOADER_METADATA_PATH;
    BOOST_REQUIRE(
        factory.resolve("org.stdcorelib.LoaderTest", candidate, &pluginPath, &metadataPath));
    BOOST_CHECK_EQUAL(pluginPath, candidate);
    BOOST_CHECK(!metadataPath);

    factory.addPluginPath("org.stdcorelib.LoaderTest", directory.path());
    const auto plugins = factory.plugins("org.stdcorelib.LoaderTest");
    BOOST_REQUIRE_EQUAL(plugins.size(), 1u);
    BOOST_CHECK_EQUAL(plugins.front()->metadata()["answer"].toInt(), 42);
}

BOOST_AUTO_TEST_CASE(test_bundle_scan_and_resolve) {
    TemporaryPluginDirectory directory;
    directory.addBundle();

    stdc::plugin::BundlePluginFactory factory;
    BOOST_CHECK_EQUAL(factory.metadataFileName(), std::filesystem::path("plugin.json"));
    factory.addPluginPath("org.stdcorelib.LoaderTest", directory.path());

    const auto plugins = factory.plugins("org.stdcorelib.LoaderTest");
    BOOST_REQUIRE_EQUAL(plugins.size(), 1u);
    BOOST_CHECK_EQUAL(plugins.front()->iid(), "org.stdcorelib.LoaderTest");
    BOOST_CHECK_EQUAL(plugins.front()->metadata()["answer"].toInt(), 42);
}

BOOST_AUTO_TEST_CASE(test_ignores_runtime_plugin_without_iid) {
    RuntimePlugin plugin;
    stdc::plugin::PluginFactory factory;
    factory.addRuntimePlugin("", &plugin);

    BOOST_CHECK(factory.plugins("").empty());
}

BOOST_AUTO_TEST_SUITE_END()
