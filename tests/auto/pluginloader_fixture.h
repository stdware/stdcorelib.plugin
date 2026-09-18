// SPDX-License-Identifier: MIT

#ifndef TEST_PLUGINLOADER_FIXTURE_H
#define TEST_PLUGINLOADER_FIXTURE_H

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <stdcorelib/plugin/pluginloader.h>

#include <boost/test/unit_test.hpp>

/// Shared by the per-platform tests that feed the embedded metadata reader malformed binaries.
///
/// Each of those files assembles the smallest image its own platform's reader accepts and then
/// lies about one field at a time. What they have in common is only how the bytes reach the
/// loader and what the loader is expected to do with them.
namespace malformed {

    /// A file that exists for the duration of one test case.
    ///
    /// It goes next to the test plugins rather than in the system temporary directory, so that a
    /// leftover from a killed run is easy to spot.
    class TempFile {
    public:
        explicit TempFile(const std::string &bytes) {
            static int counter = 0;
            _path = std::filesystem::path(TEST_PLUGINLOADER_PLUGIN_PATH).parent_path() /
                    ("malformed-" + std::to_string(counter++) + ".bin");
            std::ofstream file(_path, std::ios::binary | std::ios::trunc);
            file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }

        ~TempFile() {
            std::error_code ec;
            std::filesystem::remove(_path, ec);
        }

        TempFile(const TempFile &) = delete;
        TempFile &operator=(const TempFile &) = delete;

        const std::filesystem::path &path() const {
            return _path;
        }

    private:
        std::filesystem::path _path;
    };

    inline const char *iid() {
        return "org.stdcorelib.MalformedTest";
    }

    /// The envelope a well formed metadata section holds.
    inline std::string envelope() {
        return R"({"iid":"org.stdcorelib.MalformedTest","metadata":{"answer":42}})";
    }

    /// Checks that \a bytes are read as a plugin carrying valid metadata.
    inline void checkAccepted(const std::string &bytes) {
        const TempFile file(bytes);
        const stdc::plugin::PluginLoader loader(file.path());

        BOOST_REQUIRE_MESSAGE(loader.state() == stdc::plugin::PluginLoader::Read,
                              loader.errorMessage());
        BOOST_CHECK_EQUAL(loader.iid(), iid());
        BOOST_CHECK_EQUAL(loader.metadata()["answer"].toInt(), 42);
    }

    /// Checks that \a bytes are rejected without anything being read out of them.
    ///
    /// \param reason A substring the error message must contain, or null when the bytes reach the
    ///               JSON parser and the wording is that parser's to choose.
    inline void checkRejected(const std::string &bytes, const char *reason) {
        const TempFile file(bytes);
        const stdc::plugin::PluginLoader loader(file.path());

        BOOST_CHECK_EQUAL(loader.state(), stdc::plugin::PluginLoader::Invalid);
        BOOST_REQUIRE(loader.hasError());
        if (reason) {
            BOOST_CHECK_MESSAGE(loader.errorMessage().find(reason) != std::string::npos,
                                loader.errorMessage());
        }
        BOOST_CHECK(loader.iid().empty());
        BOOST_CHECK(loader.metadata().isNull());
    }

}

#endif // TEST_PLUGINLOADER_FIXTURE_H
