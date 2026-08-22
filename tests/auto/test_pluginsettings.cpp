// SPDX-License-Identifier: MIT

#include <stdcorelib/pluginsystem/pluginsettings.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(test_pluginsettings)

BOOST_AUTO_TEST_CASE(test_overrides) {
    stdc::pluginsystem::PluginSettings settings;
    BOOST_CHECK(!settings.pluginEnabled("Unknown"));
    BOOST_CHECK(settings.pluginEnabled("Unknown").value_or(true));
    BOOST_CHECK(!settings.pluginEnabled("Unknown").value_or(false));

    settings.setPluginEnabled("Plugin", false);
    BOOST_REQUIRE(settings.pluginEnabled("Plugin"));
    BOOST_CHECK(!*settings.pluginEnabled("Plugin"));
    BOOST_CHECK(!settings.pluginEnabled("Plugin").value_or(true));

    settings.setPluginEnabled("Plugin", true);
    BOOST_CHECK(*settings.pluginEnabled("Plugin"));
    settings.setPluginEnabled("Plugin", std::nullopt);
    BOOST_CHECK(!settings.pluginEnabled("Plugin"));
}

BOOST_AUTO_TEST_CASE(test_json_round_trip_preserves_user_data_and_unknown_ids) {
    const stdc::json::Value value = stdc::json::Object{
        {"disabledPlugins",
         stdc::json::Array{"org.example.Absent", "org.example.Disabled"}},
        {"enabledPlugins", stdc::json::Array{"org.example.Enabled"}},
        {"userData", stdc::json::Object{
                         {"theme", "dark"},
                         {"window", stdc::json::Object{{"maximized", true}}},
                     }},
    };

    std::string errorMessage = "not cleared";
    auto settings = stdc::pluginsystem::PluginSettings::fromJson(value, &errorMessage);
    BOOST_REQUIRE_MESSAGE(settings, errorMessage);
    BOOST_CHECK(errorMessage.empty());
    BOOST_CHECK(settings->toJson() == value);
    BOOST_CHECK_EQUAL(settings->userData().at("theme").toString(), "dark");
    BOOST_CHECK_EQUAL(settings->enabledPlugins().front(), "org.example.Enabled");
    BOOST_CHECK_EQUAL(settings->disabledPlugins().size(), 2u);

    settings->userData()["theme"] = "light";
    const auto &constSettings = *settings;
    BOOST_CHECK_EQUAL(constSettings.userData().at("theme").toString(), "light");
}

BOOST_AUTO_TEST_CASE(test_json_rejects_invalid_and_conflicting_ids) {
    std::string errorMessage;
    BOOST_CHECK(!stdc::pluginsystem::PluginSettings::fromJson(stdc::json::Array(), &errorMessage));
    BOOST_CHECK(!errorMessage.empty());

    const stdc::json::Value invalid = stdc::json::Object{
        {"enabledPlugins", stdc::json::Array{"Plugin", "Plugin"}},
    };
    BOOST_CHECK(!stdc::pluginsystem::PluginSettings::fromJson(invalid, &errorMessage));

    const stdc::json::Value conflicting = stdc::json::Object{
        {"disabledPlugins", stdc::json::Array{"Plugin"}},
        {"enabledPlugins",  stdc::json::Array{"Plugin"}},
    };
    BOOST_CHECK(!stdc::pluginsystem::PluginSettings::fromJson(conflicting, &errorMessage));

    const stdc::json::Value invalidUserData = stdc::json::Object{
        {"userData", stdc::json::Array{}},
    };
    BOOST_CHECK(!stdc::pluginsystem::PluginSettings::fromJson(invalidUserData, &errorMessage));
}

BOOST_AUTO_TEST_CASE(test_missing_user_data_defaults_to_empty_object) {
    const auto settings = stdc::pluginsystem::PluginSettings::fromJson(stdc::json::Object{});
    BOOST_REQUIRE(settings);
    BOOST_CHECK(settings->userData().empty());
    BOOST_CHECK(settings->toJson()["userData"].isObject());
}

BOOST_AUTO_TEST_SUITE_END()
