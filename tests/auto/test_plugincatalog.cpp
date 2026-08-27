// SPDX-License-Identifier: MIT

#include <array>
#include <atomic>
#include <string>
#include <thread>
#include <type_traits>

#include <stdcorelib/plugin/plugincatalog.h>

#include <boost/test/unit_test.hpp>

namespace {

    constexpr std::string_view EngineIID = "org.stdcorelib.Engine";

    struct Engine {
        std::string key;
        int value = 0;
    };

    class EngineFactory : public stdc::plugin::Plugin {
    public:
        virtual Engine *create(std::string_view key, int value) = 0;
    };

    class TestEngineFactory final : public EngineFactory {
    public:
        Engine *create(std::string_view key, int value) override {
            engine.key = key;
            engine.value = value;
            return &engine;
        }

        Engine engine;
    };

    class WrongFactory : public stdc::plugin::Plugin {
    public:
        Engine *create(std::string_view, int) {
            return nullptr;
        }
    };

    class TrackingPluginFactory final : public stdc::plugin::PluginFactory {
    public:
        explicit TrackingPluginFactory(bool *destroyed) : _destroyed(destroyed) {
        }

        ~TrackingPluginFactory() override {
            *_destroyed = true;
        }

    private:
        bool *_destroyed;
    };

    class NestedKeyCatalog final : public stdc::plugin::PluginCatalog {
    public:
        using PluginCatalog::PluginCatalog;

        size_t extractionCount() const {
            return _extractionCount;
        }

    protected:
        std::vector<std::string>
            keysFromMetadata(const stdc::json::Value &metadata) const override {
            ++_extractionCount;
            std::vector<std::string> result;
            const auto formats = metadata["engine"]["formats"].asArray();
            if (!formats) {
                return result;
            }
            for (const auto &format : *formats) {
                if (format.isString()) {
                    result.push_back(format.toString());
                }
            }
            return result;
        }

    private:
        mutable size_t _extractionCount = 0;
    };

}

BOOST_AUTO_TEST_SUITE(test_plugincatalog)

BOOST_AUTO_TEST_CASE(test_default_key_index_and_factory_ownership) {
    static_assert(std::is_move_constructible_v<stdc::plugin::PluginCatalog>);
    static_assert(std::is_move_assignable_v<stdc::plugin::PluginCatalog>);
    static_assert(!std::is_copy_constructible_v<stdc::plugin::PluginCatalog>);
    static_assert(!std::is_copy_assignable_v<stdc::plugin::PluginCatalog>);

    TestEngineFactory first;
    TestEngineFactory second;
    TestEngineFactory malformed;
    TestEngineFactory otherIID;
    bool factoryDestroyed = false;

    {
        auto factory = std::make_unique<TrackingPluginFactory>(&factoryDestroyed);
        factory->addRuntimePlugin(EngineIID, &first,
                                  stdc::json::Object{
                                      {"keys", stdc::json::Array{"svg", "shared", "svg", "", 42}}
        });
        factory->addRuntimePlugin(EngineIID, &second,
                                  stdc::json::Object{
                                      {"keys", stdc::json::Array{"png", "shared"}}
        });
        factory->addRuntimePlugin(EngineIID, &malformed,
                                  stdc::json::Object{
                                      {"keys", "not-an-array"}
        });
        factory->addRuntimePlugin("org.stdcorelib.Other", &otherIID,
                                  stdc::json::Object{
                                      {"keys", stdc::json::Array{"other"}}
        });

        stdc::plugin::PluginCatalog catalog(EngineIID, std::move(factory));
        BOOST_CHECK_EQUAL(catalog.iid(), EngineIID);

        const auto loaders = catalog.loaders();
        BOOST_REQUIRE_EQUAL(loaders.size(), 3u);

        const std::vector<std::string> expectedKeys{"svg", "shared", "png"};
        const auto keys = catalog.keys();
        BOOST_CHECK_EQUAL_COLLECTIONS(keys.begin(), keys.end(), expectedKeys.begin(),
                                      expectedKeys.end());
        BOOST_CHECK_EQUAL(catalog.loader("svg"), loaders[0]);
        BOOST_CHECK_EQUAL(catalog.loader("shared"), loaders[0]);
        BOOST_CHECK_EQUAL(catalog.loader("png"), loaders[1]);
        BOOST_CHECK(!catalog.loader("SVG"));
        BOOST_CHECK(!catalog.loader("other"));

        const auto shared = catalog.loaders("shared");
        BOOST_REQUIRE_EQUAL(shared.size(), 2u);
        BOOST_CHECK_EQUAL(shared[0], loaders[0]);
        BOOST_CHECK_EQUAL(shared[1], loaders[1]);

        auto engine = catalog.loadPlugin<Engine, EngineFactory>("shared", 42);
        BOOST_REQUIRE(engine);
        BOOST_CHECK_EQUAL(engine->key, "shared");
        BOOST_CHECK_EQUAL(engine->value, 42);
        BOOST_CHECK((!catalog.loadPlugin<Engine, WrongFactory>("shared", 42)));
        BOOST_CHECK((!catalog.loadPlugin<Engine, EngineFactory>("missing", 42)));

        TestEngineFactory later;
        catalog.factory()->addRuntimePlugin(
            EngineIID, &later,
            stdc::json::Object{{"keys", stdc::json::Array{"webp", "shared"}}});
        BOOST_CHECK(catalog.loader("webp"));
        BOOST_CHECK_EQUAL(catalog.loaders().size(), 4u);
        BOOST_CHECK_EQUAL(catalog.loaders("shared").size(), 3u);
    }

    BOOST_CHECK(factoryDestroyed);
}

