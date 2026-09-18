// SPDX-License-Identifier: MIT

#ifdef _WIN32

#  include <cstring>
#  include <string>
#  include <vector>

#  include <stdcorelib/platform/windows/stdc_windows.h>

#  include "pluginloader_fixture.h"

namespace {

    // The smallest image the PE reader accepts, laid out so that a test can lie about one field
    // at a time. The optional header is empty because the reader uses SizeOfOptionalHeader only
    // to find the section table, never the header itself.
    constexpr uint32_t ntOffset = sizeof(IMAGE_DOS_HEADER);
    constexpr uint32_t fileHeaderOffset = ntOffset + sizeof(DWORD);
    constexpr uint32_t sectionsOffset = fileHeaderOffset + sizeof(IMAGE_FILE_HEADER);
    constexpr uint16_t sectionCount = 2;
    constexpr uint32_t payloadOffset = sectionsOffset + sectionCount * sizeof(IMAGE_SECTION_HEADER);

    struct PeImage {
        IMAGE_DOS_HEADER dos{};
        DWORD signature = IMAGE_NT_SIGNATURE;
        IMAGE_FILE_HEADER fileHeader{};
        std::vector<IMAGE_SECTION_HEADER> sections;
        std::string payload;

        explicit PeImage(std::string metadata) : payload(std::move(metadata)) {
            dos.e_magic = IMAGE_DOS_SIGNATURE;
            dos.e_lfanew = static_cast<LONG>(ntOffset);
            fileHeader.NumberOfSections = sectionCount;
            fileHeader.SizeOfOptionalHeader = 0;
            fileHeader.Characteristics = IMAGE_FILE_DLL;

            sections.resize(sectionCount);
            // A decoy ahead of the real one, so that skipping a section is exercised too.
            std::memcpy(sections[0].Name, ".text\0\0\0", IMAGE_SIZEOF_SHORT_NAME);
            std::memcpy(sections[1].Name, ".stdcmd", IMAGE_SIZEOF_SHORT_NAME);
            sections[1].Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA;
            sections[1].SizeOfRawData = static_cast<DWORD>(payload.size());
            sections[1].Misc.VirtualSize = static_cast<DWORD>(payload.size());
            sections[1].PointerToRawData = payloadOffset;
        }

        /// Serializes the headers as they currently stand, fixing up nothing.
        std::string build() const {
            std::string bytes(payloadOffset, '\0');
            std::memcpy(bytes.data(), &dos, sizeof(dos));
            std::memcpy(bytes.data() + ntOffset, &signature, sizeof(signature));
            std::memcpy(bytes.data() + fileHeaderOffset, &fileHeader, sizeof(fileHeader));
            for (size_t i = 0; i < sections.size(); ++i) {
                std::memcpy(bytes.data() + sectionsOffset + i * sizeof(IMAGE_SECTION_HEADER),
                            &sections[i], sizeof(IMAGE_SECTION_HEADER));
            }
            return bytes + payload;
        }
    };

}

BOOST_AUTO_TEST_SUITE(test_pluginloader_win)

BOOST_AUTO_TEST_CASE(test_synthetic_pe_is_accepted) {
    malformed::checkAccepted(PeImage(malformed::envelope()).build());
}

BOOST_AUTO_TEST_CASE(test_truncated_before_dos_header) {
    malformed::checkRejected(
        PeImage(malformed::envelope()).build().substr(0, sizeof(IMAGE_DOS_HEADER) - 1),
        "is not a PE library");
}

BOOST_AUTO_TEST_CASE(test_bad_dos_magic) {
    PeImage image(malformed::envelope());
    image.dos.e_magic = 0x4242;
    malformed::checkRejected(image.build(), "is not a PE library");
}

BOOST_AUTO_TEST_CASE(test_negative_nt_offset) {
    PeImage image(malformed::envelope());
    image.dos.e_lfanew = -1;
    malformed::checkRejected(image.build(), "is not a PE library");
}

BOOST_AUTO_TEST_CASE(test_nt_offset_past_end_of_file) {
    PeImage image(malformed::envelope());
    image.dos.e_lfanew = 0x7FFFFFFF;
    malformed::checkRejected(image.build(), "is not a PE library");
}

