#include "persistence.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace r62 {
namespace persistence {

const char* const kExtMagic = "R62EXT";
const char* const kSelMagic = "R62SEL1";
const char* const kExtensionPrefix = "pst.";

// ---------------------------------------------------------------------------
// Support status
// ---------------------------------------------------------------------------
SupportStatus::SupportStatus()
    : cdxmlImportSubset(true),
      cdxmlImportPreserving(true),
      cdxmlExportNew(true),
      cdxmlExportLossless(true),
      cdxmlExportRefusesLossy(true),
      nativeDocument(true),
      selectionPackage(true),
      win32Clipboard(false),
      binaryCdxImport(false) {}

SupportStatus supportStatus() { return SupportStatus(); }

const char* cdxSupportStatus() {
    return "Binary .cdx is NOT read or written by this module. No openly published "
           "vendor specification for the .cdx binary container was found, and no "
           "officially supported third-party converter is reachable from this "
           "project, so no guessed parser is shipped. CDXML is the interchange "
           "path. If a vendor conversion route is ever verified, it belongs "
           "behind this same readCdxml entry point as a separate documented step.";
}

const char* clipboardOwnershipNote() {
    return "This module produces and consumes the selection PACKAGE BYTES only. "
           "Publishing to the Win32 clipboard (OpenClipboard/SetClipboardData, "
           "format registration, drag-and-drop) belongs to T05; no window handle, "
           "message loop or clipboard API is touched here.";
}

// ---------------------------------------------------------------------------
// RefusalReport
// ---------------------------------------------------------------------------
std::string RefusalReport::summary() const {
    if (items.empty()) return std::string("no refusals");
    std::ostringstream out;
    for (size_t i = 0; i < items.size(); ++i) {
        const Refusal& r = items[i];
        out << r.path;
        if (!r.element.empty()) out << " <" << r.element << ">";
        if (r.sourceId != 0) out << " id=" << r.sourceId;
        if (r.attribute.size()) out << " @" << r.attribute;
        out << ": " << r.reason;
        if (i + 1 != items.size()) out << "\n";
    }
    return out.str();
}

// ---------------------------------------------------------------------------
// SourceIdMap
// ---------------------------------------------------------------------------
void SourceIdMap::clear() {
    forward_.clear();
    backward_.clear();
    collisionSet_.clear();
    collisionsSnapshot_.clear();
}

bool SourceIdMap::bind(int32_t sourceId, const core::ObjectRef& ref) {
    if (sourceId == kNoSourceId || !ref.valid()) return false;
    std::vector<core::ObjectRef>& refs = forward_[sourceId];
    for (size_t i = 0; i < refs.size(); ++i)
        if (refs[i] == ref) return true;   // idempotent
    refs.push_back(ref);
    if (refs.size() > 1) {
        // A source id that maps to more than one object. NOTE that this is NOT only
        // about duplicate ids in a file: a bond legitimately binds its own id AND its
        // two endpoint ids to different objects, so an id shared by several objects is
        // the normal case during a read. The collision set is therefore a std::set, not
        // a vector: a linear std::find over a growing vector is what made a 5,000-atom
        // read take seconds, and the reader relies on this path being hot.
        collisionSet_.insert(sourceId);
        return false;   // first mapping wins; the caller is told
    }
    backward_[ref] = sourceId;
    return true;
}

const std::vector<int32_t>& SourceIdMap::collisions() const {
    // Materialised once per call so the public vector API is preserved without keeping a
    // second growing linear structure.
    collisionsSnapshot_.assign(collisionSet_.begin(), collisionSet_.end());
    return collisionsSnapshot_;
}

bool SourceIdMap::collides(int32_t sourceId) const {
    std::map<int32_t, std::vector<core::ObjectRef> >::const_iterator it =
        forward_.find(sourceId);
    return it != forward_.end() && it->second.size() > 1;
}

const std::vector<core::ObjectRef>* SourceIdMap::refsFor(int32_t sourceId) const {
    std::map<int32_t, std::vector<core::ObjectRef> >::const_iterator it =
        forward_.find(sourceId);
    return it == forward_.end() ? 0 : &it->second;
}

core::ObjectRef SourceIdMap::refFor(int32_t sourceId) const {
    const std::vector<core::ObjectRef>* refs = refsFor(sourceId);
    return (refs && !refs->empty()) ? (*refs)[0] : core::ObjectRef();
}

int32_t SourceIdMap::sourceIdFor(const core::ObjectRef& ref) const {
    std::map<core::ObjectRef, int32_t>::const_iterator it = backward_.find(ref);
    return it == backward_.end() ? kNoSourceId : it->second;
}

std::vector<int32_t> SourceIdMap::intersections(const SourceIdMap& other) const {
    std::vector<int32_t> shared;
    for (std::map<int32_t, std::vector<core::ObjectRef> >::const_iterator it =
             forward_.begin(); it != forward_.end(); ++it)
        if (other.forward_.find(it->first) != other.forward_.end())
            shared.push_back(it->first);
    return shared;
}

namespace {

void collectSourceIds(const core::ObjectSlab<core::AtomRecord>& slab,
                      core::ObjectKind kind, SourceIdMap* map) {
    const std::vector<core::AtomRecord>& records = slab.records();
    for (size_t i = 0; i < records.size(); ++i)
        if (records[i].isAlive() && records[i].sourceId != 0)
            map->bind(records[i].sourceId, core::ObjectRef(kind, records[i].id));
}
void collectSourceIds(const core::ObjectSlab<core::BondRecord>& slab,
                      core::ObjectKind kind, SourceIdMap* map) {
    const std::vector<core::BondRecord>& records = slab.records();
    for (size_t i = 0; i < records.size(); ++i)
        if (records[i].isAlive() && records[i].sourceId != 0)
            map->bind(records[i].sourceId, core::ObjectRef(kind, records[i].id));
}
void collectSourceIds(const core::ObjectSlab<core::TextRecord>& slab,
                      core::ObjectKind kind, SourceIdMap* map) {
    const std::vector<core::TextRecord>& records = slab.records();
    for (size_t i = 0; i < records.size(); ++i)
        if (records[i].isAlive() && records[i].sourceId != 0)
            map->bind(records[i].sourceId, core::ObjectRef(kind, records[i].id));
}
void collectSourceIds(const core::ObjectSlab<core::PathRecord>& slab,
                      core::ObjectKind kind, SourceIdMap* map) {
    const std::vector<core::PathRecord>& records = slab.records();
    for (size_t i = 0; i < records.size(); ++i)
        if (records[i].isAlive() && records[i].sourceId != 0)
            map->bind(records[i].sourceId, core::ObjectRef(kind, records[i].id));
}
void collectSourceIds(const core::ObjectSlab<core::AnnotationRecord>& slab,
                      core::ObjectKind kind, SourceIdMap* map) {
    const std::vector<core::AnnotationRecord>& records = slab.records();
    for (size_t i = 0; i < records.size(); ++i)
        if (records[i].isAlive() && records[i].sourceId != 0)
            map->bind(records[i].sourceId, core::ObjectRef(kind, records[i].id));
}
void collectSourceIds(const core::ObjectSlab<core::TableRecord>& slab,
                      core::ObjectKind kind, SourceIdMap* map) {
    const std::vector<core::TableRecord>& records = slab.records();
    for (size_t i = 0; i < records.size(); ++i)
        if (records[i].isAlive() && records[i].sourceId != 0)
            map->bind(records[i].sourceId, core::ObjectRef(kind, records[i].id));
}
void collectSourceIds(const core::ObjectSlab<core::GroupRecord>& slab,
                      core::ObjectKind kind, SourceIdMap* map) {
    const std::vector<core::GroupRecord>& records = slab.records();
    for (size_t i = 0; i < records.size(); ++i)
        if (records[i].isAlive() && records[i].sourceId != 0)
            map->bind(records[i].sourceId, core::ObjectRef(kind, records[i].id));
}

} // namespace

SourceIdMap SourceIdMap::fromDocument(const core::Document& document) {
    SourceIdMap map;
    const core::Document& d = document;
    collectSourceIds(d.atoms(), core::ObjectKind::Atom, &map);
    collectSourceIds(d.bonds(), core::ObjectKind::Bond, &map);
    collectSourceIds(d.texts(), core::ObjectKind::Text, &map);
    collectSourceIds(d.paths(), core::ObjectKind::Path, &map);
    collectSourceIds(d.arrows(), core::ObjectKind::Arrow, &map);
    collectSourceIds(d.brackets(), core::ObjectKind::Bracket, &map);
    collectSourceIds(d.orbitals(), core::ObjectKind::Orbital, &map);
    collectSourceIds(d.symbols(), core::ObjectKind::Symbol, &map);
    collectSourceIds(d.bioShapes(), core::ObjectKind::BioShape, &map);
    collectSourceIds(d.tables(), core::ObjectKind::Table, &map);
    collectSourceIds(d.groups(), core::ObjectKind::Group, &map);
    return map;
}

CdxmlReadReport::CdxmlReadReport()
    : pages(0), fragments(0), nodes(0), bonds(0), texts(0), textRuns(0),
      fonts(0), fontSizes(0), colors(0), graphics(0), rawElements(0),
      rawFragments(0), unknownAttributesPreserved(0), duplicateSourceIds(0),
      danglingReferences(0), objectsImported(0) {}

CdxmlWriteFidelity::CdxmlWriteFidelity()
    : objectsFromSource(0), objectsRegenerated(0), rawFragmentsEmitted(0),
      unknownAttributesEmitted(0), newFileIdsAssigned(0) {}

CdxmlReadOptions::CdxmlReadOptions()
    // refuseUnmappedSemantics defaults to FALSE because it selects the STRICT
    // attribute gate. With the gate on, the very first attribute outside the
    // promoted table aborts the load - correct behaviour for a caller that wants a
    // hard guarantee, useless as a default for a reader that must open real
    // ChemDraw files. Unknown attributes are preserved either way, so the default
    // is lossless without being brittle.
    : preserveUnknowns(true), refuseUnmappedSemantics(false),
      // strictReferences defaults to TRUE for DANGLING references (a bond that
      // points nowhere cannot be represented) but duplicate ids are only a
      // warning: the vendor specification says ids need only be unique within a
      // container, and 56 of 616 shipped ChemDraw files contain a repeat.
      strictReferences(false), maxObjects(4000000u) {}

CdxmlWriteOptions::CdxmlWriteOptions()
    : mode(CdxmlExportMode::Lossless), failOnLossyEntities(true),
      allowDuplicateSourceIds(false), prettyPrint(true) {}

CdxmlFidelityPlan::CdxmlFidelityPlan()
    : objectsPreserved(0), objectsRegenerated(0), objectsLost(0),
      objectsRefused(0) {}

DocumentRoundTrip::DocumentRoundTrip()
    : structural(false), display(false), byteExact(false) {}

DocumentCatalog::DocumentCatalog() : formatVersion(kExtFormatVersion),
    producer("T02 persistence"), producerVersion("T02-1.0"),
    payloadChecksum(0) {}

NativeSaveOptions::NativeSaveOptions()
    : writeCatalog(true), writeChecksum(true),
      unknownPolicy(UnknownTypePolicy::Refuse) {}

NativeLoadReport::NativeLoadReport()
    : containerVersion(0), containerPresent(false), payloadBytes(0),
      payloadChecksumStored(0), payloadChecksumComputed(0),
      checksumVerified(false) {}

SelectionPackageInfo::SelectionPackageInfo()
    : version(0), objectCount(0), atomCount(0), bondCount(0), textCount(0),
      otherCount(0), byteSize(0) {}

PasteMapping::PasteMapping() : referencesRewritten(0) {}

AtomicWriteOptions::AtomicWriteOptions()
    : verifyAfterWrite(true), verifyParses(false) {}

AtomicWriteResult::AtomicWriteResult()
    : bytesWritten(0), verified(false), replacedExisting(false),
      tempRemoved(false) {}

// ---------------------------------------------------------------------------
// CRC-32 (IEEE 802.3), table-free bitwise implementation.
// ---------------------------------------------------------------------------
// Deliberately the simple reflected-polynomial loop: a table would be faster
// and a wrong table impossible to spot by reading. 5 MB of payload hashes in
// well under a millisecond either way.
uint32_t crc32(const void* data, size_t bytes, uint32_t seed) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    uint32_t crc = seed ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < bytes; ++i) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// Paths (the ONE place this module touches a path)
// ---------------------------------------------------------------------------
std::string pathDirectory(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return std::string(".");
    if (slash == 0) return path.substr(0, 1);
    return path.substr(0, slash);
}

