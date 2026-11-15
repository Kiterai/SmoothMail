// Build-time helper only. Never shipped in the application ZIP.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtEndian>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

QByteArray read(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read package input");
    const auto data = file.readAll();
    require(file.error() == QFileDevice::NoError, "Cannot read complete package input");
    return data;
}

void write(const QString &path, const QByteArray &data)
{
    require(QDir().mkpath(QFileInfo(path).absolutePath()), "Cannot create package directory");
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Refusing to replace package input");
    require(file.write(data) == data.size(), "Cannot write package input");
    require(file.flush(), "Cannot flush package input");
}

QString utf8(const QByteArray &data)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder(data);
    require(!decoder.hasError(), "Legal input must be UTF-8 text");
    require(!text.contains(QChar(0)), "Legal input must be text");
    return text;
}

QString relative(const QJsonValue &value)
{
    require(value.isString(), "Legal paths must be strings");
    const auto name = value.toString();
    require(!name.isEmpty() && !name.startsWith('/') && !name.contains('\\')
                && !name.contains(':') && !name.contains(QChar(0)),
            "Legal paths must be safe relative paths");
    for (const auto &part : name.split('/'))
        require(!part.isEmpty() && part != "." && part != "..", "Unsafe legal path component");
    return name;
}

void safeFile(const QString &root, const QString &name)
{
    require(!QFileInfo(root).isSymLink() && !QFileInfo(root).isJunction(), "Linked legal root");
    const auto base = QFileInfo(root).canonicalFilePath();
    const QFileInfo file(root + "/" + name);
    require(!base.isEmpty() && file.isFile() && !file.isSymLink() && !file.isJunction(),
            "Missing or unsafe legal input");
    require(file.canonicalFilePath().startsWith(base + "/", Qt::CaseInsensitive),
            "Legal input escapes its directory");
    auto parent = file.dir();
    while (parent.absolutePath().compare(QDir(root).absolutePath(), Qt::CaseInsensitive) != 0) {
        const QFileInfo info(parent.absolutePath());
        require(!info.isSymLink() && !info.isJunction(), "Linked legal directory");
        require(parent.cdUp(), "Unsafe legal directory");
    }
}

struct Legal {
    QJsonObject manifest;
    QSet<QString> files;
    QStringList patterns;
};

Legal legalManifest(const QString &directory, bool review = false)
{
    const auto manifestName = review ? "/manifest.review.json" : "/manifest.json";
    safeFile(directory, review ? "manifest.review.json" : "manifest.json");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(utf8(read(directory + manifestName)).toUtf8(), &error);
    require(error.error == QJsonParseError::NoError && document.isObject(), "Invalid legal JSON");
    Legal legal{document.object(), {}, {}};
    const auto keys = legal.manifest.keys();
    require(keys.size() == (review ? 3 : 2) && keys.contains("schema") && keys.contains("components")
                && (!review || (legal.manifest["review_only"].isBool() && legal.manifest["review_only"].toBool()))
                && legal.manifest["schema"].isDouble() && legal.manifest["schema"].toDouble() == 1
                && legal.manifest["components"].isArray()
                && !legal.manifest["components"].toArray().isEmpty(),
            "Legal manifest schema/components missing");
    const QSet<QString> allowed{"name", "version", "binaries", "license_files", "source_notice"};
    for (const auto &value : legal.manifest["components"].toArray()) {
        require(value.isObject(), "Invalid legal component");
        const auto component = value.toObject();
        for (auto it = component.begin(); it != component.end(); ++it)
            require(allowed.contains(it.key()), "Unexpected legal component field");
        require(component["name"].isString() && !component["name"].toString().isEmpty()
                    && (!component.contains("version") || component["version"].isString())
                    && component["binaries"].isArray() && !component["binaries"].toArray().isEmpty()
                    && component["license_files"].isArray() && !component["license_files"].toArray().isEmpty(),
                "Incomplete legal component");
        for (const auto &pattern : component["binaries"].toArray())
            legal.patterns.append(relative(pattern));
        auto names = component["license_files"].toArray();
        names.append(component["source_notice"]);
        for (const auto &item : names) {
            const auto name = relative(item);
            safeFile(directory, name);
            const auto data = read(directory + "/" + name);
            require(!data.isEmpty(), "Empty legal input");
            utf8(data);
            legal.files.insert(name);
        }
    }
    return legal;
}

