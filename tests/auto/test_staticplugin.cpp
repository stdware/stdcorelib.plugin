// SPDX-License-Identifier: MIT

#include <algorithm>
#include <string>
#include <type_traits>

#include <stdcorelib/plugin/plugin.h>
#include <stdcorelib/plugin/pluginfactory.h>

#include <boost/test/unit_test.hpp>

namespace {

    static_assert(std::is_move_constructible_v<stdc::plugin::PluginLoader>);
    static_assert(std::is_move_assignable_v<stdc::plugin::PluginLoader>);
    static_assert(!std::is_copy_constructible_v<stdc::plugin::PluginLoader>);
    static_assert(!std::is_copy_assignable_v<stdc::plugin::PluginLoader>);
    static_assert(std::is_same_v<stdc::plugin::StaticPluginRegistry::result_type,
                                 stdc::plugin::StaticPlugin>);

    class TestStaticPlugin : public stdc::plugin::Plugin {};

    class InvalidStaticPlugin : public stdc::plugin::Plugin {};

    class InvalidMetadataStaticPlugin : public stdc::plugin::Plugin {};

}

STDC_EXPORT_STATIC_PLUGIN(TestStaticPlugin, "org.stdcorelib.Test",
                          (stdc::json::Object{
                              {"answer", 42}
}))

STDC_EXPORT_STATIC_PLUGIN(InvalidStaticPlugin, "", (stdc::json::Object{}))

STDC_EXPORT_STATIC_PLUGIN(InvalidMetadataStaticPlugin, "org.stdcorelib.InvalidMetadata", 42)

BOOST_AUTO_TEST_SUITE(test_staticplugin)

BOOST_AUTO_TEST_CASE(test_registry) {
    const auto sets = stdc::plugin::PluginLoader::staticPluginSets();
    BOOST_CHECK(std::find(sets.begin(), sets.end(), "org.stdcorelib.Test") != sets.end());

    const auto plugins = stdc::plugin::PluginLoader::staticPlugins("org.stdcorelib.Test");
    BOOST_REQUIRE_EQUAL(plugins.size(), 1u);
    BOOST_REQUIRE(plugins.front().instance);
    BOOST_REQUIRE(plugins.front().metadata);
    BOOST_CHECK(plugins.front().instance() != nullptr);
    BOOST_CHECK_EQUAL(plugins.front().iid, "org.stdcorelib.Test");
    BOOST_CHECK_EQUAL(plugins.front().metadata()["answer"].toInt(), 42);
}

BOOST_AUTO_TEST_CASE(test_loader_origin) {
    const auto staticPlugins = stdc::plugin::PluginLoader::staticPlugins("org.stdcorelib.Test");
    BOOST_REQUIRE_EQUAL(staticPlugins.size(), 1u);

    stdc::plugin::PluginLoader loader(staticPlugins.front());
    BOOST_CHECK_EQUAL(loader.origin(), stdc::plugin::PluginLoader::Static);
    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::Read);
    BOOST_CHECK(!loader.plugin());
    BOOST_CHECK_EQUAL(loader.iid(), "org.stdcorelib.Test");
    BOOST_CHECK_EQUAL(loader.metadata()["answer"].toInt(), 42);

    BOOST_REQUIRE_MESSAGE(loader.load(), loader.errorMessage());
    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::Loaded);
    BOOST_CHECK(loader.plugin());

    stdc::plugin::PluginFactory factory;
    factory.addStaticPlugins("org.stdcorelib.Test");

    const auto plugins = factory.plugins("org.stdcorelib.Test");
    BOOST_REQUIRE_EQUAL(plugins.size(), 1u);
    BOOST_CHECK_EQUAL(plugins.front()->origin(), stdc::plugin::PluginLoader::Static);
}

BOOST_AUTO_TEST_CASE(test_factory_ignores_static_plugin_without_iid) {
    stdc::plugin::PluginFactory factory;
    factory.addStaticPlugins("");

    BOOST_CHECK(factory.plugins("").empty());
}

BOOST_AUTO_TEST_CASE(test_static_metadata_requires_object) {
    const auto staticPlugins =
        stdc::plugin::PluginLoader::staticPlugins("org.stdcorelib.InvalidMetadata");
    BOOST_REQUIRE_EQUAL(staticPlugins.size(), 1u);

    const stdc::plugin::PluginLoader loader(staticPlugins.front());

    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::Invalid);
    BOOST_CHECK_EQUAL(loader.origin(), stdc::plugin::PluginLoader::Static);
    BOOST_CHECK(loader.hasError());
    BOOST_CHECK(loader.errorMessage().find("metadata") != std::string::npos);
    BOOST_CHECK(loader.iid().empty());
    BOOST_CHECK(loader.metadata().isNull());
    BOOST_CHECK(!loader.plugin());
}

BOOST_AUTO_TEST_SUITE_END()