std::string pathFileName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string pathJoin(const std::string& directory, const std::string& name) {
    if (directory.empty()) return name;
    const char last = directory[directory.size() - 1];
    if (last == '/' || last == '\\') return directory + name;
    return directory + "/" + name;
}

// ---------------------------------------------------------------------------
// File helpers
// ---------------------------------------------------------------------------
#ifdef _WIN32
// Forward declaration: the wide reader is defined below with the other Win32 helpers.
bool readWideInto(const std::string& path, std::string* bytes, std::string* error);
#endif

core::Error readFileBytes(const std::string& path, std::string* bytes) {
    if (!bytes) return core::Error(core::ErrorCode::InvalidArgument, "null byte buffer");
    bytes->clear();
#ifdef _WIN32
    // The WIDE API, for the same reason every write is wide: a narrow ifstream resolves a
    // non-ASCII path through the process ANSI code page and opens a different file (or
    // none). Reading is on the same footing as writing here.
    std::string error;
    if (readWideInto(path, bytes, &error))
        return core::Error::success();
    return core::Error(core::ErrorCode::IoError, error);
#else
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) return core::Error(core::ErrorCode::IoError, "cannot open file: " + path);
    char buffer[65536];
    while (input) {
        input.read(buffer, sizeof(buffer));
        const std::streamsize got = input.gcount();
        if (got <= 0) break;
        bytes->append(buffer, static_cast<size_t>(got));
    }
    if (input.bad())
        return core::Error(core::ErrorCode::IoError, "read failed: " + path);
    return core::Error::success();
