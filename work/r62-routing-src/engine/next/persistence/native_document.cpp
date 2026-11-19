#include "persistence.hpp"

#include <cstring>
#include <sstream>

namespace r62 {
namespace persistence {

using core::Error;
using core::ErrorCode;

namespace {

// ---------------------------------------------------------------------------
// Container layout
// ---------------------------------------------------------------------------
//   line 1: R62EXT <containerVersion> <payloadMagic> <payloadVersion> <encoding> <payloadBytes>
//   then   K <keyHex> <valueHex>          (any number, catalog entries)
//   then   exactly <payloadBytes> raw bytes of the T01 native payload
//   then   Z <crc32Hex>
//
// The payload is byte-identical to what T01's saveDocument() produces, so an
// older T01 reader can still consume it after extractNativePayload(). The
// container adds exactly the three things T01DOC has no room for: a format
// version of its own, a self-describing type catalog, and a checksum. Adding
// them to T01DOC itself would be a T01 interface change, which this task
// forbids T02 from making.
const char* const kEncodingRaw = "raw";

std::string hexEncode(const std::string& value) { return core::hexEncode(value); }

std::string hexEncodeU64(uint64_t value) {
    std::ostringstream out;
    out << std::hex << value;
    return out.str();
}

bool hexToU64(const std::string& text, uint64_t* out) {
    if (text.empty()) return false;
    uint64_t value = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        uint64_t digit = 0;
        if (c >= 48 && c <= 57) digit = static_cast<uint64_t>(c - 48);
        else if (c >= 97 && c <= 102) digit = static_cast<uint64_t>(c - 97 + 10);
        else if (c >= 65 && c <= 70) digit = static_cast<uint64_t>(c - 65 + 10);
        else return false;
        value = value * 16u + digit;
    }
    *out = value;
    return true;
}

const char* const kKnownTypes[] = {
    "Atom", "Bond", "Text", "Path", "Arrow", "Bracket", "Table", "Orbital",
    "Symbol", "BioShape", "Group"
};

bool isKnownType(const std::string& name) {
    for (size_t i = 0; i < sizeof(kKnownTypes) / sizeof(kKnownTypes[0]); ++i)
        if (name == kKnownTypes[i]) return true;
    return false;
}

// The type catalog is DERIVED from the document, not declared by the caller: a
// declaration that disagreed with the data would be worse than none. Only the
// extra pst.catalog.* entries a caller supplies are merged in.
DocumentCatalog buildCatalog(const core::Document& document,
                             const NativeSaveOptions& options,
                             const DocumentCatalog* overrideCatalog) {
    DocumentCatalog catalog;
    if (overrideCatalog) catalog = *overrideCatalog;
    catalog.formatVersion = kExtFormatVersion;
    catalog.producer = "T02 persistence";
    catalog.producerVersion = "T02-1.0";

    const core::Document::Stats stats = document.stats();
    catalog.typeCounts["Atom"] = stats.aliveAtoms;
    catalog.typeCounts["Bond"] = stats.aliveBonds;
    catalog.typeCounts["Text"] = stats.aliveTexts;
    catalog.typeCounts["Path"] = 0;
    catalog.typeCounts["Annotations"] = stats.annotations;
    catalog.typeCounts["Table"] = stats.tables;
    catalog.typeCounts["Group"] = stats.groups;

    for (size_t i = 0; i < sizeof(kKnownTypes) / sizeof(kKnownTypes[0]); ++i) {
        bool present = false;
        for (size_t k = 0; k < catalog.knownTypes.size(); ++k)
            if (catalog.knownTypes[k] == kKnownTypes[i]) present = true;
        if (!present) catalog.knownTypes.push_back(kKnownTypes[i]);
    }
    for (size_t i = 0; i < options.declaredTypes.size(); ++i) {
        const std::string& name = options.declaredTypes[i];
        if (isKnownType(name)) continue;
        bool present = false;
        for (size_t k = 0; k < catalog.unknownTypes.size(); ++k)
            if (catalog.unknownTypes[k] == name) present = true;
        if (!present) catalog.unknownTypes.push_back(name);
    }
    return catalog;
}

std::string renderCatalog(const DocumentCatalog& catalog) {
    std::ostringstream out;
    out << "K " << hexEncode("format.version") << " " << catalog.formatVersion << "\n";
    out << "K " << hexEncode("producer") << " " << hexEncode(catalog.producer) << "\n";
    out << "K " << hexEncode("producer.version") << " " << hexEncode(catalog.producerVersion) << "\n";
    for (std::map<std::string, uint64_t>::const_iterator it = catalog.typeCounts.begin();
         it != catalog.typeCounts.end(); ++it) {
        bool known = isKnownType(it->first) || it->first == "Annotations";
        std::ostringstream name;
        name << (known ? "type.known." : "type.unknown.") << it->first;
        out << "K " << hexEncode(name.str()) << " " << it->second << "\n";
    }
    for (size_t i = 0; i < catalog.knownTypes.size(); ++i)
        out << "K " << hexEncode(std::string("type.known.") + catalog.knownTypes[i])
            << " " << 1 << "\n";
    for (size_t i = 0; i < catalog.unknownTypes.size(); ++i)
        out << "K " << hexEncode(std::string("type.unknown.") + catalog.unknownTypes[i])
            << " " << 1 << "\n";
    return out.str();
}

} // namespace

// ---------------------------------------------------------------------------
// Native save / load
// ---------------------------------------------------------------------------
Error saveNative(const core::Document& document, std::ostream& output,
                 const NativeSaveOptions& options, const DocumentCatalog* overrideCatalog) {
    std::ostringstream payload;
    Error saved = core::saveDocument(document, payload, options.payload);
    if (!saved.ok()) return saved;
    const std::string payloadBytes = payload.str();
    const uint64_t checksum = options.writeChecksum
                                 ? static_cast<uint64_t>(crc32(payloadBytes.data(), payloadBytes.size()))
                                 : 0ull;

    DocumentCatalog catalog = buildCatalog(document, options, overrideCatalog);
    catalog.payloadChecksum = checksum;

    output << kExtMagic << " " << kExtFormatVersion << " " << core::kNativeMagic << " "
           << core::kNativeFormatVersion << " " << kEncodingRaw << " " << payloadBytes.size()
           << "\n";
    if (options.writeCatalog) output << renderCatalog(catalog);
    output.write(payloadBytes.data(), static_cast<std::streamsize>(payloadBytes.size()));
    if (options.writeChecksum) output << "\nZ " << hexEncodeU64(checksum);
    output << "\n";
    if (!output) return Error(ErrorCode::IoError, "cannot write the native container");
    return Error::success();
}

Error saveNative(const core::Document& document, const std::string& path,
                 const NativeSaveOptions& options, const DocumentCatalog* overrideCatalog) {
    std::ostringstream buffer;
    Error saved = saveNative(document, buffer, options, overrideCatalog);
    if (!saved.ok()) return saved;
    AtomicWriteOptions atomic;
    atomic.sourcePath = document.sourcePath();
    atomic.verifyAfterWrite = true;
    return writeFileAtomic(path, buffer.str(), atomic, 0);
}

Error inspectNative(const std::string& bytes, NativeLoadReport* report) {
    const size_t firstNewline = bytes.find(10);
    if (firstNewline == std::string::npos)
        return Error(ErrorCode::Truncated, "native container has no header line");
    std::string header = bytes.substr(0, firstNewline);
    if (!header.empty() && header[header.size() - 1] == 13) header.erase(header.size() - 1);

    if (header.compare(0, std::strlen(kExtMagic), kExtMagic) != 0) {
        // A bare T01DOC payload is legal input: the container is optional.
        core::LoadReport payloadReport;
        Error detected = core::detectFormat(bytes, &payloadReport);
        if (report) {
            report->containerPresent = false;
            report->payloadBytes = bytes.size();
            report->payloadReport = payloadReport;
        }
        return detected;
    }

    std::istringstream in(header);
    std::string magic, payloadMagic, encoding;
    int containerVersion = 0, payloadVersion = 0;
    unsigned long long payloadBytes = 0;
    if (!(in >> magic >> containerVersion >> payloadMagic >> payloadVersion >> encoding
          >> payloadBytes))
        return Error(ErrorCode::ParseError, "native container header is malformed");
    if (containerVersion > kExtFormatVersion)
        return Error(ErrorCode::Unsupported,
                     "native container version is newer than this build supports");
    if (payloadMagic != core::kNativeMagic)
        return Error(ErrorCode::ParseError, "native container payload magic is not T01DOC");
    if (encoding != kEncodingRaw)
        return Error(ErrorCode::Unsupported, "unsupported native container encoding");
    if (firstNewline + 1 + payloadBytes > bytes.size())
        return Error(ErrorCode::Truncated, "native container payload is truncated");

    size_t cursor = firstNewline + 1;
    DocumentCatalog catalog;
    catalog.formatVersion = containerVersion;
    while (cursor < bytes.size() && bytes.compare(cursor, 2, "K ") == 0) {
        const size_t eol = bytes.find(10, cursor);
        if (eol == std::string::npos)
            return Error(ErrorCode::Truncated, "native container catalog line is truncated");
        std::string line = bytes.substr(cursor, eol - cursor);
        if (!line.empty() && line[line.size() - 1] == 13) line.erase(line.size() - 1);
        std::istringstream entry(line);
        std::string tag, keyHex, valueHex;
        if (!(entry >> tag >> keyHex))
            return Error(ErrorCode::ParseError, "malformed native container catalog entry");
        entry >> valueHex;
        // Catalog values are HEX-ENCODED STRINGS ONLY for the textual keys. The
        // numeric type counters are written as plain hexadecimal NUMBERS, so
        // decoding them as a hex string would fail on odd lengths. The two are
        // distinguished by key prefix, not by guessing.
        std::string key, value;
        Error decoded = core::hexDecode(keyHex, &key);
        if (!decoded.ok()) return decoded;
        const bool isCounter = key.compare(0, 5, "type.") == 0 || key == "format.version";
        if (isCounter) {
            value = valueHex;                      // already a readable hex number
        } else if (!valueHex.empty() && valueHex != "-") {
            decoded = core::hexDecode(valueHex, &value);
            if (!decoded.ok()) return decoded;
        }
        // compare() takes a LENGTH, not a "does it start with" test: passing a length
        // shorter than the literal compares a prefix of the literal and never matches.
        // Use the literal's own length so the check cannot drift when it is edited.
        const std::string unknownPrefix = "type.unknown.";
        const std::string knownPrefix = "type.known.";
        if (key.compare(0, unknownPrefix.size(), unknownPrefix) == 0)
            catalog.unknownTypes.push_back(key.substr(unknownPrefix.size()));
        else if (key.compare(0, knownPrefix.size(), knownPrefix) == 0)
            catalog.knownTypes.push_back(key.substr(knownPrefix.size()));
        else if (key == "producer") catalog.producer = value;
        else if (key == "producer.version") catalog.producerVersion = value;
        cursor = eol + 1;
    }

    const uint64_t computed = static_cast<uint64_t>(crc32(bytes.data() + cursor,
                                                           static_cast<size_t>(payloadBytes)));
    (void)computed;
    if (report) {
        report->containerPresent = true;
        report->containerVersion = containerVersion;
        report->encoding = encoding;
        report->payloadBytes = payloadBytes;
        report->payloadChecksumComputed = computed;
        report->catalog = catalog;
    }
    return Error::success();
}

Error extractNativePayload(const std::string& bytes, std::string* payload) {
    if (!payload) return Error(ErrorCode::InvalidArgument, "null payload buffer");
    const size_t firstNewline = bytes.find(10);
    if (firstNewline == std::string::npos)
        return Error(ErrorCode::Truncated, "native container has no header line");
    std::string header = bytes.substr(0, firstNewline);
    if (header.compare(0, std::strlen(kExtMagic), kExtMagic) != 0) {
        *payload = bytes;   // already a bare payload
        return Error::success();
    }
    std::istringstream in(header);
    std::string magic, payloadMagic, encoding;
    int containerVersion = 0, payloadVersion = 0;
    unsigned long long payloadBytes = 0;
    if (!(in >> magic >> containerVersion >> payloadMagic >> payloadVersion >> encoding >> payloadBytes))
        return Error(ErrorCode::ParseError, "native container header is malformed");
    if (firstNewline + 1 + payloadBytes > bytes.size())
        return Error(ErrorCode::Truncated, "native container payload is truncated");
    size_t cursor = firstNewline + 1;
    while (cursor < bytes.size() && bytes.compare(cursor, 2, "K ") == 0)
        cursor = bytes.find(10, cursor) + 1;
    *payload = bytes.substr(cursor, static_cast<size_t>(payloadBytes));
    return Error::success();
}

Error loadNative(core::Document& document, std::istream& input, NativeLoadReport* report,
                 UnknownTypePolicy policy) {
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string bytes = buffer.str();
    if (bytes.empty()) return Error(ErrorCode::Truncated, "empty native document");

    NativeLoadReport local;
    Error inspected = inspectNative(bytes, &local);
    if (!inspected.ok()) return inspected;

    // The unknown-type policy decides whether a catalog declaration this build
    // cannot read is fatal. Default is Refuse: loading a document while silently
    // dropping the object types a newer writer put there is the one outcome the
    // whole module exists to prevent.
    if (!local.catalog.unknownTypes.empty() && policy == UnknownTypePolicy::Refuse) {
        std::ostringstream reason;
        reason << "document declares " << local.catalog.unknownTypes.size()
               << " object type(s) this build does not implement: ";
        for (size_t i = 0; i < local.catalog.unknownTypes.size(); ++i) {
            if (i) reason << ", ";
            reason << local.catalog.unknownTypes[i];
        }
        return Error(ErrorCode::Unsupported, reason.str());
    }

    std::string payload;
    Error extracted = extractNativePayload(bytes, &payload);
    if (!extracted.ok()) return extracted;

    if (local.containerPresent) {
        // Verify the checksum BEFORE parsing the payload: a truncated or tampered
        // container must be reported as corruption, not as a parse error inside
        // an otherwise plausible document.
        // The checksum line is located by walking past the payload using the byte
        // count from the header. Searching the WHOLE file for "Z " would let a
        // "Z " inside payload text be mistaken for the terminator.
        size_t scan = bytes.find(10) + 1;
        while (scan < bytes.size() && bytes.compare(scan, 2, "K ") == 0)
            scan = bytes.find(10, scan) + 1;
        const size_t payloadEnd = scan + static_cast<size_t>(local.payloadBytes);
        const size_t zPos = payloadEnd <= bytes.size() ? bytes.find("Z ", payloadEnd)
                                                      : std::string::npos;
        if (zPos != std::string::npos) {
            std::string hex = bytes.substr(zPos + 2);
            while (!hex.empty() && (hex[hex.size() - 1] == 10 || hex[hex.size() - 1] == 13))
                hex.erase(hex.size() - 1);
            uint64_t stored = 0;
            if (!hexToU64(hex, &stored))
                return Error(ErrorCode::ParseError, "native container checksum is malformed");
            local.payloadChecksumStored = stored;
            local.payloadChecksumComputed =
                static_cast<uint64_t>(crc32(payload.data(), payload.size()));
            local.checksumVerified = stored == local.payloadChecksumComputed;
            if (!local.checksumVerified)
                return Error(ErrorCode::ParseError,
                             "native container checksum mismatch: the payload is corrupt");
        }
    }

    std::istringstream payloadStream(payload);
    core::LoadReport payloadReport;
    Error loaded = core::loadDocument(document, payloadStream, &payloadReport);
    if (!loaded.ok()) return loaded;
    local.payloadReport = payloadReport;
    if (report) *report = local;
    return Error::success();
}

Error loadNative(core::Document& document, const std::string& path, NativeLoadReport* report,
                 UnknownTypePolicy policy) {
    std::string bytes;
    Error read = readFileBytes(path, &bytes);
    if (!read.ok()) return read;
    std::istringstream input(bytes);
    return loadNative(document, input, report, policy);
}

} // namespace persistence
} // namespace r62

