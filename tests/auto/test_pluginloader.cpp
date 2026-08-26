// SPDX-License-Identifier: MIT

#include <stdcorelib/plugin/pluginfactory.h>
#include <stdcorelib/plugin/pluginloader.h>

#include <chrono>

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
                    ("stdcorelib-plugin-test-" + std::to_string(suffix));
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

        const std::filesystem::path &path() const {
            return _path;
        }

    private:
        std::filesystem::path _path;
    };

}

BOOST_AUTO_TEST_SUITE(test_pluginloader)

BOOST_AUTO_TEST_CASE(test_null) {
    const stdc::plugin::PluginLoader loader;

    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::Null);
    BOOST_CHECK(!loader.hasError());
}

BOOST_AUTO_TEST_CASE(test_embedded_metadata_and_load) {
    stdc::plugin::PluginLoader loader(TEST_PLUGINLOADER_PLUGIN_PATH);

    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::Read);
    BOOST_CHECK_EQUAL(loader.origin(), stdc::plugin::PluginLoader::FileSystem);
    BOOST_CHECK(!loader.hasError());
    BOOST_CHECK(!loader.plugin());
    BOOST_CHECK_EQUAL(loader.iid(), "org.stdcorelib.LoaderTest");
    BOOST_CHECK_EQUAL(loader.metadata()["answer"].toInt(), 42);
    BOOST_CHECK_EQUAL(loader.metadata()["iid"].toString(), "user.metadata.iid");
    BOOST_CHECK(loader.metadata()["metadata"]["nested"].toBool());
    BOOST_CHECK_EQUAL(loader.metadata()["name"].toString(), "user metadata name");

    BOOST_REQUIRE_MESSAGE(loader.load(), loader.errorMessage());
    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::Loaded);
    BOOST_CHECK_EQUAL(loader.origin(), stdc::plugin::PluginLoader::FileSystem);
    BOOST_CHECK(loader.isLoaded());
    BOOST_CHECK(loader.plugin());

    BOOST_REQUIRE_MESSAGE(loader.unload(), loader.errorMessage());
    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::Read);
    BOOST_CHECK(!loader.isLoaded());
    BOOST_CHECK(!loader.plugin());
    BOOST_CHECK(!loader.hasError());
}

BOOST_AUTO_TEST_CASE(test_set_file_path) {
    stdc::plugin::PluginLoader loader;
    loader.setFilePath(TEST_PLUGINLOADER_PLUGIN_PATH, TEST_PLUGINLOADER_METADATA_PATH);

    BOOST_CHECK_EQUAL(loader.filePath(), TEST_PLUGINLOADER_PLUGIN_PATH);
    BOOST_CHECK_EQUAL(loader.iid(), "org.stdcorelib.LoaderTest");
    BOOST_CHECK_EQUAL(loader.metadata()["iid"].toString(), "user.metadata.iid");
    BOOST_CHECK(!loader.plugin());
}

BOOST_AUTO_TEST_CASE(test_failed_external_metadata_does_not_publish_embedded_data) {
    const stdc::plugin::PluginLoader loader(TEST_PLUGINLOADER_PLUGIN_PATH,
                                            TEST_PLUGINLOADER_LIBRARY_PATH);

    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::Invalid);
    BOOST_CHECK(loader.hasError());
    BOOST_CHECK(loader.iid().empty());
    BOOST_CHECK(loader.metadata().isNull());
}

BOOST_AUTO_TEST_CASE(test_runtime_plugin) {
    RuntimePlugin plugin;
    const stdc::json::Value metadata = stdc::json::Object{
        {"answer", 42},
    };

    stdc::plugin::PluginLoader loader("org.stdcorelib.RuntimeTest", &plugin, metadata);

    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::Loaded);
    BOOST_CHECK_EQUAL(loader.origin(), stdc::plugin::PluginLoader::Runtime);
    BOOST_CHECK(loader.isLoaded());
    BOOST_CHECK_EQUAL(loader.plugin(), &plugin);
    BOOST_CHECK_EQUAL(loader.iid(), "org.stdcorelib.RuntimeTest");
    BOOST_CHECK_EQUAL(loader.metadata()["answer"].toInt(), 42);
    BOOST_CHECK(loader.filePath().empty());

    BOOST_CHECK(!loader.unload());
    BOOST_CHECK(loader.hasError());
    BOOST_CHECK(!loader.errorMessage().empty());
    BOOST_CHECK(loader.load());
    BOOST_CHECK(!loader.hasError());
}

BOOST_AUTO_TEST_CASE(test_runtime_metadata_requires_object) {
    RuntimePlugin plugin;
    const stdc::json::Value metadata = 42;

    const stdc::plugin::PluginLoader loader("org.stdcorelib.RuntimeTest", &plugin, metadata);

    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::Invalid);
    BOOST_CHECK_EQUAL(loader.origin(), stdc::plugin::PluginLoader::Runtime);
    BOOST_CHECK(loader.hasError());
    BOOST_CHECK(loader.errorMessage().find("metadata") != std::string::npos);
    BOOST_CHECK(loader.iid().empty());
    BOOST_CHECK(loader.metadata().isNull());
    BOOST_CHECK(!loader.plugin());
}

BOOST_AUTO_TEST_CASE(test_load_failed) {
    stdc::plugin::PluginLoader loader;
    loader.setFilePath(TEST_PLUGINLOADER_METADATA_ONLY_LIBRARY_PATH);

    BOOST_REQUIRE_EQUAL(loader.state(), stdc::plugin::PluginLoader::Read);
    BOOST_CHECK(!loader.load());
    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::LoadFailed);
    BOOST_CHECK(loader.hasError());
    BOOST_CHECK(loader.unload());
    BOOST_CHECK(!loader.hasError());
}

BOOST_AUTO_TEST_CASE(test_factory_scan_hooks) {
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

BOOST_AUTO_TEST_CASE(test_factory_retries_failed_scan) {
    const auto root = std::filesystem::path(TEST_PLUGINLOADER_METADATA_PATH).parent_path();

    RetryPluginFactory factory;
    factory.addPluginPath("org.stdcorelib.LoaderTest", root);
    BOOST_CHECK(factory.plugins("org.stdcorelib.LoaderTest").empty());
    BOOST_CHECK_EQUAL(factory.plugins("org.stdcorelib.LoaderTest").size(), 1u);
}

BOOST_AUTO_TEST_CASE(test_factory_keeps_matching_plugin_with_invalid_external_metadata) {
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

BOOST_AUTO_TEST_CASE(test_factory_replaces_paths) {
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

BOOST_AUTO_TEST_CASE(test_factory_keeps_loaded_plugin_when_replacing_paths) {
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

BOOST_AUTO_TEST_CASE(test_default_factory_scan) {
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

BOOST_AUTO_TEST_CASE(test_factory_ignores_runtime_plugin_without_iid) {
    RuntimePlugin plugin;
    stdc::plugin::PluginFactory factory;
    factory.addRuntimePlugin("", &plugin);

    BOOST_CHECK(factory.plugins("").empty());
}

BOOST_AUTO_TEST_SUITE_END()