#endif
}

#ifdef _WIN32

namespace {

// Convert a UTF-8 std::string into a wide path. Doing it HERE and only here is
// what lets a Chinese-named file work regardless of the process ANSI code page.
std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                           static_cast<int>(utf8.size()), 0, 0);
    if (needed <= 0) return std::wstring();
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                        &wide[0], needed);
    // Forward slashes are legal in an NT object name but NOT in every Win32 file API
    // (MoveFileExW among them), so they are normalised here, in the one function that
    // produces a wide path. Without this, saving to a path built with '/' fails at the
    // rename while the temporary file it wrote is perfectly fine.
    for (size_t i = 0; i < wide.size(); ++i)
        if (wide[i] == L'/') wide[i] = L'\\';
    return wide;
}

bool fileIdentity(const std::string& path, DWORD* volume, DWORD* indexHigh,
                  DWORD* indexLow) {
    const std::wstring wide = toWide(path);
    HANDLE handle = CreateFileW(wide.c_str(), 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
    if (handle == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info;
    const BOOL ok = GetFileInformationByHandle(handle, &info);
    CloseHandle(handle);
    if (!ok) return false;
    *volume = info.dwVolumeSerialNumber;
    *indexHigh = info.nFileIndexHigh;
    *indexLow = info.nFileIndexLow;
    return true;
}

} // namespace

bool sameFile(const std::string& a, const std::string& b) {
    DWORD va = 0, ha = 0, la = 0, vb = 0, hb = 0, lb = 0;
    const bool okA = fileIdentity(a, &va, &ha, &la);
    const bool okB = fileIdentity(b, &vb, &hb, &lb);
    if (okA && okB) return va == vb && ha == hb && la == lb;
    // A file that does not exist yet cannot be identical to one that does,
    // but equal path spellings still count as the same target.
    return toWide(a) == toWide(b);
}

namespace {

// Write a file by PATH USING THE WIDE API. A narrow std::ofstream interprets the path in
// the process ANSI code page, so a path with non-ASCII characters silently becomes a
// DIFFERENT path (or fails); every write in this module goes through here instead.
bool writeWide(const std::string& path, const std::string& bytes, std::string* error) {
    const std::wstring wide = toWide(path);
    HANDLE handle = CreateFileW(wide.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, 0);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error) *error = "cannot create file (wide API): " + path;
        return false;
    }
    size_t written = 0;
    bool ok = true;
    while (written < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(
            (bytes.size() - written) > 0x40000000ull ? 0x40000000ull : (bytes.size() - written));
        DWORD done = 0;
        if (!WriteFile(handle, bytes.data() + written, chunk, &done, 0) || done == 0) {
            ok = false;
            break;
        }
        written += done;
    }
    if (!ok && error) *error = "write failed (wide API): " + path;
    CloseHandle(handle);
    return ok;
}

} // namespace

