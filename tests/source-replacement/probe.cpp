// Disposable-runtime probe only. No credentials, accounts or external network.
#include <windows.h>
#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QDomDocument>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSvgRenderer>
#include <QTextDocument>
#include <QVariant>
#include <KCodecs>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}
void localModule(const wchar_t *name, const char *marker)
{
    const auto module = GetModuleHandleW(name);
    require(module != nullptr, "Expected module not loaded");
    wchar_t file[32768]{};
    const auto length = GetModuleFileNameW(module, file, 32768);
    require(length > 0 && length < 32768, "Cannot identify loaded module");
    const auto path = QDir::fromNativeSeparators(QString::fromWCharArray(file));
    require(QFileInfo(path).canonicalPath() == QDir(QCoreApplication::applicationDirPath()).canonicalPath(),
        "DLL loaded outside the isolated runtime");
    if (marker) {
        using Marker = int (*)();
        const auto function = reinterpret_cast<Marker>(GetProcAddress(module, marker));
        require(function && function() == 45654, "Modified DLL marker missing");
    }
}
}
int main(int argc, char **argv)
{
    try {
        QApplication application(argc, argv);
        const bool markers = application.arguments().contains(QStringLiteral("--require-markers"));
        require(QString::fromLatin1(qVersion()) == QStringLiteral("6.12.0"), "Unexpected Qt version");
        const auto text = QString::fromUtf8("再構築・差替え");
        require(QString::fromUtf8(text.toUtf8()) == text, "QtCore Unicode round trip failed");
        QDomDocument xml;
        require(bool(xml.setContent(QStringLiteral("<root><value>42</value></root>"))), "QtXml parse failed");
        require(xml.documentElement().firstChildElement().text() == QStringLiteral("42"), "QtXml result failed");
        QSvgRenderer svg(QByteArray("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16'>"
            "<rect width='16' height='16' fill='#00ff00'/></svg>"));
        require(svg.isValid(), "QtSvg parse failed");
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        { QPainter painter(&image); svg.render(&painter); }
        require(image.pixelColor(8, 8) == QColor(Qt::green), "QtSvg render failed");
        QTextDocument document;
        document.setHtml(QStringLiteral("<p>Rebuilt <b>Qt</b></p>"));
        require(document.toPlainText() == QStringLiteral("Rebuilt Qt"), "QtGui text failed");
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
        database.setDatabaseName(QStringLiteral(":memory:"));
        require(database.open(), "Rebuilt SQLite plugin failed");
        QSqlQuery query;
        require(query.exec(QStringLiteral("SELECT 6 * 7")) && query.next() && query.value(0).toInt() == 42,
            "Rebuilt SQLite query failed");
        require(KCodecs::decodeRFC2047String(QStringView(u"=?UTF-8?B?5pel5pys6Kqe?=")) == QStringLiteral("日本語"),
            "Rebuilt KCodecs RFC2047 decode failed");
        for (const auto *name : {L"Qt6Core.dll", L"Qt6Gui.dll", L"Qt6Widgets.dll", L"Qt6Sql.dll", L"Qt6Xml.dll"})
            localModule(name, nullptr);
        localModule(L"Qt6Svg.dll", markers ? "smoothmail_source_replacement_svg" : nullptr);
        localModule(L"KF6Codecs.dll", markers ? "smoothmail_source_replacement_codecs" : nullptr);
        std::cout << "PASS: Unicode/XML/SVG/text/SQLite/RFC2047; local DLLs"
                  << (markers ? "; both modified-library markers\n" : "\n");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
