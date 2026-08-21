// SPDX-License-Identifier: MIT

#include <stdcorelib/plugin/pluginfactory.h>
#include <stdcorelib/plugin/pluginloader.h>

#include <boost/test/unit_test.hpp>

namespace {

    class RuntimePlugin : public stdc::plugin::Plugin {};

    class TestPluginFactory : public stdc::plugin::PluginFactory {
    protected:
        bool scanPluginPaths(const std::filesystem::path &,
                             std::vector<std::filesystem::path> *pluginPaths) const override {
            pluginPaths->push_back("candidate");
            return true;
        }

        bool resolvePluginPath(const std::filesystem::path &, std::filesystem::path *pluginPath,
                               std::optional<std::filesystem::path> *metadataPath) const override {
            *pluginPath = TEST_PLUGINLOADER_PLUGIN_PATH;
            *metadataPath = TEST_PLUGINLOADER_METADATA_PATH;
            return true;
        }
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

    BOOST_REQUIRE_MESSAGE(loader.load(), loader.errorMessage());
    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::Loaded);
    BOOST_CHECK_EQUAL(loader.origin(), stdc::plugin::PluginLoader::FileSystem);
    BOOST_CHECK(loader.isLoaded());
    BOOST_CHECK(loader.plugin());

    BOOST_REQUIRE_MESSAGE(loader.unload(), loader.errorMessage());
    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::Read);
    BOOST_CHECK(!loader.isLoaded());
    BOOST_CHECK(!loader.plugin());
}

BOOST_AUTO_TEST_CASE(test_set_file_path) {
    stdc::plugin::PluginLoader loader;
    loader.setFilePath(TEST_PLUGINLOADER_PLUGIN_PATH, TEST_PLUGINLOADER_METADATA_PATH);

    BOOST_CHECK_EQUAL(loader.filePath(), TEST_PLUGINLOADER_PLUGIN_PATH);
    BOOST_CHECK_EQUAL(loader.iid(), "org.stdcorelib.LoaderTest");
    BOOST_CHECK(!loader.plugin());
}

BOOST_AUTO_TEST_CASE(test_runtime_plugin) {
    RuntimePlugin plugin;
    const stdc::json::Value metadata = stdc::json::Object{
        {"iid", "org.stdcorelib.RuntimeTest"},
    };

    stdc::plugin::PluginLoader loader(&plugin, metadata);

    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::Loaded);
    BOOST_CHECK_EQUAL(loader.origin(), stdc::plugin::PluginLoader::Runtime);
    BOOST_CHECK(loader.isLoaded());
    BOOST_CHECK_EQUAL(loader.plugin(), &plugin);
    BOOST_CHECK_EQUAL(loader.iid(), "org.stdcorelib.RuntimeTest");
    BOOST_CHECK(loader.filePath().empty());
}

BOOST_AUTO_TEST_CASE(test_load_failed) {
    stdc::plugin::PluginLoader loader;
    loader.setFilePath(TEST_PLUGINLOADER_METADATA_PATH, TEST_PLUGINLOADER_METADATA_PATH);

    BOOST_REQUIRE_EQUAL(loader.state(), stdc::plugin::PluginLoader::Read);
    BOOST_CHECK(!loader.load());
    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::LoadFailed);
    BOOST_CHECK(loader.hasError());
}

BOOST_AUTO_TEST_CASE(test_factory_scan_hooks) {
    const auto root = std::filesystem::path(TEST_PLUGINLOADER_METADATA_PATH).parent_path();

    TestPluginFactory factory;
    factory.addPluginPath("org.stdcorelib.LoaderTest", root);
    const auto plugins = factory.plugins("org.stdcorelib.LoaderTest");
    BOOST_REQUIRE_EQUAL(plugins.size(), 1u);
    BOOST_CHECK_EQUAL(plugins.front()->filePath(), TEST_PLUGINLOADER_PLUGIN_PATH);

    TestPluginFactory mismatchedFactory;
    mismatchedFactory.addPluginPath("org.stdcorelib.Other", root);
    BOOST_CHECK(mismatchedFactory.plugins("org.stdcorelib.Other").empty());
}

BOOST_AUTO_TEST_CASE(test_factory_ignores_runtime_plugin_without_iid) {
    RuntimePlugin plugin;
    stdc::plugin::PluginFactory factory;
    factory.addRuntimePlugin(&plugin, stdc::json::Object());

    BOOST_CHECK(factory.plugins("").empty());
}

BOOST_AUTO_TEST_SUITE_END()
