// SPDX-License-Identifier: MIT

#include <stdcorelib/plugin/pluginloader.h>

#include <boost/test/unit_test.hpp>

namespace {

    class RuntimePlugin : public stdc::plugin::Plugin {};

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

BOOST_AUTO_TEST_SUITE_END()
