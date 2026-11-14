// Build-time evidence only. Does not certify license compliance.
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QCryptographicHash>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
QByteArray read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read SDK SBOM");
    return file.readAll();
}
QString id(QString value)
{
    // SPDX cross-document links use DocumentRef-...:SPDXRef-...
    return value.mid(value.lastIndexOf(':') + 1);
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        if (app.arguments().size() != 3) throw std::runtime_error("Specify SDK root and new report file");
        QMap<QString, QJsonObject> packages;
        QMap<QString, QStringList> edges;
        QSet<QString> roots;
        QSet<QString> rootNames;
        const QSet<QString> shipped{"Core", "Gui", "Network", "Sql", "Widgets", "Xml",
            "QWindowsIntegrationPlugin", "QSQLiteDriverPlugin", "QSchannelBackendPlugin", "Svg"};
        QJsonArray inputs;
        for (const auto &module : {"qtbase", "qtsvg"}) {
            const auto bytes = read(app.arguments()[1] + "/sbom/" + module + "-6.12.0.spdx.json");
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(bytes, &error);
            if (error.error != QJsonParseError::NoError || !document.isObject())
                throw std::runtime_error("Invalid SDK SBOM");
            const auto object = document.object();
            inputs.append(QJsonObject{{"module", module},
                {"sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}});
            for (const auto &entry : object["packages"].toArray()) {
                const auto package = entry.toObject();
                const auto key = package["SPDXID"].toString();
                packages.insert(key, package);
                if (shipped.contains(package["name"].toString())) {
                    roots.insert(key);
                    rootNames.insert(package["name"].toString());
                }
            }
            for (const auto &entry : object["relationships"].toArray()) {
                const auto relation = entry.toObject();
                if (relation["relationshipType"].toString() == "DEPENDS_ON")
                    edges[id(relation["spdxElementId"].toString())].append(id(relation["relatedSpdxElement"].toString()));
            }
        }
        if (roots.size() != shipped.size() || rootNames != shipped)
            throw std::runtime_error("Missing or ambiguous shipped module roots");
        QStringList pending = roots.values();
        QSet<QString> selected;
        QSet<QString> unresolved;
        while (!pending.isEmpty()) {
            const auto key = pending.takeLast();
            if (selected.contains(key)) continue;
            if (!packages.contains(key)) { unresolved.insert(key); continue; }
            selected.insert(key);
            pending.append(edges.value(key));
        }
        auto keys = selected.values();
        std::sort(keys.begin(), keys.end());
        QJsonArray rows;
        for (const auto &key : keys) {
            const auto package = packages.value(key);
            rows.append(QJsonObject{{"id", key}, {"name", package["name"]},
                {"version", package["versionInfo"]}, {"license", package["licenseConcluded"]},
                {"copyright", package["copyrightText"]}, {"shippedRoot", roots.contains(key)}});
        }
        auto missing = unresolved.values();
        std::sort(missing.begin(), missing.end());
        QJsonArray missingRows;
        for (const auto &key : missing) missingRows.append(key);
        QJsonObject report{{"schema", 1}, {"review_only", true},
            {"scope", "DEPENDS_ON closure from the ten shipped Qt modules/plugins; not a compiled-file or legal certification"},
            {"inputs", inputs}, {"packages", rows}, {"unresolved", missingRows}};
        QFile output(app.arguments()[2]);
        if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            throw std::runtime_error("Choose a new report file");
        const auto bytes = QJsonDocument(report).toJson();
        if (output.write(bytes) != bytes.size() || !output.flush())
            throw std::runtime_error("Cannot write complete report");
        std::cout << "Shipped roots: " << roots.size() << "; dependency packages: " << selected.size()
                  << "; unresolved: " << unresolved.size() << '\n';
        return unresolved.isEmpty() ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << "SBOM review failed: " << error.what() << '\n';
        return 1;
    }
}
