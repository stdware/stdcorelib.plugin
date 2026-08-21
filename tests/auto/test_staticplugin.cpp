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
    static_assert(std::is_move_constructible_v<stdc::plugin::PluginFactory>);
    static_assert(std::is_move_assignable_v<stdc::plugin::PluginFactory>);
    static_assert(!std::is_copy_constructible_v<stdc::plugin::PluginFactory>);
    static_assert(!std::is_copy_assignable_v<stdc::plugin::PluginFactory>);
    static_assert(std::is_same_v<stdc::plugin::StaticPluginRegistry::result_type,
                                 stdc::plugin::StaticPlugin>);

    class TestStaticPlugin : public stdc::plugin::Plugin {};

    class InvalidStaticPlugin : public stdc::plugin::Plugin {};

}

STDC_EXPORT_STATIC_PLUGIN(TestStaticPlugin, "test",
                          (stdc::json::Object{
                              {"iid",      "org.stdcorelib.Test"},
                              {"metadata", stdc::json::Object() },
}))

STDC_EXPORT_STATIC_PLUGIN(InvalidStaticPlugin, "invalid", (stdc::json::Object{}))

BOOST_AUTO_TEST_SUITE(test_staticplugin)

BOOST_AUTO_TEST_CASE(test_registry) {
    const auto sets = stdc::plugin::PluginLoader::staticPluginSets();
    BOOST_CHECK(std::find(sets.begin(), sets.end(), "test") != sets.end());

    const auto plugins = stdc::plugin::PluginLoader::staticPlugins("test");
    BOOST_REQUIRE_EQUAL(plugins.size(), 1u);
    BOOST_REQUIRE(plugins.front().instance);
    BOOST_REQUIRE(plugins.front().metadata);
    BOOST_CHECK(plugins.front().instance() != nullptr);
    BOOST_CHECK_EQUAL(plugins.front().metadata()["iid"].toString(), "org.stdcorelib.Test");
}

BOOST_AUTO_TEST_CASE(test_loader_origin) {
    const auto staticPlugins = stdc::plugin::PluginLoader::staticPlugins("test");
    BOOST_REQUIRE_EQUAL(staticPlugins.size(), 1u);

    stdc::plugin::PluginLoader loader(staticPlugins.front());
    BOOST_CHECK_EQUAL(loader.origin(), stdc::plugin::PluginLoader::Static);
    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::Read);
    BOOST_CHECK(!loader.plugin());
    BOOST_CHECK_EQUAL(loader.metadata()["iid"].toString(), "org.stdcorelib.Test");

    BOOST_REQUIRE_MESSAGE(loader.load(), loader.errorMessage());
    BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::Loaded);
    BOOST_CHECK(loader.plugin());

    stdc::plugin::PluginFactory factory;
    factory.addStaticPlugins("test");

    const auto plugins = factory.plugins("org.stdcorelib.Test");
    BOOST_REQUIRE_EQUAL(plugins.size(), 1u);
    BOOST_CHECK_EQUAL(plugins.front()->origin(), stdc::plugin::PluginLoader::Static);
}

BOOST_AUTO_TEST_CASE(test_factory_ignores_static_plugin_without_iid) {
    stdc::plugin::PluginFactory factory;
    factory.addStaticPlugins("invalid");

    BOOST_CHECK(factory.plugins("").empty());
}

BOOST_AUTO_TEST_SUITE_END()
