// SPDX-License-Identifier: MIT

#include <stdcorelib/plugin/pluginloader.h>

#include <boost/test/unit_test.hpp>

namespace {

    class RuntimePlugin : public stdc::plugin::Plugin {};

    RuntimePlugin staticInstance;

    stdc::plugin::Plugin *makeStaticInstance() {
        return &staticInstance;
    }

    stdc::json::Value staticMetadata() {
        return stdc::json::Object{
            {"answer", 42},
        };
    }

    /// A descriptor equivalent to what STDC_EXPORT_STATIC_PLUGIN registers, built here so that the
    /// state transitions do not depend on what another test file happens to register.
    const stdc::plugin::StaticPlugin staticDescriptor("org.stdcorelib.StateTest",
                                                      &makeStaticInstance, &staticMetadata);

    std::filesystem::path missingLibrary() {
        return std::filesystem::path(TEST_PLUGINLOADER_PLUGIN_PATH).parent_path() /
               "missing-plugin.dll";
    }

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

BOOST_AUTO_TEST_CASE(test_unload_preserves_invalid_diagnostic) {
    const auto missingPath =
        std::filesystem::path(TEST_PLUGINLOADER_PLUGIN_PATH).parent_path() / "missing-plugin.dll";
    stdc::plugin::PluginLoader loader(missingPath);

    BOOST_REQUIRE_EQUAL(loader.state(), stdc::plugin::PluginLoader::Invalid);
    BOOST_REQUIRE(loader.hasError());
    const auto errorMessage = loader.errorMessage();

    BOOST_CHECK(loader.unload());
    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::Invalid);
    BOOST_CHECK(loader.hasError());
    BOOST_CHECK_EQUAL(loader.errorMessage(), errorMessage);
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
    BOOST_REQUIRE(loader.hasError());
    const auto errorMessage = loader.errorMessage();

    BOOST_CHECK(loader.unload());
    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::LoadFailed);
    BOOST_CHECK(loader.hasError());
    BOOST_CHECK_EQUAL(loader.errorMessage(), errorMessage);

    BOOST_CHECK(!loader.load());
    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::LoadFailed);
    BOOST_CHECK_EQUAL(loader.errorMessage(), errorMessage);

    loader.setFilePath(TEST_PLUGINLOADER_PLUGIN_PATH);
    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::Read);
    BOOST_CHECK(!loader.hasError());
}

// The defects found in review were all about which state a loader ends up in and what it keeps
// from the state before, not about lines nobody executed. The cases below walk every state and
// pin what load(), unload() and the setters do from each one.

BOOST_AUTO_TEST_CASE(test_load_from_every_state) {
    using Loader = stdc::plugin::PluginLoader;

    // Null. There is nothing selected, so loading reports the failure against an empty path
    // rather than doing nothing.
    {
        Loader loader;
        BOOST_CHECK(!loader.load());
        BOOST_CHECK_EQUAL(loader.state(), Loader::LoadFailed);
        BOOST_CHECK(loader.hasError());
    }

    // Invalid. The error that made it invalid is the one worth keeping, so loading must not try
    // the known bad path and overwrite it.
    {
        Loader loader(missingLibrary());
        BOOST_REQUIRE_EQUAL(loader.state(), Loader::Invalid);
        const auto errorMessage = loader.errorMessage();

        BOOST_CHECK(!loader.load());
        BOOST_CHECK_EQUAL(loader.state(), Loader::Invalid);
        BOOST_CHECK_EQUAL(loader.errorMessage(), errorMessage);
    }

    // Read, from a library.
    {
        Loader loader(TEST_PLUGINLOADER_PLUGIN_PATH);
        BOOST_REQUIRE_EQUAL(loader.state(), Loader::Read);
        BOOST_REQUIRE_MESSAGE(loader.load(), loader.errorMessage());
        BOOST_CHECK_EQUAL(loader.state(), Loader::Loaded);

        // Loaded. Loading again is a no-op that succeeds.
        BOOST_CHECK(loader.load());
        BOOST_CHECK_EQUAL(loader.state(), Loader::Loaded);
        BOOST_CHECK(loader.unload());
    }

    // Read, from a static descriptor.
    {
        Loader loader(staticDescriptor);
        BOOST_REQUIRE_EQUAL(loader.state(), Loader::Read);
        BOOST_CHECK(loader.load());
        BOOST_CHECK_EQUAL(loader.state(), Loader::Loaded);
        BOOST_CHECK_EQUAL(loader.plugin(), &staticInstance);
    }
}