QStringList entries(const QString &directory)
{
    require(QFileInfo(directory).isDir() && !QFileInfo(directory).isSymLink()
                && !QFileInfo(directory).isJunction(), "Unsafe stage directory");
    QStringList files;
    QSet<QString> folded;
    QDirIterator iterator(directory, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const auto info = iterator.fileInfo();
        require(!info.isSymLink() && !info.isJunction(), "Links are forbidden in package inputs");
        if (info.isFile()) {
            const auto name = QDir(directory).relativeFilePath(info.absoluteFilePath());
            relative(name);
            for (auto ch : name)
                require(ch.unicode() >= 32 && ch.unicode() != 127, "Unsafe ZIP filename");
            require(!folded.contains(name.toCaseFolded()), "Case-colliding ZIP filenames");
            folded.insert(name.toCaseFolded());
            files.append(name);
        }
        else
            require(info.isDir(), "Unsupported package entry");
    }
    std::sort(files.begin(), files.end());
    return files;
}

void prepareLegal(const QString &stage, const QString &directory, const QString &repository)
{
    const auto legal = legalManifest(directory);
    for (const auto &name : entries(stage)) {
        if (!name.endsWith(".dll", Qt::CaseInsensitive))
            continue;
        bool covered = false;
        for (const auto &pattern : legal.patterns) {
            const QRegularExpression expression(
                QRegularExpression::wildcardToRegularExpression(pattern,
                    QRegularExpression::UnanchoredWildcardConversion
                        | QRegularExpression::NonPathWildcardConversion),
                QRegularExpression::CaseInsensitiveOption);
            // Match the entire relative filename, as fnmatchcase did.
            const QRegularExpression anchored(QRegularExpression::anchoredPattern(expression.pattern()),
                                              QRegularExpression::CaseInsensitiveOption);
            covered |= anchored.match(name).hasMatch();
        }
        require(covered, "Missing license/source coverage for a DLL");
    }
    require(!QFileInfo::exists(stage + "/licenses") && !QFileInfo::exists(stage + "/legal"),
            "Stage already contains license inputs");
    auto names = legal.files.values();
    std::sort(names.begin(), names.end());
    for (const auto &name : names)
        write(stage + "/licenses/" + name, read(directory + "/" + name));
    write(stage + "/licenses/manifest.json", QJsonDocument(legal.manifest).toJson());
    // Recipient notices come from the generated manifest, not the source-tree
    // development history in THIRD_PARTY_NOTICES.md.
    write(stage + "/LICENSE", read(repository + "/LICENSE"));
}

using PathPolicy = QMap<QString, QJsonObject>;

bool sha256Text(const QJsonValue &value)
{
    return value.isString() && QRegularExpression("^[a-f0-9]{64}$").match(value.toString()).hasMatch();
}

PathPolicy pathPolicy(const QString &path)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(read(path), &error);
    require(error.error == QJsonParseError::NoError && document.isObject(), "Invalid path policy JSON");
    const auto object = document.object();
    require(object.keys().size() == 2 && object["schema"].toInt() == 1
                && object["files"].isArray(), "Invalid path policy schema");
    PathPolicy policy;
    QSet<QString> folded;
    for (const auto &value : object["files"].toArray()) {
        require(value.isObject(), "Invalid path policy entry");
        const auto entry = value.toObject();
        const auto name = relative(entry["file"]);
        require(entry.keys().size() == 4 && sha256Text(entry["sha256"])
                    && entry["origin"].isString() && !entry["origin"].toString().isEmpty()
                    && entry["ranges"].isArray() && !entry["ranges"].toArray().isEmpty()
                    && name.endsWith(".dll", Qt::CaseInsensitive)
                    && !folded.contains(name.toCaseFolded()), "Invalid or duplicate path policy entry");
        folded.insert(name.toCaseFolded());
        qsizetype previousEnd = 0;
        for (const auto &item : entry["ranges"].toArray()) {
            require(item.isObject(), "Invalid path policy range");
            const auto range = item.toObject();
            const auto offset = range["offset"].toInteger(-1);
            const auto length = range["length"].toInteger(-1);
            const QSet<QString> reasons{"supplier-pdb", "supplier-source", "upstream-default"};
            require(range.keys().size() == 4 && offset >= previousEnd && length > 0 && length <= 4096
                        && offset <= 0x7fffffff - length && sha256Text(range["sha256"])
                        && reasons.contains(range["reason"].toString()), "Invalid path policy range");
            previousEnd = offset + length;
        }
        policy.insert(name, entry);
    }
    return policy;
}

