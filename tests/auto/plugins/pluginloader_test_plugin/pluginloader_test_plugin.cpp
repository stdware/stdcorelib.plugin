// SPDX-License-Identifier: MIT

#include <stdcorelib/plugin/plugin.h>

namespace {

    class LoaderTestPlugin : public stdc::plugin::Plugin {};

}

STDC_EXPORT_PLUGIN(LoaderTestPlugin)

#if !defined(_WIN32)

#  if defined(__APPLE__)
#    define STDC_TEST_METADATA_SECTION __attribute__((section("__TEXT,stdc_metadata")))
#  else
#    define STDC_TEST_METADATA_SECTION __attribute__((section(".stdc_metadata")))
#  endif

STDC_TEST_METADATA_SECTION __attribute__((used)) static constexpr char metadata[] = R"({
    "$version": "1.0",
    "iid": "org.stdcorelib.LoaderTest",
    "metadata": {"answer": 42}
})";

#endif