BOOST_AUTO_TEST_CASE(test_unload_from_every_state) {
    using Loader = stdc::plugin::PluginLoader;

    // Nothing is live, so unloading succeeds and changes nothing.
    {
        Loader loader;
        BOOST_CHECK(loader.unload());
        BOOST_CHECK_EQUAL(loader.state(), Loader::Null);
        BOOST_CHECK(!loader.hasError());
    }
    {
        Loader loader(TEST_PLUGINLOADER_PLUGIN_PATH);
        BOOST_REQUIRE_EQUAL(loader.state(), Loader::Read);
        BOOST_CHECK(loader.unload());
        BOOST_CHECK_EQUAL(loader.state(), Loader::Read);
        BOOST_CHECK(!loader.hasError());
    }

    // A live instance this loader did not create cannot be taken back, and the failure leaves the
    // instance exactly where it was.
    {
        Loader loader(staticDescriptor);
        BOOST_REQUIRE(loader.load());
        BOOST_CHECK(!loader.unload());
        BOOST_CHECK_EQUAL(loader.state(), Loader::Loaded);
        BOOST_CHECK(loader.hasError());
        BOOST_CHECK_EQUAL(loader.plugin(), &staticInstance);

        // Loading again clears the complaint, because the instance is still live.
        BOOST_CHECK(loader.load());
        BOOST_CHECK(!loader.hasError());
    }
}

BOOST_AUTO_TEST_CASE(test_setters_start_over_from_every_state) {
    using Loader = stdc::plugin::PluginLoader;
    RuntimePlugin plugin;

    // From Invalid, from LoadFailed and from Loaded alike, selecting something else drops
    // everything the loader was holding, including the error.
    {
        Loader loader(missingLibrary());
        BOOST_REQUIRE_EQUAL(loader.state(), Loader::Invalid);
        loader.setStaticPlugin(staticDescriptor);
        BOOST_CHECK_EQUAL(loader.state(), Loader::Read);
        BOOST_CHECK_EQUAL(loader.origin(), Loader::Static);
        BOOST_CHECK(!loader.hasError());
        BOOST_CHECK(loader.filePath().empty());
    }
    {
        Loader loader(TEST_PLUGINLOADER_METADATA_ONLY_LIBRARY_PATH);
        BOOST_REQUIRE(!loader.load());
        BOOST_REQUIRE_EQUAL(loader.state(), Loader::LoadFailed);
        loader.setPlugin("org.stdcorelib.StateTest", &plugin);
        BOOST_CHECK_EQUAL(loader.state(), Loader::Loaded);
        BOOST_CHECK_EQUAL(loader.origin(), Loader::Runtime);
        BOOST_CHECK(!loader.hasError());
        BOOST_CHECK_EQUAL(loader.plugin(), &plugin);
    }
    {
        Loader loader(TEST_PLUGINLOADER_PLUGIN_PATH);
        BOOST_REQUIRE(loader.load());
        loader.setFilePath(missingLibrary());
        BOOST_CHECK_EQUAL(loader.state(), Loader::Invalid);
        BOOST_CHECK(!loader.plugin());
        BOOST_CHECK(loader.iid().empty());
        BOOST_CHECK(loader.metadata().isNull());
    }

    // A setter that rejects its argument leaves nothing of the previous selection behind either.
    {
        Loader loader(TEST_PLUGINLOADER_PLUGIN_PATH);
        BOOST_REQUIRE_EQUAL(loader.state(), Loader::Read);
        loader.setPlugin("", &plugin);
        BOOST_CHECK_EQUAL(loader.state(), Loader::Invalid);
        BOOST_CHECK(loader.hasError());
        BOOST_CHECK(loader.iid().empty());
        BOOST_CHECK(loader.filePath().empty());
        BOOST_CHECK(!loader.plugin());
    }
}

BOOST_AUTO_TEST_SUITE_END()
