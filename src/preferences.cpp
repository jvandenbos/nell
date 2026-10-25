#include "preferences.h"

#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFontDatabase>
#include <QStandardPaths>

namespace {
struct ReadingTheme {
    const char *key;
    const char *page;
    const char *text;
};

// After Apple Books' page themes, plus one that borrows the desktop's own
// colors. "omarchy" has no fixed colors; they come from the Theme.
const ReadingTheme readingThemeTable[] = {
    {"original", "#ffffff", "#1d1d1f"},
    {"paper", "#f7f2e8", "#2c2a26"},
    {"calm", "#ebdfc6", "#3b2f22"},
    {"quiet", "#3b3b3e", "#d6d6d6"},
    {"night", "#111111", "#c4c4c4"},
    {"omarchy", nullptr, nullptr},
};

QString settingsFile(const QString &file) {
    if (!file.isEmpty())
        return file;
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/nell/nell.conf";
}
}

Preferences::Preferences(Theme *theme, const QString &file, QObject *parent)
    : QObject(parent), m_settings(settingsFile(file), QSettings::IniFormat), m_theme(theme) {
    // QSettings would rewrite a file it couldn't parse from only the keys it
    // understood, so a damaged or newer one is read but never written.
    if (m_settings.status() != QSettings::NoError) {
        m_writable = false;
        m_storageProblem = tr("%1 is damaged, so Nell is leaving it as it is and not saving settings.")
                               .arg(m_settings.fileName());
    } else if (m_settings.value("version", FormatVersion).toInt() > FormatVersion) {
        m_writable = false;
        m_storageProblem = tr("%1 was written by a newer Nell, so this one doesn't save settings.").arg(m_settings.fileName());
    }
    m_readingTheme = m_settings.value("reading/theme", "original").toString();
    if (!readingThemes().contains(m_readingTheme))
        m_readingTheme = "original";
    m_fontSize = qBound(12, m_settings.value("reading/fontSize", 20).toInt(), 44);
    // Proportional to the font's own line height, which already has leading.
    m_lineSpacing = qBound(1.0, m_settings.value("reading/lineSpacing", 1.1).toDouble(), 2.0);
    // Ragged right by default: justified narrow columns open rivers.
    m_justify = m_settings.value("reading/justify", false).toBool();
    m_margins = qBound(0, m_settings.value("reading/margins", 1).toInt(), 2);
    m_spread = m_settings.value("reading/spread", true).toBool();
    m_hideRunningHeads = m_settings.value("reading/hideRunningHeads", true).toBool();
    m_librarySort = m_settings.value("library/sort", "recent").toString();
    m_infoMode = m_settings.value("reading/infoMode", 0).toInt();
    // About 250 words a minute, until the reader shows us their own pace.
    m_readingSpeed = qBound(3.0, m_settings.value("reading/speed", 22.0).toDouble(), 200.0);
    m_highlightColor = m_settings.value("reading/highlightColor", "yellow").toString();
    m_reduceMotion = m_settings.value("ui/reduceMotion", false).toBool();

    const QStringList installed = QFontDatabase::families();
    for (const char *family : {"Literata", "Charter", "Georgia", "EB Garamond", "Crimson Pro", "Noto Serif", "C059",
                               "Liberation Serif", "iA Writer Quattro S", "Noto Sans", "Adwaita Sans"})
        if (installed.contains(QString::fromLatin1(family)))
            m_fonts << QString::fromLatin1(family);
    m_fontFamily = m_settings.value("reading/font", m_fonts.value(0)).toString();
    if (!m_fontFamily.isEmpty() && !m_fonts.contains(m_fontFamily))
        m_fontFamily = m_fonts.value(0);

    if (m_theme)
        connect(m_theme, &Theme::changed, this, [this] {
            if (m_readingTheme == QLatin1String("omarchy"))
                emit colorsChanged();
        });
}

QStringList Preferences::readingThemes() const {
    QStringList keys;
    for (const auto &theme : readingThemeTable)
        keys << QString::fromLatin1(theme.key);
    return keys;
}

void Preferences::setReadingTheme(const QString &key) {
    if (key == m_readingTheme || !readingThemes().contains(key))
        return;
    m_readingTheme = key;
    store("reading/theme", key);
    emit colorsChanged();
}