BOOST_AUTO_TEST_CASE(test_bad_nt_signature) {
    PeImage image(malformed::envelope());
    image.signature = IMAGE_NT_SIGNATURE + 1;
    malformed::checkRejected(image.build(), "is not a PE library");
}

BOOST_AUTO_TEST_CASE(test_not_a_dll) {
    PeImage image(malformed::envelope());
    image.fileHeader.Characteristics = 0;
    malformed::checkRejected(image.build(), "is not a PE library");
}

BOOST_AUTO_TEST_CASE(test_section_count_larger_than_the_file) {
    PeImage image(malformed::envelope());
    image.fileHeader.NumberOfSections = 4096;
    malformed::checkRejected(image.build(), "invalid PE section table");
}

BOOST_AUTO_TEST_CASE(test_optional_header_size_pushes_the_table_past_the_end) {
    PeImage image(malformed::envelope());
    image.fileHeader.SizeOfOptionalHeader = 0xFFFF;
    malformed::checkRejected(image.build(), "invalid PE section table");
}

BOOST_AUTO_TEST_CASE(test_without_a_metadata_section) {
    PeImage image(malformed::envelope());
    std::memcpy(image.sections[1].Name, ".rdata\0", IMAGE_SIZEOF_SHORT_NAME);
    malformed::checkRejected(image.build(), "does not contain embedded plugin metadata");
}

BOOST_AUTO_TEST_CASE(test_metadata_section_must_not_be_executable) {
    PeImage image(malformed::envelope());
    image.sections[1].Characteristics |= IMAGE_SCN_MEM_EXECUTE;
    malformed::checkRejected(image.build(), "contains invalid plugin metadata");
}

BOOST_AUTO_TEST_CASE(test_metadata_section_must_not_be_writable) {
    PeImage image(malformed::envelope());
    image.sections[1].Characteristics |= IMAGE_SCN_MEM_WRITE;
    malformed::checkRejected(image.build(), "contains invalid plugin metadata");
}

BOOST_AUTO_TEST_CASE(test_metadata_section_must_hold_initialized_data) {
    PeImage image(malformed::envelope());
    image.sections[1].Characteristics = IMAGE_SCN_CNT_UNINITIALIZED_DATA;
    malformed::checkRejected(image.build(), "contains invalid plugin metadata");
}

BOOST_AUTO_TEST_CASE(test_empty_metadata_section) {
    PeImage image(malformed::envelope());
    image.sections[1].SizeOfRawData = 0;
    image.sections[1].Misc.VirtualSize = 0;
    malformed::checkRejected(image.build(), "contains invalid plugin metadata");
}

BOOST_AUTO_TEST_CASE(test_metadata_offset_past_end_of_file) {
    PeImage image(malformed::envelope());
    image.sections[1].PointerToRawData = 0xFFFFFF00;
    malformed::checkRejected(image.build(), "contains invalid plugin metadata");
}

BOOST_AUTO_TEST_CASE(test_metadata_size_runs_past_end_of_file) {
    PeImage image(malformed::envelope());
    image.sections[1].SizeOfRawData = 0xFFFFFF00;
    image.sections[1].Misc.VirtualSize = 0xFFFFFF00;
    malformed::checkRejected(image.build(), "contains invalid plugin metadata");
}

BOOST_AUTO_TEST_CASE(test_virtual_size_bounds_the_metadata) {
    // A virtual size below the raw size wins, which here cuts the envelope in half.
    PeImage image(malformed::envelope());
    image.sections[1].Misc.VirtualSize = static_cast<DWORD>(image.payload.size() / 2);
    malformed::checkRejected(image.build(), nullptr);
}

BOOST_AUTO_TEST_CASE(test_metadata_padded_with_nuls_is_accepted) {
    // The linker zero pads the section, so the reader has to tolerate the padding.
    malformed::checkAccepted(PeImage(malformed::envelope() + std::string(64, '\0')).build());
}

BOOST_AUTO_TEST_CASE(test_metadata_section_holding_garbage) {
    malformed::checkRejected(PeImage("not json at all").build(), nullptr);
}

BOOST_AUTO_TEST_SUITE_END()

#endif // _WIN32
