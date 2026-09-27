#include <Core/GName.h>
#include <array>
#include <map>
#include <stdexcept>

FGameData GameData;
FOffset Offset;

static void Check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

struct Image
{
    uint64_t Base = 0x140000000;
    uint64_t Table = Base + 0x2020;
    uint64_t FailAddress = 0;
    size_t MaxReadSize = 0;
    uint64_t TransientAddress = 0;
    unsigned TransientFailures = 0;
    unsigned FailedPageReads = 0;
    bool StaleTranslations = false;
    unsigned TranslationRefreshes = 0;
    std::map<uint64_t, std::vector<uint8_t>> Regions;
    std::vector<std::array<uint32_t, 2>> Headers;
    size_t HeaderReads = 0;
};

class SnapshotBackend : public IBackend
{
    Image& image;
public:
    explicit SnapshotBackend(Image& value) : image(value) {}
    const char* GetName() const override { return "name-pool-snapshot"; }
    bool Attach(const std::string&) override { return true; }
    void Shutdown() override {}
    uintptr_t GetModuleBase(const std::string&) override { return image.Base; }
    size_t GetModuleSize(const std::string&) override { return 0xDF43000; }
    bool RefreshTranslationCache() override
    {
        ++image.TranslationRefreshes;
        image.StaleTranslations = false;
        return true;
    }
    bool ReadRaw(uintptr_t address, void* buffer, size_t size) override
    {
        if (image.MaxReadSize && size > image.MaxReadSize) return false;
        if (image.StaleTranslations && address >= 0x500000000ULL) return false;
        if (image.FailAddress >= address && image.FailAddress - address < size) {
            if (size <= 4096) ++image.FailedPageReads;
            return false;
        }
        if (image.TransientFailures && size <= 4096 &&
            image.TransientAddress >= address && image.TransientAddress - address < size) {
            --image.TransientFailures;
            ++image.FailedPageReads;
            return false;
        }
        if (address == image.Table - 8 && size == 8 && !image.Headers.empty()) {
            const auto& header = image.Headers[(std::min)(image.HeaderReads++, image.Headers.size() - 1)];
            std::memcpy(buffer, header.data(), 8);
            return true;
        }
        auto it = image.Regions.upper_bound(address);
        if (it == image.Regions.begin()) return false;
        --it;
        const auto offset = address - it->first;
        if (offset > it->second.size() || size > it->second.size() - offset) return false;
        std::memcpy(buffer, it->second.data() + offset, size);
        return true;
    }
};

static constexpr uint64_t BlockBase = 0x500000000;
static constexpr size_t BlockSize = 0x80000;

static void Put(std::vector<uint8_t>& bytes, size_t offset, const void* value, size_t size)
{
    Check(offset <= bytes.size() && size <= bytes.size() - offset, "fixture bounds");
    std::memcpy(bytes.data() + offset, value, size);
}

template<typename T>
static void Put(std::vector<uint8_t>& bytes, size_t offset, T value)
{
    Put(bytes, offset, &value, sizeof(value));
}

static void Entry(std::vector<uint8_t>& bytes, size_t offset, uint16_t length, uint8_t kind,
                  const std::string& text, bool wide = false)
{
    // Explicit fixture offsets and allocations keep the oracle independent of the walker.
    Put<uint64_t>(bytes, offset, 0x12345678);
    Put<uint16_t>(bytes, offset + 8, static_cast<uint16_t>((length << 6) | (wide ? 1 : 0)));
    bytes[offset + 10] = kind;
    Put(bytes, offset + 12, text.data(), text.size());
}

static Image Pool(uint32_t last, uint32_t cursor)
{
    Offset = FOffset{};
    Offset.GNames = 0x2000;
    Image image;
    auto& pool = image.Regions[image.Base + 0x2000];
    pool.resize(0x1000);
    Put<uint32_t>(pool, 0x18, last);
    Put<uint32_t>(pool, 0x1C, cursor);
    for (uint32_t i = 0; i <= last; ++i) {
        const uint64_t address = BlockBase + uint64_t(i) * 0x100000;
        Put(pool, 0x20 + i * 8, address);
        image.Regions[address].resize(BlockSize);
    }
    return image;
}

