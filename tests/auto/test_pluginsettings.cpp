// SPDX-License-Identifier: MIT

#include <stdcorelib/pluginsystem/pluginsettings.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(test_pluginsettings)

BOOST_AUTO_TEST_CASE(test_overrides) {
    stdc::pluginsystem::PluginSettings settings;
    BOOST_CHECK(!settings.pluginEnabled("Unknown"));
    BOOST_CHECK(settings.isPluginEnabled("Unknown", true));
    BOOST_CHECK(!settings.isPluginEnabled("Unknown", false));

    settings.setPluginEnabled("Plugin", false);
    BOOST_REQUIRE(settings.pluginEnabled("Plugin"));
    BOOST_CHECK(!*settings.pluginEnabled("Plugin"));
    BOOST_CHECK(!settings.isPluginEnabled("Plugin", true));

    settings.setPluginEnabled("Plugin", true);
    BOOST_CHECK(*settings.pluginEnabled("Plugin"));
    settings.resetPlugin("Plugin");
    BOOST_CHECK(!settings.pluginEnabled("Plugin"));
}

BOOST_AUTO_TEST_CASE(test_json_round_trip_preserves_unknown_ids) {
    const stdc::json::Value value = stdc::json::Object{
        {"disabled", stdc::json::Array{"org.example.Absent", "org.example.Disabled"}},
        {"enabled",  stdc::json::Array{"org.example.Enabled"}                       },
    };

    std::string errorMessage = "not cleared";
    auto settings = stdc::pluginsystem::PluginSettings::fromJson(value, &errorMessage);
    BOOST_REQUIRE_MESSAGE(settings, errorMessage);
    BOOST_CHECK(errorMessage.empty());
    BOOST_CHECK(settings->toJson() == value);
    BOOST_CHECK_EQUAL(settings->enabledPlugins().front(), "org.example.Enabled");
    BOOST_CHECK_EQUAL(settings->disabledPlugins().size(), 2u);
}

BOOST_AUTO_TEST_CASE(test_json_rejects_invalid_and_conflicting_ids) {
    std::string errorMessage;
    BOOST_CHECK(!stdc::pluginsystem::PluginSettings::fromJson(stdc::json::Array(), &errorMessage));
    BOOST_CHECK(!errorMessage.empty());

    const stdc::json::Value invalid = stdc::json::Object{
        {"enabled", stdc::json::Array{"Plugin", "Plugin"}},
    };
    BOOST_CHECK(!stdc::pluginsystem::PluginSettings::fromJson(invalid, &errorMessage));

    const stdc::json::Value conflicting = stdc::json::Object{
        {"disabled", stdc::json::Array{"Plugin"}},
        {"enabled",  stdc::json::Array{"Plugin"}},
    };
    BOOST_CHECK(!stdc::pluginsystem::PluginSettings::fromJson(conflicting, &errorMessage));
}

BOOST_AUTO_TEST_SUITE_END()
