#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtEndian>
#include <windows.h>
#include <clocale>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
void u32(QByteArray &data, quint32 value)
{
    value = qToLittleEndian(value);
    data.append(reinterpret_cast<const char *>(&value), 4);
}
QByteArray catalog()
{
    const QList<QByteArray> originals{"", "native-probe", QByteArray("one\0many", 8)};
    const QList<QByteArray> translations{
        "Content-Type: text/plain; charset=UTF-8\nLanguage: zz\nPlural-Forms: nplurals=2; plural=(n != 1);\n",
        QStringLiteral("変換成功").toUtf8(),
        QStringLiteral("単数").toUtf8() + '\0' + QStringLiteral("複数").toUtf8()};
    QByteArray data;
    u32(data, 0x950412de); u32(data, 0); u32(data, 3);
    u32(data, 28); u32(data, 52); u32(data, 0); u32(data, 0);
    QByteArray strings;
    for (const auto &table : {originals, translations}) {
        for (const auto &text : table) {
            u32(data, quint32(text.size())); u32(data, 76 + quint32(strings.size()));
            strings += text; strings += '\0';
        }
    }
    return data + strings;
}
template<typename Function> Function resolve(HMODULE module, const char *name)
{
    const auto address = GetProcAddress(module, name);
    require(address != nullptr, "Native export missing");
    return reinterpret_cast<Function>(address);
}
HMODULE load(const QString &path)
{
    const auto module = LoadLibraryExW(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    require(module != nullptr, "Cannot load isolated native DLL");
    wchar_t loaded[32768]{};
    const auto size = GetModuleFileNameW(module, loaded, 32768);
    require(size > 0 && size < 32768, "Cannot identify loaded native DLL");
    require(QFileInfo(QString::fromWCharArray(loaded, int(size))).canonicalFilePath().compare(
                QFileInfo(path).canonicalFilePath(), Qt::CaseInsensitive) == 0,
            "Loaded unexpected native DLL");
    return module;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    try {
        require(application.arguments().size() == 2, "Choose a fixture directory containing the GNU DLLs");
        const auto directory = QFileInfo(application.arguments()[1]).absoluteFilePath();
        const auto iconv = load(directory + "/iconv-2.dll");
        const auto intl = load(directory + "/intl-8.dll");
        const auto open = resolve<void *(*)(const char *, const char *)>(iconv, "libiconv_open");
        const auto convert = resolve<size_t (*)(void *, char **, size_t *, char **, size_t *)>(iconv, "libiconv");
        const auto close = resolve<int (*)(void *)>(iconv, "libiconv_close");
        auto descriptor = open("UTF-8", "SHIFT_JIS");
        require(descriptor != reinterpret_cast<void *>(-1), "Cannot open Shift-JIS conversion");
        char inputBytes[]{char(0x82), char(0xa0)};
        char outputBytes[32]{};
        char *input = inputBytes, *output = outputBytes;
        size_t inputSize = 2, outputSize = sizeof(outputBytes);
        require(convert(descriptor, &input, &inputSize, &output, &outputSize) != size_t(-1)
                    && inputSize == 0 && QByteArray(outputBytes, output - outputBytes) == QByteArray::fromHex("e38182"),
                "Shift-JIS to UTF-8 conversion failed");
        char invalid[]{char(0x82)};
        input = invalid; inputSize = 1; output = outputBytes; outputSize = sizeof(outputBytes);
        require(convert(descriptor, &input, &inputSize, &output, &outputSize) == size_t(-1),
                "Incomplete character was silently accepted");
        require(close(descriptor) == 0, "Cannot close conversion");

        qputenv("LANGUAGE", "zz");
        const auto locale = resolve<char *(*)(int, const char *)>(intl, "libintl_setlocale");
        const auto bind = resolve<char *(*)(const char *, const char *)>(intl, "libintl_bindtextdomain");
        const auto codeset = resolve<char *(*)(const char *, const char *)>(intl, "libintl_bind_textdomain_codeset");
        const auto domain = resolve<char *(*)(const char *)>(intl, "libintl_textdomain");
        const auto gettext = resolve<char *(*)(const char *)>(intl, "libintl_gettext");
        const auto plural = resolve<char *(*)(const char *, const char *, unsigned long)>(intl, "libintl_ngettext");
        const char *defaultDirectory = bind("native-default-probe", nullptr);
        require(defaultDirectory != nullptr, "Cannot query the logical catalog directory");
        auto logicalDirectory = QString::fromUtf8(defaultDirectory);
        logicalDirectory.replace('\\', '/');
        require(logicalDirectory == QStringLiteral("/share/gettext-libintl/locale"),
                "Unexpected logical catalog directory (UNC/MSYS path conversion must be avoided)");
        require(locale(LC_ALL, "English_United States.65001") != nullptr, "Cannot set process-local test locale");
        const auto catalogRoot = directory + "/catalogs";
        const auto catalogFile = catalogRoot + "/zz/LC_MESSAGES/native-probe.mo";
        require(QDir().mkpath(QFileInfo(catalogFile).absolutePath()), "Cannot prepare synthetic catalog");
        const auto bytes = catalog();
        QFile file(catalogFile);
        if (file.exists()) {
            require(file.open(QIODevice::ReadOnly) && file.readAll() == bytes, "Unexpected existing synthetic catalog");
        } else {
            require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
                        && file.write(bytes) == bytes.size(), "Cannot write synthetic catalog");
        }
        file.close();
        QTemporaryDir unrelated;
        require(unrelated.isValid() && QDir::setCurrent(unrelated.path()), "Cannot isolate working directory");
        const auto catalogPath = QDir::toNativeSeparators(catalogRoot).toUtf8();
        require(bind("native-probe", catalogPath.constData()) != nullptr
                    && codeset("native-probe", "UTF-8") != nullptr && domain("native-probe") != nullptr,
                "Cannot bind relocated synthetic catalog");
        require(QByteArray(gettext("native-probe")) == QStringLiteral("変換成功").toUtf8(),
                "Relocated UTF-8 catalog lookup failed");
        require(QByteArray(plural("one", "many", 1)) == QStringLiteral("単数").toUtf8()
                    && QByteArray(plural("one", "many", 2)) == QStringLiteral("複数").toUtf8(),
                "Relocated plural catalog lookup failed");
        QDir::setCurrent(directory);
        std::cout << "GNU conversion, invalid-input rejection, UTF-8 catalog and plural lookup passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