BOOST_AUTO_TEST_CASE(test_custom_key_extraction_is_late_bound) {
    TestEngineFactory plugin;
    auto factory = std::make_unique<stdc::plugin::PluginFactory>();
    factory->addRuntimePlugin(
        EngineIID, &plugin,
        stdc::json::Object{
            {"engine", stdc::json::Object{{"formats", stdc::json::Array{"SVG", "theme"}}}}
    });

    NestedKeyCatalog catalog(EngineIID, std::move(factory));
    BOOST_CHECK_EQUAL(catalog.extractionCount(), 0u);
    BOOST_CHECK(catalog.loader("SVG"));
    BOOST_CHECK_EQUAL(catalog.extractionCount(), 1u);
    BOOST_CHECK(!catalog.loader("svg"));

    const std::vector<std::string> expectedKeys{"SVG", "theme"};
    const auto keys = catalog.keys();
    BOOST_CHECK_EQUAL_COLLECTIONS(keys.begin(), keys.end(), expectedKeys.begin(),
                                  expectedKeys.end());
    BOOST_CHECK_EQUAL(catalog.extractionCount(), 1u);
}

BOOST_AUTO_TEST_CASE(test_concurrent_queries_follow_factory_changes) {
    std::array<TestEngineFactory, 16> plugins;
    auto factory = std::make_unique<stdc::plugin::PluginFactory>();
    stdc::plugin::PluginCatalog catalog(EngineIID, std::move(factory));
    BOOST_CHECK(catalog.keys().empty());

    std::atomic<bool> start = false;
    std::thread writer([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (size_t i = 0; i < plugins.size(); ++i) {
            catalog.factory()->addRuntimePlugin(
                EngineIID, &plugins[i],
                stdc::json::Object{{"keys", stdc::json::Array{"engine-" + std::to_string(i)}}});
        }
    });

    std::array<std::thread, 4> readers;
    for (auto &reader : readers) {
        reader = std::thread([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < 100; ++i) {
                catalog.keys();
                catalog.loaders();
                catalog.loader("engine-0");
            }
        });
    }

    start.store(true, std::memory_order_release);
    writer.join();
    for (auto &reader : readers) {
        reader.join();
    }

    BOOST_CHECK_EQUAL(catalog.keys().size(), plugins.size());
    BOOST_CHECK_EQUAL(catalog.loaders().size(), plugins.size());
}

BOOST_AUTO_TEST_SUITE_END()
