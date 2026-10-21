#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace OpenYAMM::Engine
{
// One content pack of a release (tools/runtime_packs.py): a zip holding part of a package, mounted at a virtual folder.
struct AssetPackRecord
{
    std::string id;
    // "engine" or a world id.
    std::string package;
    // "/engine", "/worlds/<id>" or a folder below one of them.
    std::string mount;
    std::string file;
    uint64_t size = 0;
    std::string contentId;
    std::string sha256;
};

// The installed content: assets/index.json, written by the packager and the updater.
struct AssetPackIndex
{
    std::string release;
    std::string build;
    std::string profile;
    uint32_t contentFormat = 0;
    // Folder of the pack files, relative to the index file.
    std::string store;
    std::vector<AssetPackRecord> packs;
};

inline constexpr const char *AssetPackIndexFileName = "index.json";
// The content format this binary reads; tools/package_runtime_assets.py CONTENT_FORMAT must match. Bump both when
// the binary needs new content or the content needs a new binary.
inline constexpr uint32_t SupportedContentFormat = 1;

// Parses and validates an index: known schema, unique ids and files, mounts inside their package. Files are not
// checked; hashing every pack on each start would be too slow, so hashes are checked on download and on request.
std::optional<AssetPackIndex> parseAssetPackIndex(const std::string &text, std::string &error);
// A pack mounts inside its package and has a plain .zip file name, so it can neither escape its store nor shadow
// another package.
bool validateAssetPackRecord(const AssetPackRecord &pack, std::string &error);
std::optional<AssetPackIndex> loadAssetPackIndex(const std::filesystem::path &path, std::string &error);
// Every pack file exists in the store with the size the index records.
bool validateAssetPackFiles(const AssetPackIndex &index, const std::filesystem::path &storeRoot, std::string &error);
}
