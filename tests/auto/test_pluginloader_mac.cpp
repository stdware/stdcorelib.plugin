// SPDX-License-Identifier: MIT

#include <mach-o/fat.h>
#include <mach-o/loader.h>

#include <cstring>
#include <string>
#include <vector>

#include "pluginloader_fixture.h"

namespace {

    // The smallest Mach-O image the reader accepts, laid out so that a test can lie about one
    // field at a time. There is one LC_SEGMENT_64 carrying two sections, a decoy ahead of the real
    // one so that skipping a section is exercised too.
    constexpr uint32_t sectionsPerSegment = 2;
    constexpr uint32_t segmentSize =
        sizeof(segment_command_64) + sectionsPerSegment * sizeof(section_64);
    constexpr uint64_t commandsOffset = sizeof(mach_header_64);
    constexpr uint64_t payloadOffset = commandsOffset + segmentSize;

    struct MachOImage {
        mach_header_64 header{};
        segment_command_64 segment{};
        std::vector<section_64> sections;
        std::string payload;

        explicit MachOImage(std::string metadata) : payload(std::move(metadata)) {
            // The reader looks at the magic and the load commands only, so the architecture and
            // file type are left as they are.
            header.magic = MH_MAGIC_64;
            header.ncmds = 1;
            header.sizeofcmds = segmentSize;

            segment.cmd = LC_SEGMENT_64;
            segment.cmdsize = segmentSize;
            std::memcpy(segment.segname, "__TEXT", sizeof("__TEXT"));
            segment.nsects = sectionsPerSegment;

            sections.resize(sectionsPerSegment);
            std::memcpy(sections[0].sectname, "__const", sizeof("__const"));
            std::memcpy(sections[0].segname, "__TEXT", sizeof("__TEXT"));
            std::memcpy(sections[1].sectname, "stdc_metadata", sizeof("stdc_metadata"));
            std::memcpy(sections[1].segname, "__TEXT", sizeof("__TEXT"));
            sections[1].offset = static_cast<uint32_t>(payloadOffset);
            sections[1].size = payload.size();
        }

        /// Serializes the headers as they currently stand, fixing up nothing.
        std::string build() const {
            std::string bytes(payloadOffset, '\0');
            std::memcpy(bytes.data(), &header, sizeof(header));
            std::memcpy(bytes.data() + commandsOffset, &segment, sizeof(segment));
            for (size_t i = 0; i < sections.size(); ++i) {
                std::memcpy(bytes.data() + commandsOffset + sizeof(segment_command_64) +
                                i * sizeof(section_64),
                            &sections[i], sizeof(section_64));
            }
            return bytes + payload;
        }
    };

    // Like the ELF reader, this one cannot tell a truncated load command from a library that
    // simply carries no metadata, so nearly everything it rejects reports the same way.
    const char *const noMetadata = "does not contain embedded plugin metadata";

}

BOOST_AUTO_TEST_SUITE(test_pluginloader_mac)

BOOST_AUTO_TEST_CASE(test_synthetic_macho_is_accepted) {
    malformed::checkAccepted(MachOImage(malformed::envelope()).build());
}

BOOST_AUTO_TEST_CASE(test_truncated_before_header) {
    malformed::checkRejected(
        MachOImage(malformed::envelope()).build().substr(0, sizeof(mach_header_64) - 1),
        noMetadata);
}

BOOST_AUTO_TEST_CASE(test_bad_magic) {
    MachOImage image(malformed::envelope());
    image.header.magic = 0x42424242;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_command_count_larger_than_the_command_area) {
    MachOImage image(malformed::envelope());
    image.header.ncmds = 4096;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_command_area_past_end_of_file) {
    MachOImage image(malformed::envelope());
    image.header.sizeofcmds = 0xFFFFFF00;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_zero_command_size_does_not_advance) {
    // A load command that claims no size would walk the reader in place forever if it were
    // believed, so it has to be rejected outright.
    MachOImage image(malformed::envelope());
    image.segment.cmdsize = 0;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_command_size_past_the_command_area) {
    MachOImage image(malformed::envelope());
    image.segment.cmdsize = segmentSize + 1;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_command_size_too_small_for_a_segment) {
    MachOImage image(malformed::envelope());
    image.segment.cmdsize = sizeof(load_command);
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_section_count_larger_than_the_command) {
    MachOImage image(malformed::envelope());
    image.segment.nsects = 4096;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_metadata_section_in_another_segment) {
    // The section name alone is not enough. It has to sit in __TEXT, which is where
    // STDC_PLUGIN_METADATA_SECTION puts it.
    MachOImage image(malformed::envelope());
    std::memcpy(image.sections[1].segname, "__DATA", sizeof("__DATA"));
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_without_a_metadata_section) {
    MachOImage image(malformed::envelope());
    std::memcpy(image.sections[1].sectname, "__cstring", sizeof("__cstring"));
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_empty_metadata_section) {
    MachOImage image(malformed::envelope());
    image.sections[1].size = 0;
    malformed::checkRejected(image.build(), nullptr);
}

BOOST_AUTO_TEST_CASE(test_metadata_offset_past_end_of_file) {
    MachOImage image(malformed::envelope());
    image.sections[1].offset = 0xFFFFFF00;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_metadata_size_runs_past_end_of_file) {
    MachOImage image(malformed::envelope());
    image.sections[1].size = 0xFFFFFF00;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_metadata_padded_with_nuls_is_accepted) {
    // The linker zero pads the section, so the reader has to tolerate the padding.
    malformed::checkAccepted(MachOImage(malformed::envelope() + std::string(64, '\0')).build());
}

BOOST_AUTO_TEST_CASE(test_metadata_section_holding_garbage) {
    malformed::checkRejected(MachOImage("not json at all").build(), nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
