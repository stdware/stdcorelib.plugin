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

}

STDC_EXPORT_STATIC_PLUGIN(TestStaticPlugin, "test",
                          (stdc::json::Object{
                              {"$version", "1.0"                },
                              {"iid",      "org.stdcorelib.Test"},
                              {"metadata", stdc::json::Object() },
}))

BOOST_AUTO_TEST_SUITE(test_staticplugin)

BOOST_AUTO_TEST_CASE(test_registry) {
    const auto sets = stdc::plugin::PluginFactory::staticPluginSets();
    BOOST_CHECK(std::find(sets.begin(), sets.end(), "test") != sets.end());

    const auto plugins = stdc::plugin::PluginFactory::staticPlugins("test");
    BOOST_REQUIRE_EQUAL(plugins.size(), 1u);
    BOOST_REQUIRE(plugins.front().instance);
    BOOST_REQUIRE(plugins.front().metadata);
    BOOST_CHECK(plugins.front().instance() != nullptr);
    BOOST_CHECK_EQUAL(plugins.front().metadata()["iid"].toString(), "org.stdcorelib.Test");
}

BOOST_AUTO_TEST_SUITE_END()
