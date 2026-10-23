#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace pdftweak::formats {

// Decision 10: keeps an XMP packet consistent with the document information that the final write
// produces, without parsing and reserialising it. Sets pdf:Producer to producer (when given),
// and xmp:ModifyDate and xmp:MetadataDate to date (a W3C date), each only where the packet already
// holds it as a simple element (<p:Name>text</p:Name>) or attribute (p:Name="text"), with the
// prefixes taken from the packet's xmlns declarations of the PDF 1.3 and XAP 1.0 namespaces.
// Returns nothing, so that the packet stays as it is, when it is not valid UTF-8 or holds one of
// the properties in any other form. Never adds a property.
std::optional<std::string> updateXmpPacket(std::string_view packet,
                                           const std::optional<std::string>& producer,
                                           const std::string& date);

} // namespace pdftweak::formats