static void Attach(Image& image)
{
    Memory::RegisterBackend("snapshot", [&image] { return new SnapshotBackend(image); });
    Check(mem.Init("fixture.exe", "snapshot"), "attach fixture");
    GameData.Global.Base = image.Base;
}

using Names = std::map<uint32_t, std::string>;
static bool ReadNames(Names& names, GName::DumpStats& stats, std::string& error)
{
    names.clear();
    return GName::Enumerate([&](uint32_t id, const std::string& name) {
        return names.emplace(id, name).second;
    }, stats, error);
}

static std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

static void SyntheticTests()
{
    Image image = Pool(0, 1232);
    auto& bytes = image.Regions[BlockBase];
    Entry(bytes, 0, 4, 0, "None");
    Entry(bytes, 16, 6, 0, "Object");
    Entry(bytes, 40, 10, 1, "Expressioncpbmpnogjhbj"); // Allocates 40, displays 10.
    Entry(bytes, 80, 30, 2, "_aaabolbhccnfnhlfobhdf"); // Allocates 48, displays 22.
    Entry(bytes, 128, 0, 2, "_aaabolbhccnfnhlfobhdf"); // Fixed name with zero header length.
    const std::u16string unicode = u"\u6D4B\u8BD5\U0001F600";
    Entry(bytes, 168, 4, 0, std::string(reinterpret_cast<const char*>(unicode.data()), 8), true);
    const std::u16string longWide(512, u'\u6D4B');
    Entry(bytes, 192, 512, 0, std::string(reinterpret_cast<const char*>(longWide.data()), 1024), true);
    Entry(bytes, 1232, 6, 0, "Poison"); // Outside the captured cursor.
    Attach(image);
    Names names;
    GName::DumpStats stats;
    std::string error;
    Check(ReadNames(names, stats, error), error.c_str());
    Check(names.size() == 7 && stats.HighestID == 24 && stats.Cursor == 1232, "entry boundaries/cursor");
    Check(names.at(5) == "Expression" && names.at(10) == "_aaabolbhccnfnhlfobhdf" && names.at(16) == names.at(10), "modified allocation lengths");
    Check(names.at(21) == "\xE6\xB5\x8B\xE8\xAF\x95\xF0\x9F\x98\x80" && names.at(24).size() == 1536, "UTF-16 to UTF-8 including expansion and surrogate pairs");
    for (const auto& [id, name] : names) Check(GName::ResolveName(id) == name, "direct lookup matches enumeration");
    Check(GName::ResolveName(1ULL << 40) == "FAIL", "out-of-range ID rejected before narrowing");

    // The AOB and manual anchor pairs must enumerate the identical pool.
    Offset.GNames += 0x10;
    Offset.NamePoolBlocksOffset = 0x10;
    Names aobNames;
    Check(ReadNames(aobNames, stats, error) && aobNames == names, "equivalent AOB anchor");
    Offset.GNames -= 0x10;
    Offset.NamePoolBlocksOffset = 0x20;

    GameData.Directory = std::filesystem::current_path() / "synthetic-namepool";
    std::filesystem::create_directories(GameData.Directory);
    Check(GName::Init(), "synthetic NamesDump output");
    const auto original = ReadFile(GameData.Directory / "NamesDump.txt");
    Check(original.find("[000000005] Expression\n") != std::string::npos, "real dump format");
    image.FailAddress = BlockBase + 100;
    Check(!GName::Init() && GameData.GName.FNameTables.empty(), "failed block aborts and clears partial index");
    Check(ReadFile(GameData.Directory / "NamesDump.txt") == original, "failed dump preserves previous complete file");
    image.FailAddress = 0;

    auto& header = image.Regions[image.Base + 0x2000];
    for (uint32_t cursor : { 1224u, 1240u, 0x80008u, 17u, 0u }) {
        Put(header, 0x1C, cursor);
        Check(!ReadNames(aobNames, stats, error), "invalid or partial final cursor must fail");
    }
    Put<uint32_t>(header, 0x1C, 1232);
    Put<uint32_t>(header, 0x18, 4096);
    Check(!ReadNames(aobNames, stats, error), "invalid current block");
    Put<uint32_t>(header, 0x18, 0);
    Put<uint64_t>(header, 0x20, 0);
    Check(!ReadNames(aobNames, stats, error), "null active block");
    Put<uint64_t>(header, 0x20, BlockBase);
    bytes[12] = 'B';
    Check(!ReadNames(aobNames, stats, error), "None anchor required");
    bytes[12] = 'N';
    bytes[26] = 7;
    Check(!ReadNames(aobNames, stats, error), "unknown entry kind rejected");
    bytes[26] = 0;
    Put<uint16_t>(bytes, 180, 0xD800);
    Put<uint16_t>(bytes, 182, 'A');
    Check(!ReadNames(aobNames, stats, error), "invalid UTF-16 rejected");
    Offset.ChunkMask = 64;
    Check(!ReadNames(aobNames, stats, error) && GName::ResolveName(0) == "FAIL", "invalid shift guarded");

    // Reach IDs beyond 500000 with deterministic, densely allocated completed blocks.
    image = Pool(8, 24);
    size_t expected = 1;
    const std::string filler(1000, 'x');
    for (uint32_t b = 0; b < 8; ++b) {
        auto& data = image.Regions[BlockBase + uint64_t(b) * 0x100000];
        size_t offset = 0;
        if (!b) { Entry(data, 0, 4, 0, "None"); offset = 16; ++expected; }
        while (BlockSize - offset > 2048) {
            Entry(data, offset, 1000, 0, filler);
            offset += 1016;
            ++expected;
        }
        // Real block 39 ends with a length-zero header containing hash bits
        // and a stale kind byte, not an entirely zero prefix.
        Put<uint16_t>(data, offset + 8, 0x24);
        data[offset + 10] = 144;
    }
    Entry(image.Regions[BlockBase + 8 * 0x100000], 0, 7, 0, "Kavkazi");
    Attach(image);
    Check(ReadNames(names, stats, error), error.c_str());
    Check(stats.Names == expected && stats.HighestID == 524288 && names.at(524288) == "Kavkazi", "all blocks and high name IDs");
    const auto expectedNames = names;
    image.MaxReadSize = 4096;
    image.TransientAddress = BlockBase + 0x1234;
    image.TransientFailures = 2;
    image.FailedPageReads = 0;
    Check(ReadNames(names, stats, error) && names == expectedNames && image.TransientFailures == 0 &&
        image.FailedPageReads == 2, "bulk failures and transient page failures recover exact names");
    std::vector<uint8_t> unaligned(5000);
    Check(GName::ReadPoolBlock(0, BlockBase + 0xFF8, unaligned.data(), unaligned.size(), error, false) &&
        std::memcmp(unaligned.data(), image.Regions[BlockBase].data() + 0xFF8, unaligned.size()) == 0,
        "page fallback preserves unaligned endpoints");
    image.MaxReadSize = 0;
    const auto refreshCount = image.TranslationRefreshes;
    image.StaleTranslations = true;
    Check(ReadNames(names, stats, error) && names == expectedNames && image.TranslationRefreshes > refreshCount,
        "stale mappings recover through translation refresh");
    Check(!GName::Enumerate([](uint32_t, const std::string&) { return false; }, stats, error), "output failure propagates");
    image.FailAddress = BlockBase + 0x10000;
    image.FailedPageReads = 0;
    Check(!ReadNames(names, stats, error), "failed middle read cannot masquerade as zero terminator");
    Check(image.FailedPageReads == 3 && error.find("page 0x500010000") != std::string::npos &&
        error.find("block +0x10000") != std::string::npos, "persistent failure has bounded retries and exact page diagnostics");
    image.FailAddress = 0;
    image.Headers = {{7, 16}, {8, 24}, {8, 24}, {8, 48}};
    image.HeaderReads = 0;
    Check(ReadNames(names, stats, error) && stats.Cursor == 24 && image.HeaderReads == 4, "block rollover retry and fixed prefix cursor");
    image.Headers = {{7, 16}, {8, 24}, {7, 16}, {8, 24}, {7, 16}, {8, 24}};
    image.HeaderReads = 0;
    Check(!ReadNames(names, stats, error) && image.HeaderReads == 6, "unstable allocator retries are bounded");
    mem.Shutdown();
    std::puts("PASS: name-pool traversal, allocation lengths, Unicode, high IDs, rollover and failure handling.");
}

