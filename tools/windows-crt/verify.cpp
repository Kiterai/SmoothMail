// Build-time verifier only; never included in the product ZIP.
#include <windows.h>
#include <wintrust.h>
#include <softpub.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <array>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <cstddef>

namespace {
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
std::vector<unsigned char> read(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    require(bool(file), "Cannot read CRT input");
    const auto size = file.tellg();
    require(size > 0 && size < 32 * 1024 * 1024, "Invalid CRT input size");
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size));
    require(bool(file), "Incomplete CRT input");
    return bytes;
}
void x64(const std::vector<unsigned char> &bytes)
{
    require(bytes.size() >= sizeof(IMAGE_DOS_HEADER), "Invalid CRT PE");
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, bytes.data(), sizeof(dos));
    require(dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew >= 0
                && size_t(dos.e_lfanew) <= bytes.size()
                && sizeof(IMAGE_NT_HEADERS64) <= bytes.size() - size_t(dos.e_lfanew), "Invalid CRT PE");
    IMAGE_NT_HEADERS64 pe{};
    std::memcpy(&pe, bytes.data() + dos.e_lfanew, sizeof(pe));
    require(pe.Signature == IMAGE_NT_SIGNATURE && pe.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64
                && pe.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC, "CRT must be x64 PE32+");
}
std::array<DWORD, 2> version(const wchar_t *path)
{
    DWORD unused = 0;
    const auto length = GetFileVersionInfoSizeW(path, &unused);
    require(length > 0, "CRT version resource missing");
    std::vector<unsigned char> data(length);
    require(GetFileVersionInfoW(path, 0, length, data.data()), "Cannot read CRT version");
    VS_FIXEDFILEINFO *info = nullptr;
    UINT size = 0;
    require(VerQueryValueW(data.data(), L"\\", reinterpret_cast<void **>(&info), &size)
                && size >= sizeof(*info) && info->dwSignature == 0xfeef04bd, "Invalid CRT version");
    require(HIWORD(info->dwFileVersionMS) == 14, "Expected MSVC v14 runtime");
    return {info->dwFileVersionMS, info->dwFileVersionLS};
}
void microsoftSignature(const wchar_t *path)
{
    WINTRUST_FILE_INFO file{sizeof(file)};
    file.pcwszFilePath = path;
    WINTRUST_DATA trust{sizeof(trust)};
    trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &file;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    // No dialogs or network traffic. This verifies the local trust chain and
    // embedded signature, not online revocation freshness.
    trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_REVOCATION_CHECK_NONE;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const auto status = WinVerifyTrust(nullptr, &action, &trust);
    bool microsoft = false;
    if (status == ERROR_SUCCESS) {
        auto *provider = WTHelperProvDataFromStateData(trust.hWVTStateData);
        auto *signer = provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
        if (signer && signer->csCertChain > 0) {
            wchar_t name[256]{};
            wchar_t organization[256]{};
            CertGetNameStringW(signer->pasCertChain[0].pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 256);
            CertGetNameStringW(signer->pasCertChain[0].pCert, CERT_NAME_ATTR_TYPE, 0,
                const_cast<char *>(szOID_ORGANIZATION_NAME), organization, 256);
            const std::wstring publisher(name);
            microsoft = std::wstring(organization) == L"Microsoft Corporation"
                && (publisher == L"Microsoft Corporation" || publisher == L"Microsoft Windows"
                    || publisher == L"Microsoft Windows Software Compatibility Publisher");
        }
    }
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &trust);
    require(status == ERROR_SUCCESS && microsoft, "CRT Microsoft Authenticode verification failed");
}
void verify(const wchar_t *sourcePath, const wchar_t *stagedPath, const wchar_t *baseline)
{
    const auto source = read(sourcePath);
    const auto staged = read(stagedPath);
    x64(source); x64(staged);
    require(source == staged, "Staged CRT differs from redistributable input");
    const auto expected = version(baseline);
    const auto actual = version(sourcePath);
    require(actual == expected && version(stagedPath) == expected, "Mixed CRT file versions");
    microsoftSignature(sourcePath);
    microsoftSignature(stagedPath);
    std::array<unsigned char, 32> hash{};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0, "SHA256 unavailable");
    const auto result = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(source.data()),
        static_cast<ULONG>(source.size()), hash.data(), static_cast<ULONG>(hash.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    require(result == 0, "CRT SHA256 failed");
    require(source == read(sourcePath) && staged == read(stagedPath), "CRT input changed during verification");
    std::cout << std::dec << "version=" << HIWORD(actual[0]) << '.' << LOWORD(actual[0]) << '.'
              << HIWORD(actual[1]) << '.' << LOWORD(actual[1]) << " sha256=";
    for (auto value : hash) std::cout << std::hex << std::setw(2) << std::setfill('0') << int(value);
    std::cout << '\n';
}
template<typename Operation> void rejects(Operation operation, const char *expected)
{
    try { operation(); }
    catch (const std::runtime_error &error) {
        require(std::string(error.what()) == expected, "Verifier rejected fixture for an unexpected reason");
        return;
    }
    throw std::runtime_error("CRT verifier accepted an unsafe fixture");
}
void selfTest(const wchar_t *baseline, const wchar_t *directory)
{
    const std::filesystem::path root(directory);
    require(std::filesystem::create_directory(root), "Choose a new verifier fixture directory");
    auto bytes = read(baseline);
    const auto write = [&](const wchar_t *name, const std::vector<unsigned char> &data) {
        const auto path = root / name;
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
        file.close();
        require(bool(file), "Cannot write verifier fixture");
        return path;
    };
    const auto good = write(L"good.dll", bytes);
    verify(baseline, good.c_str(), baseline);
    auto changed = bytes;
    changed.back() ^= 1;
    const auto modified = write(L"modified.dll", changed);
    rejects([&] { verify(baseline, modified.c_str(), baseline); }, "Staged CRT differs from redistributable input");
    rejects([&] { x64(std::vector<unsigned char>(64)); }, "Invalid CRT PE");
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, bytes.data(), sizeof(dos));
    changed = bytes;
    const WORD x86 = IMAGE_FILE_MACHINE_I386;
    std::memcpy(changed.data() + dos.e_lfanew + sizeof(DWORD), &x86, sizeof(x86));
    rejects([&] { x64(changed); }, "CRT must be x64 PE32+");
    changed = bytes;
    const auto certificateOffset = size_t(dos.e_lfanew) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader)
        + offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory) + IMAGE_DIRECTORY_ENTRY_SECURITY * sizeof(IMAGE_DATA_DIRECTORY);
    std::fill_n(changed.begin() + certificateOffset, sizeof(IMAGE_DATA_DIRECTORY), 0);
    const auto unsignedFile = write(L"unsigned.dll", changed);
    rejects([&] { verify(unsignedFile.c_str(), unsignedFile.c_str(), baseline); },
        "CRT Microsoft Authenticode verification failed");
    const auto expected = version(baseline);
    const std::array<DWORD, 4> pattern{0xfeef04bd, 0x00010000, expected[0], expected[1]};
    const auto *start = reinterpret_cast<const unsigned char *>(pattern.data());
    changed = bytes;
    const auto where = std::search(changed.begin(), changed.end(), start, start + sizeof(pattern));
    require(where != changed.end(), "Cannot locate synthetic version fixture");
    const DWORD different = expected[1] + 1;
    std::memcpy(&*where + 3 * sizeof(DWORD), &different, sizeof(different));
    const auto mixed = write(L"mixed.dll", changed);
    rejects([&] { verify(mixed.c_str(), mixed.c_str(), baseline); }, "Mixed CRT file versions");
    std::cout << "CRT verifier copy, malformed/x86 PE, unsigned and mixed-version guards passed\n";
}
}
int wmain(int argc, wchar_t **argv)
{
    try {
        require(argc == 4, "Specify source/copy/baseline or --self-test baseline new-fixture-directory");
        if (std::wstring(argv[1]) == L"--self-test")
            selfTest(argv[2], argv[3]);
        else
            verify(argv[1], argv[2], argv[3]);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "CRT verification failed: " << error.what() << '\n';
        return 1;
    }
}