bool readWideInto(const std::string& path, std::string* bytes, std::string* error) {
    const std::wstring wide = toWide(path);
    HANDLE handle = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, 0);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error) *error = "cannot open file (wide API): " + path;
        return false;
    }
    char buffer[65536];
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(handle, buffer, sizeof(buffer), &got, 0)) {
            CloseHandle(handle);
            if (error) *error = "read failed (wide API): " + path;
            return false;
        }
        if (got == 0) break;
        bytes->append(buffer, static_cast<size_t>(got));
    }
    CloseHandle(handle);
    return true;
}

namespace {

bool removeWide(const std::string& path) {
    return DeleteFileW(toWide(path).c_str()) != 0;
}

bool moveIntoPlace(const std::wstring& from, const std::wstring& to,
                   bool* replaced) {
    *replaced = false;
    if (MoveFileExW(from.c_str(), to.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    return false;
}

} // namespace

bool fileExists(const std::string& path) {
#ifdef _WIN32
    const std::wstring wide = toWide(path);
    const DWORD attributes = GetFileAttributesW(wide.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES;
#else
    std::ifstream probe(path.c_str(), std::ios::binary);
    return static_cast<bool>(probe);
#endif
}

#else

bool sameFile(const std::string& a, const std::string& b) {
    struct stat sa, sb;
    if (stat(a.c_str(), &sa) != 0 || stat(b.c_str(), &sb) != 0) return a == b;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

#endif

core::Error writeFileAtomic(const std::string& path, const std::string& bytes,
                            const AtomicWriteOptions& options,
                            AtomicWriteResult* result) {
    AtomicWriteResult local;
    // R3: never replace the file the content came from.
    if (!options.sourcePath.empty() && sameFile(path, options.sourcePath))
        return core::Error(core::ErrorCode::Locked,
                           "refusing to overwrite the source document: " + path);

    const std::string directory = pathDirectory(path);
    const std::string name = pathFileName(path);
    // The temporary lives in the SAME directory, so the final rename is a
    // same-volume operation and cannot fail with a cross-device error.
    const std::string temporary = pathJoin(directory, "." + name + ".r62tmp");
    local.replacedExisting = fileExists(path);

    {
        std::string error;
        if (!writeWide(temporary, bytes, &error)) {
            removeWide(temporary);
            local.tempRemoved = true;
            return core::Error(core::ErrorCode::IoError,
                               error + " (previous file left intact: " + path + ")");
        }
    }

    if (options.verifyAfterWrite) {
        std::string readBack;
        core::Error read = readFileBytes(temporary, &readBack);
        if (!read.ok()) {
            removeWide(temporary);
            local.tempRemoved = true;
            return read;
        }
        if (readBack.size() != bytes.size() ||
            (bytes.size() && std::memcmp(readBack.data(), bytes.data(), bytes.size()) != 0)) {
            removeWide(temporary);
            local.tempRemoved = true;
            return core::Error(core::ErrorCode::IoError,
                               "post-write verification failed, previous file left intact");
        }
        local.verified = true;
    }

    if (options.verifyParses) {
        core::Document scratch;
        core::LoadReport loaded;
        core::Error parsed = core::loadDocument(scratch, temporary, &loaded);
        if (!parsed.ok()) {
            removeWide(temporary);
            local.tempRemoved = true;
            return core::Error(parsed.code,
                               "temporary file is not a loadable document: " +
                               parsed.message);
        }
    }

    // The rename is the LAST step. Everything above can fail without harming
    // the destination.
#ifdef _WIN32
    bool replaced = false;
    if (!moveIntoPlace(toWide(temporary), toWide(path), &replaced)) {
        removeWide(temporary);
        local.tempRemoved = true;
        return core::Error(core::ErrorCode::IoError,
                           "cannot replace destination, previous file left intact: " + path);
    }
#else
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        removeWide(temporary);
        local.tempRemoved = true;
        return core::Error(core::ErrorCode::IoError,
                           "cannot replace destination, previous file left intact: " + path);
    }
#endif

    local.bytesWritten = bytes.size();
    if (result) *result = local;
    return core::Error::success();
}

} // namespace persistence
} // namespace r62