QString Preferences::pageColor() const {
    for (const auto &theme : readingThemeTable)
        if (m_readingTheme == QLatin1String(theme.key))
            return theme.page ? QString::fromLatin1(theme.page) : m_theme ? m_theme->background() : "#1a1b26";
    return "#ffffff";
}

QString Preferences::textColor() const {
    for (const auto &theme : readingThemeTable)
        if (m_readingTheme == QLatin1String(theme.key))
            return theme.text ? QString::fromLatin1(theme.text) : m_theme ? m_theme->foreground() : "#c0caf5";
    return "#1d1d1f";
}

QStringList Preferences::themeColors(const QString &key) const {
    for (const auto &theme : readingThemeTable)
        if (key == QLatin1String(theme.key))
            return theme.page ? QStringList{QString::fromLatin1(theme.page), QString::fromLatin1(theme.text)}
                              : QStringList{m_theme ? m_theme->background() : "#1a1b26", m_theme ? m_theme->foreground() : "#c0caf5"};
    return {};
}

bool Preferences::darkPage() const { return QColor(pageColor()).lightnessF() < 0.5; }

void Preferences::setFontFamily(const QString &family) {
    if (family == m_fontFamily)
        return;
    m_fontFamily = family;
    store("reading/font", family);
    emit layoutChanged();
}

void Preferences::setFontSize(int size) {
    size = qBound(12, size, 44);
    if (size == m_fontSize)
        return;
    m_fontSize = size;
    store("reading/fontSize", size);
    emit layoutChanged();
}

void Preferences::setLineSpacing(double spacing) {
    spacing = qBound(1.0, spacing, 2.0);
    if (qFuzzyCompare(spacing, m_lineSpacing))
        return;
    m_lineSpacing = spacing;
    store("reading/lineSpacing", spacing);
    emit layoutChanged();
}

void Preferences::setJustify(bool justify) {
    if (justify == m_justify)
        return;
    m_justify = justify;
    store("reading/justify", justify);
    emit layoutChanged();
}

void Preferences::setMargins(int margins) {
    margins = qBound(0, margins, 2);
    if (margins == m_margins)
        return;
    m_margins = margins;
    store("reading/margins", margins);
    emit layoutChanged();
}

void Preferences::setSpread(bool spread) {
    if (spread == m_spread)
        return;
    m_spread = spread;
    store("reading/spread", spread);
    emit layoutChanged();
}

void Preferences::setHideRunningHeads(bool hide) {
    if (hide == m_hideRunningHeads)
        return;
    m_hideRunningHeads = hide;
    store("reading/hideRunningHeads", hide);
    emit layoutChanged();
}

void Preferences::setLibrarySort(const QString &sort) {
    if (sort == m_librarySort)
        return;
    m_librarySort = sort;
    store("library/sort", sort);
    emit librarySortChanged();
}

void Preferences::setInfoMode(int mode) {
    if (mode == m_infoMode)
        return;
    m_infoMode = mode;
    store("reading/infoMode", mode);
    emit infoModeChanged();
}

void Preferences::learnReadingSpeed(double charactersPerSecond) {
    // A slow moving average: one skimmed page shouldn't swing the estimate.
    m_readingSpeed = qBound(3.0, m_readingSpeed * 0.85 + charactersPerSecond * 0.15, 200.0);
    store("reading/speed", m_readingSpeed);
}

void Preferences::store(const QString &key, const QVariant &value) {
    if (!m_writable)
        return;
    m_settings.setValue(key, value);
    m_settings.setValue("version", FormatVersion);
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError)
        setStorageProblem(tr("Could not save settings in %1.").arg(m_settings.fileName()));
}

void Preferences::setStorageProblem(const QString &problem) {
    if (problem == m_storageProblem)
        return;
    m_storageProblem = problem;
    emit storageProblemChanged();
}

void Preferences::setHighlightColor(const QString &color) {
    if (color == m_highlightColor || color.isEmpty())
        return;
    m_highlightColor = color;
    m_settings.setValue("reading/highlightColor", color);
    emit highlightColorChanged();
}

void Preferences::setReduceMotion(bool reduce) {
    if (reduce == m_reduceMotion)
        return;
    m_reduceMotion = reduce;
    m_settings.setValue("ui/reduceMotion", reduce);
    emit reduceMotionChanged();
}