QStringList decodedTexts(const QByteArray &data)
{
    QStringList texts{QString::fromLatin1(data), QString::fromUtf8(data)};
    for (int alignment : {0, 1}) {
        QString text;
        text.reserve(data.size() / 2);
        for (qsizetype i = alignment; i + 1 < data.size(); i += 2)
            text.append(QChar(qFromLittleEndian<quint16>(data.constData() + i)));
        texts.append(text);
    }
    return texts;
}

void audit(const QByteArray &data, const QString &name, const QStringList &roots,
           const PathPolicy &policy = {})
{
    const QFileInfo info(name);
    const auto suffix = info.suffix().toLower();
    static const QSet<QString> forbidden{"pdb", "ilk", "sqlite", "db", "log", "ini"};
    static const QRegularExpression debug(
        R"(^(?:qt6.*d|ucrtbased|msvcp.*d(?:_.*)?|vcruntime.*d|concrt.*d)\.dll$)",
        QRegularExpression::CaseInsensitiveOption);
    require(!forbidden.contains(suffix) && !debug.match(info.fileName()).hasMatch(),
            "Debug or user data in distribution");
    if (suffix == "dll" || suffix == "exe") {
        require(data.size() >= 64 && data.first(2) == "MZ", "Invalid PE binary");
        const quint64 offset = qFromLittleEndian<quint32>(data.constData() + 60);
        require(offset + 26 <= quint64(data.size())
                    && data.mid(offset, 4) == QByteArray("PE\0\0", 4)
                    && qFromLittleEndian<quint16>(data.constData() + offset + 4) == 0x8664
                    && qFromLittleEndian<quint16>(data.constData() + offset + 24) == 0x20b,
                "Distribution requires x64 PE32+");
    }
    static const QRegularExpression privatePath(
        R"((?:[a-z]:[\\/](?![\\/])(?:[a-z0-9_. ()~$@+\-]{2,100}[\\/])+[a-z0-9_. ()~$@+\-]{3,100}|[a-z]:[\\/][a-z0-9_. ()~$@+\-]{3,100}\.(?:pdb|cpp|h|obj|lib|dll|exe)|/(?:home|Users)/[^/\x00-\x20]+/|\\\\[a-z0-9_.-]{2,}\\[a-z0-9$_.-]{2,}))",
        QRegularExpression::CaseInsensitiveOption);
    // Explicit builder roots/identifiers are NEVER exempted, including inside
    // approved vendor metadata. The input bytes and shipped DLL remain untouched.
    for (auto text : decodedTexts(data)) {
        text.replace('\\', '/');
        for (auto root : roots) {
            root.replace('\\', '/');
            require(root.isEmpty() || !text.contains(root, Qt::CaseInsensitive),
                    "Embedded input directory detected");
        }
    }
    auto inspected = data;
    const auto approval = policy.constFind(name);
    if (approval != policy.cend()) {
        require(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()
                    == approval->value("sha256").toString().toLatin1(),
                "Reviewed supplier binary changed; review path policy");
        for (const auto &item : approval->value("ranges").toArray()) {
            const auto range = item.toObject();
            const auto offset = range["offset"].toInteger();
            const auto length = range["length"].toInteger();
            require(offset >= 0 && length > 0 && offset <= data.size() && length <= data.size() - offset
                        && QCryptographicHash::hash(data.mid(offset, length), QCryptographicHash::Sha256).toHex()
                            == range["sha256"].toString().toLatin1(),
                    "Reviewed supplier path range changed");
            // Mask only the reviewed range in this temporary inspection copy.
            inspected.replace(offset, length, QByteArray(length, '\0'));
        }
    }
    for (const auto &text : decodedTexts(inspected))
        require(!privatePath.match(text).hasMatch(), "Embedded machine/user path detected");
}