template<typename T>
static T ReadValue(std::ifstream& input)
{
    T value{};
    input.read(reinterpret_cast<char*>(&value), sizeof(value));
    Check(bool(input), "truncated live snapshot");
    return value;
}

static void Replay(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    char magic[8]{};
    input.read(magic, 8);
    Check(std::memcmp(magic, "FNPOOL01", 8) == 0, "snapshot magic");
    Offset = FOffset{};
    Image image;
    image.Base = ReadValue<uint64_t>(input);
    Offset.GNames = ReadValue<uint64_t>(input);
    Offset.NamePoolBlocksOffset = ReadValue<uint32_t>(input);
    const auto last = ReadValue<uint32_t>(input);
    const auto cursor = ReadValue<uint32_t>(input);
    Check(last < 4096 && cursor <= BlockSize, "snapshot allocator bounds");
    image.Table = image.Base + Offset.GNames + Offset.NamePoolBlocksOffset;
    const uint64_t headerPage = (image.Table - 8) & ~0xFFFULL;
    auto& header = image.Regions[headerPage];
    header.resize(((image.Table - headerPage + (last + 1) * 8) + 4095) & ~4095ULL);
    Put(header, image.Table - headerPage - 8, last);
    Put(header, image.Table - headerPage - 4, cursor);
    for (uint32_t b = 0; b <= last; ++b) {
        const auto address = ReadValue<uint64_t>(input);
        const auto size = ReadValue<uint32_t>(input);
        Check(size == (b == last ? cursor : BlockSize), "snapshot block length");
        Put(header, image.Table - headerPage + b * 8, address);
        auto& block = image.Regions[address];
        block.resize(BlockSize);
        input.read(reinterpret_cast<char*>(block.data()), size);
        Check(bool(input), "truncated snapshot block");
    }
    Attach(image);
    GameData.Directory = std::filesystem::current_path() / "live-namepool-replay";
    std::filesystem::create_directories(GameData.Directory);
    Check(GName::Init(), "live snapshot dump");
    const auto& names = GameData.GName.FNameTables;
    for (const auto& [id, value] : std::map<int, std::string>{{0, "None"}, {268, "Object"}, {760, "/Script/CoreUObject"}, {740198, "Kavkazi"}}) {
        Check(names.at(id) == value && GName::ResolveName(id) == value, "live anchor/direct lookup");
    }
    Check(names.size() > 400000, "live full-pool coverage");
    size_t lines = 0;
    uint32_t highest = 0;
    std::ifstream dump(GameData.Directory / "NamesDump.txt");
    for (std::string line; std::getline(dump, line);) {
        Check(line.size() >= 13 && line[0] == '[' && line[10] == ']' && line[11] == ' ', "output line format");
        const auto id = static_cast<uint32_t>(std::stoul(line.substr(1, 9)));
        Check((!lines || id > highest) && names.at(id) == line.substr(12), "ordered output with exact IDs/text");
        highest = id;
        ++lines;
    }
    Check(lines == names.size() && (highest >> 16) == last, "output count and last block coverage");
    if (last == 46 && cursor == 73360) Check(lines == 426542 && highest == 3023822, "2026-09-20 snapshot reference count");
    std::printf("PASS: live snapshot replay wrote %zu names, highest ID %u, %u blocks.\n", lines, highest, last + 1);
    mem.Shutdown();
}

int main(int argc, char** argv)
{
    try {
        SyntheticTests();
        if (argc > 1) Replay(argv[1]);
        else std::puts("SKIP: live snapshot replay (pass live-namepool.bin to enable).");
        return 0;
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "FAIL: %s\n", exception.what());
        return 1;
    }
}
