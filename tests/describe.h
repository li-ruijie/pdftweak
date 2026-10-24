#pragma once

// A document described by meaning, for comparing two outputs: the pages (boxes, rotation, text,
// annotations, and transitions) and the catalog (page labels, bookmarks, viewer settings,
// attachments, form fields, encryption, and document information without Producer and the dates).

#include <filesystem>
#include <string>

namespace pdftweak::test {

struct DescribeOptions {
    std::string password;
    bool javaPermissions = false; // adjust Java's /P to the bits the port writes
    bool text = true;
};

std::string describe(const std::filesystem::path& file, const DescribeOptions& options = {});

} // namespace pdftweak::test