quint32 crc32(const QByteArray &data)
{
    quint32 crc = 0xffffffffu;
    for (const auto byte : data) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void u16(QByteArray &data, quint16 value)
{
    const auto le = qToLittleEndian(value);
    data.append(reinterpret_cast<const char *>(&le), 2);
}
void u32(QByteArray &data, quint32 value)
{
    const auto le = qToLittleEndian(value);
    data.append(reinterpret_cast<const char *>(&le), 4);
}

// Deterministic ZIP32: UTF-8 names, raw DEFLATE, 1980-01-01, Unix 0644.
// Fail instead of truncating ZIP32 fields. No host path/name is recorded.
void archive(const QString &stage, const QString &output, const QStringList &roots,
             const PathPolicy &policy = {})
{
    require(!QFileInfo::exists(output) && !QFileInfo(output).isSymLink(), "Output already exists");
    require(!QFileInfo::exists(stage + "/runtime-sha256.txt"), "Stage already has an inventory");
    auto files = entries(stage);
    require(!files.isEmpty() && files.size() < 65535, "Invalid ZIP entry count");
    QByteArray inventory;
    for (const auto &name : files) {
        const auto data = read(stage + "/" + name);
        audit(data, name, roots, policy);
        inventory += QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()
            + "  " + name.toUtf8() + "\n";
    }
    write(stage + "/runtime-sha256.txt", inventory);
    files.append("runtime-sha256.txt");
    std::sort(files.begin(), files.end());
    require(QDir().mkpath(QFileInfo(output).absolutePath()), "Cannot create ZIP directory");
    QTemporaryFile temporary(QFileInfo(output).absolutePath() + "/.smoothmail-XXXXXX.zip");
    require(temporary.open(), "Cannot create temporary ZIP");
    QByteArray central;
    auto append = [&](const QByteArray &data) {
        require(temporary.write(data) == data.size(), "Cannot write ZIP");
        require(temporary.pos() < 0xffffffffLL, "ZIP32 size limit exceeded");
    };
    for (const auto &file : files) {
        const auto name = ("SmoothMail/" + file).toUtf8();
        const auto data = read(stage + "/" + file);
        // Recheck inputs read for archiving, not just the earlier inventory pass.
        audit(data, file, roots, policy);
        require(data.size() < 0xffffffffLL && name.size() < 65536, "ZIP32 size limit exceeded");
        if (file != "runtime-sha256.txt")
            require(inventory.contains(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()
                        + "  " + file.toUtf8() + "\n"), "Stage changed during packaging");
        const auto wrapped = qCompress(data, 9);
        require(data.isEmpty() || wrapped.size() >= 10, "Cannot compress ZIP entry");
        const auto compressed = data.isEmpty() ? QByteArray::fromHex("0300")
            : wrapped.mid(6, wrapped.size() - 10); // Qt length + zlib header/trailer
        const auto crc = crc32(data);
        const auto offset = quint32(temporary.pos());
        QByteArray local;
        u32(local, 0x04034b50); u16(local, 20); u16(local, 0x800); u16(local, 8);
        u16(local, 0); u16(local, 33); u32(local, crc);
        u32(local, quint32(compressed.size())); u32(local, quint32(data.size()));
        u16(local, quint16(name.size())); u16(local, 0);
        append(local + name + compressed);
        u32(central, 0x02014b50); u16(central, 0x0314); u16(central, 20);
        u16(central, 0x800); u16(central, 8); u16(central, 0); u16(central, 33);
        u32(central, crc); u32(central, quint32(compressed.size())); u32(central, quint32(data.size()));
        u16(central, quint16(name.size())); u16(central, 0); u16(central, 0);
        u16(central, 0); u16(central, 0); u32(central, 0100644u << 16); u32(central, offset);
        central += name;
    }
    const auto centralOffset = quint32(temporary.pos());
    append(central);
    QByteArray end;
    u32(end, 0x06054b50); u16(end, 0); u16(end, 0);
    u16(end, quint16(files.size())); u16(end, quint16(files.size()));
    u32(end, quint32(central.size())); u32(end, centralOffset); u16(end, 0);
    append(end);
    require(temporary.flush(), "Cannot flush ZIP");
    const auto temporaryName = temporary.fileName();
    temporary.close();
    std::error_code error;
    // Exclusive atomic publication, never replacing a concurrently created output.
    std::filesystem::create_hard_link(
        std::filesystem::path(temporaryName.toStdWString()),
        std::filesystem::path(output.toStdWString()), error);
    require(!error, "Cannot publish ZIP without replacement");
}

QByteArray fixture()
{
    QByteArray data(128, '\0');
    data.replace(0, 2, "MZ");
    qToLittleEndian<quint32>(64, data.data() + 60);
    data.replace(64, 4, QByteArray("PE\0\0", 4));
    qToLittleEndian<quint16>(0x8664, data.data() + 68);
    qToLittleEndian<quint16>(0x20b, data.data() + 88);
    return data;
}

template<typename Operation> void rejects(Operation operation)
{
    bool rejected = false;
    try { operation(); } catch (const std::runtime_error &) { rejected = true; }
    require(rejected, "Package guard failed to reject fixture");
}

void selfTest(const QString &artifact = {})
{
    for (const auto &name : {"../outside", "C:/private/file", "..\\outside", "/etc/passwd", ""})
        rejects([&] { relative(QString::fromLatin1(name)); });
    rejects([] { utf8(QByteArray("\xff\xfe\0", 3)); });
    for (const auto &name : {"Qt6Cored.dll", "qt6keychaind.dll", "ucrtbased.dll",
                            "msvcp140d_atomic_wait.dll", "app.pdb", "mail.sqlite", "session.log", "settings.ini"})
        rejects([&] { audit(fixture(), name, {}); });
    rejects([] { audit("not a PE", "app.exe", {}); });
    for (quint16 machine : {0x14c, 0xaa64}) {
        auto data = fixture();
        qToLittleEndian<quint16>(machine, data.data() + 68);
        rejects([&] { audit(data, "app.exe", {}); });
    }
    for (const QString text : {QStringLiteral("C:\\Users\\private-builder\\project\\file.cpp"),
                              QStringLiteral("/home/private-builder/project/file.cpp"),
                              QStringLiteral("\\\\private-pc\\share\\source")}) {
        QByteArray wide;
        for (auto ch : text)
            u16(wide, ch.unicode());
        for (const auto &payload : {text.toUtf8(), wide})
            for (const auto &padding : {QByteArray{}, QByteArray("x")})
                rejects([&] { audit(fixture() + padding + payload, "app.exe", {}); });
    }
    rejects([] { audit(fixture() + "private-root", "app.exe", {"private-root"}); });
    const auto unicodeRoot = QStringLiteral("C:/ビルド/入力");
    rejects([&] { audit(fixture() + unicodeRoot.toUtf8(), "app.exe", {unicodeRoot}); });
    QByteArray unicodeWide;
    for (auto ch : unicodeRoot)
        u16(unicodeWide, ch.unicode());
    rejects([&] { audit(fixture() + "x" + unicodeWide, "app.exe", {unicodeRoot}); });
    // Supplier metadata is allowed only for the reviewed bytes/ranges. A name,
    // signature, or even a reviewed hash alone is not a blanket exemption.
    const QByteArray vendorPath("C:/Users/vendor-builder/sdk/vendor.pdb");
    const auto vendor = fixture() + vendorPath;
    auto range = QJsonObject{{"offset", fixture().size()}, {"length", vendorPath.size()},
        {"sha256", QString::fromLatin1(QCryptographicHash::hash(vendorPath, QCryptographicHash::Sha256).toHex())},
        {"reason", "supplier-pdb"}};
    auto approval = QJsonObject{{"file", "vendor.dll"}, {"origin", "Synthetic supplier fixture"},
        {"sha256", QString::fromLatin1(QCryptographicHash::hash(vendor, QCryptographicHash::Sha256).toHex())},
        {"ranges", QJsonArray{range}}};
    const PathPolicy reviewed{{"vendor.dll", approval}};
    audit(vendor, "vendor.dll", {}, reviewed);
    rejects([&] { audit(vendor, "vendor.dll", {}); });
    rejects([&] { audit(vendor, "renamed.dll", {}, reviewed); });
    rejects([&] { audit(vendor + "x", "vendor.dll", {}, reviewed); });
    rejects([&] { audit(vendor, "vendor.dll", {"vendor-builder"}, reviewed); });
    auto contaminated = vendor + QByteArray("\0", 1) + "C:/Users/private-builder/source/file.cpp";
    approval["sha256"] = QString::fromLatin1(QCryptographicHash::hash(contaminated, QCryptographicHash::Sha256).toHex());
    rejects([&] { audit(contaminated, "vendor.dll", {}, {{"vendor.dll", approval}}); });
    auto badRange = range;
    badRange["sha256"] = QString(64, '0');
    auto badApproval = reviewed.value("vendor.dll");
    badApproval["ranges"] = QJsonArray{badRange};
    rejects([&] { audit(vendor, "vendor.dll", {}, {{"vendor.dll", badApproval}}); });
    QTemporaryDir policyFixture;
    require(policyFixture.isValid(), "Cannot create policy fixture");
    const auto policyPath = policyFixture.path() + "/policy.json";
    write(policyPath, QJsonDocument(QJsonObject{{"schema", 1}, {"files", QJsonArray{reviewed.value("vendor.dll")}}}).toJson());
    audit(vendor, "vendor.dll", {}, pathPolicy(policyPath));
    const auto invalidPolicy = policyFixture.path() + "/invalid.json";
    badRange["offset"] = -1;
    badApproval["ranges"] = QJsonArray{badRange};
    write(invalidPolicy, QJsonDocument(QJsonObject{{"schema", 1}, {"files", QJsonArray{badApproval}}}).toJson());
    rejects([&] { pathPolicy(invalidPolicy); });
    const auto duplicatePolicy = policyFixture.path() + "/duplicate.json";
    write(duplicatePolicy, QJsonDocument(QJsonObject{{"schema", 1},
        {"files", QJsonArray{reviewed.value("vendor.dll"), reviewed.value("vendor.dll")}}}).toJson());
    rejects([&] { pathPolicy(duplicatePolicy); });
    const auto vendorStage = policyFixture.path() + "/runtime";
    write(vendorStage + "/vendor.dll", vendor);
    archive(vendorStage, policyFixture.path() + "/vendor.zip", {}, reviewed);
    const auto privateStage = policyFixture.path() + "/private";
    write(privateStage + "/vendor.dll", vendor);
    rejects([&] { archive(privateStage, policyFixture.path() + "/private.zip", {"vendor-builder"}, reviewed); });
    require(!QFileInfo::exists(policyFixture.path() + "/private.zip"), "Rejected vendor ZIP was published");
    audit(fixture() + QByteArray("\x89x:/\x90\xff\0https://example.test/sources\0", 38), "app.exe", {});
    QTemporaryDir temporary;
    require(temporary.isValid(), "Cannot create package test directory");
    const auto stage = temporary.path() + "/runtime";
    write(stage + "/smoothmail.exe", fixture());
    write(stage + "/library.dll", fixture());
    write(stage + "/empty.txt", {});
    const auto legal = temporary.path() + "/legal";
    write(legal + "/license.txt", "Fixture license\n");
    write(legal + "/sources.md", "Sources: https://example.test/sources\n");
    write(legal + "/manifest.json", R"({"schema":1,"components":[{"name":"fixture","binaries":["*.dll"],"license_files":["license.txt"],"source_notice":"sources.md"}]})");
    auto reviewManifest = QJsonDocument::fromJson(read(legal + "/manifest.json")).object();
    reviewManifest["review_only"] = true;
    write(legal + "/manifest.review.json", QJsonDocument(reviewManifest).toJson());
    legalManifest(legal, true);
    const auto binaryReview = temporary.path() + "/binary-review";
    write(binaryReview + "/manifest.review.json", QJsonDocument(reviewManifest).toJson());
    write(binaryReview + "/sources.md", "Fixture source notice\n");
    write(binaryReview + "/license.txt", QByteArray::fromHex("89504e470d0a1a0a"));
    rejects([&] { legalManifest(binaryReview, true); });
    const auto repository = temporary.path() + "/repo";
    write(repository + "/LICENSE", "Fixture license\n");
    write(repository + "/THIRD_PARTY_NOTICES.md", "Fixture notices\n");
    prepareLegal(stage, legal, repository);
    archive(stage, temporary.path() + "/fixture.zip", {});
    const auto zip = read(temporary.path() + "/fixture.zip");
    require(zip.contains("SmoothMail/smoothmail.exe") && zip.contains("SmoothMail/licenses/license.txt")
                && !zip.contains("SmoothMail/legal/") && !zip.contains("SmoothMail/THIRD_PARTY_NOTICES.md"),
            "Missing ZIP entries");
    // Verify the first local header's fixed date, UTF-8 flag, and raw deflate data
    // by reconstructing the zlib wrapper accepted by qUncompress.
    require(qFromLittleEndian<quint16>(zip.constData() + 10) == 0
                && qFromLittleEndian<quint16>(zip.constData() + 12) == 33
                && qFromLittleEndian<quint16>(zip.constData() + 6) == 0x800,
            "Non-deterministic ZIP metadata");
    const auto firstData = read(stage + "/LICENSE");
    const auto wrapped = qCompress(firstData, 9);
    const auto size = qFromLittleEndian<quint32>(zip.constData() + 18);
    const auto length = qFromLittleEndian<quint16>(zip.constData() + 26);
    require(zip.mid(30 + length, size) == wrapped.mid(6, wrapped.size() - 10), "Invalid raw DEFLATE");
    rejects([&] { archive(stage, temporary.path() + "/fixture.zip", {}); });
    require(read(temporary.path() + "/fixture.zip") == zip, "Existing ZIP was changed");
    rejects([&] { archive(stage, temporary.path() + "/inventory-already-present.zip", {}); });
    // A second stage must produce byte-identical archives.
    const auto second = temporary.path() + "/second";
    for (const auto &name : entries(stage))
        if (name != "runtime-sha256.txt")
            write(second + "/" + name, read(stage + "/" + name));
    archive(second, temporary.path() + "/second.zip", {});
    require(read(temporary.path() + "/second.zip") == zip, "ZIP is not deterministic");
    if (!artifact.isEmpty())
        write(artifact, zip);
    const auto uncovered = temporary.path() + "/uncovered";
    write(uncovered + "/uncovered.dll", fixture());
    // Replace only an owned synthetic manifest to test missing coverage.
    QFile manifest(legal + "/manifest.json");
    require(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate), "Fixture write failed");
    manifest.write(R"({"schema":1,"components":[{"name":"fixture","binaries":["another.dll"],"license_files":["license.txt"],"source_notice":"sources.md"}]})");
    manifest.close();
    rejects([&] { prepareLegal(uncovered, legal, repository); });
    std::cout << "Package guards passed (offline)\n";
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const auto args = application.arguments();
    try {
        if ((args.size() == 2 || args.size() == 3) && args[1] == "--self-test") {
            selfTest(args.value(2));
        } else if (args.size() == 3 && args[1] == "--check-legal") {
            legalManifest(args[2]);
        } else if (args.size() == 3 && args[1] == "--check-legal-review") {
            legalManifest(args[2], true);
        } else if ((args.size() >= 3 && args[1] == "--audit-stage")
                   || (args.size() >= 4 && args[1] == "--audit-stage-with-policy")) {
            const bool reviewed = args[1] == "--audit-stage-with-policy";
            const auto policy = reviewed ? pathPolicy(args[3]) : PathPolicy{};
            for (const auto &name : entries(args[2])) {
                try {
                    audit(read(args[2] + "/" + name), name, args.mid(reviewed ? 4 : 3), policy);
                } catch (const std::runtime_error &) {
                    std::cerr << "Failed package entry: " << name.toStdString() << '\n';
                    throw;
                }
            }
        } else if ((args.size() >= 6 && args[1] == "--package")
                   || (args.size() >= 7 && args[1] == "--package-with-policy")) {
            // stage, legal directory, repository, output, private roots...
            const bool reviewed = args[1] == "--package-with-policy";
            const auto policy = reviewed ? pathPolicy(args[6]) : PathPolicy{};
            prepareLegal(args[2], args[3], args[4]);
            archive(args[2], args[5], args.mid(reviewed ? 7 : 6), policy);
        } else {
            throw std::runtime_error("Invalid package helper arguments");
        }
        return 0;
    } catch (const std::exception &error) {
        // Diagnostics never include file contents, private paths, or URLs.
        std::cerr << "Packaging failed: " << error.what() << '\n';
        return 1;
    }
}
