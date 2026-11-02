#include "hyphenator.h"

#include <QDir>
#include <QFile>
#include <QMutex>
#include <QRegularExpression>
#include <QStringList>

namespace {
// The bundled tags, and the tag a book's language falls back to: a book in
// "en" or "en-US" reads the en-US patterns, "de-AT" the German ones.
const char *const Bundled[] = {"en-US", "en-GB", "de", "fr", "es", "nl", "da"};

QString bundledFor(const QString &language) {
    const QString tag = QString(language).replace('_', '-');
    const QString base = tag.section('-', 0, 0).toLower();
    for (const char *bundled : Bundled)
        if (tag.compare(QLatin1String(bundled), Qt::CaseInsensitive) == 0)
            return QString::fromLatin1(bundled);
    if (base == QLatin1String("en"))
        return QStringLiteral("en-US");
    for (const char *bundled : Bundled)
        if (base == QLatin1String(bundled))
            return QString::fromLatin1(bundled);
    return {};
}

// libhyphen's dictionaries, as Arch's hyphen-* packages install them.
QString installedFor(const QString &language) {
    const QString tag = QString(language).replace('-', '_');
    const QString base = tag.section('_', 0, 0).toLower();
    if (base.isEmpty())
        return {};
    const QDir dir("/usr/share/hyphen");
    const QStringList files = dir.entryList({"hyph_*.dic"}, QDir::Files, QDir::Name);
    for (const auto &file : files)
        if (file.compare("hyph_" + tag + ".dic", Qt::CaseInsensitive) == 0)
            return dir.filePath(file);
    for (const auto &file : files)
        if (file.startsWith("hyph_" + base + "_", Qt::CaseInsensitive) || file.compare("hyph_" + base + ".dic", Qt::CaseInsensitive) == 0)
            return dir.filePath(file);
    return {};
}

QByteArray readAll(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

QStringList Hyphenator::bundled() {
    QStringList tags;
    for (const char *tag : Bundled)
        tags << QString::fromLatin1(tag);
    return tags;
}

const Hyphenator *Hyphenator::forLanguage(const QString &language) {
    static QMutex mutex;
    static QHash<QString, Hyphenator *> loaded;
    const QString tag = bundledFor(language);
    const QString key = tag.isEmpty() ? installedFor(language) : ":/hyphen/" + tag;
    if (key.isEmpty())
        return nullptr;
    QMutexLocker lock(&mutex);
    auto it = loaded.find(key);
    if (it != loaded.end())
        return *it;
    auto *hyphenator = new Hyphenator;
    const bool ok = tag.isEmpty() ? hyphenator->loadDictionary(readAll(key))
                                  : hyphenator->loadPatterns(readAll(key + ".pat.txt"), readAll(key + ".hyp.txt"));
    if (!ok) {
        delete hyphenator;
        hyphenator = nullptr;
    }
    loaded.insert(key, hyphenator);
    return hyphenator;
}

bool Hyphenator::loadPatterns(const QByteArray &patterns, const QByteArray &exceptions) {
    for (const auto &line : QString::fromUtf8(patterns).split('\n'))
        addPattern(line.trimmed());
    for (const auto &line : QString::fromUtf8(exceptions).split('\n'))
        addException(line.trimmed());
    return isValid();
}

bool Hyphenator::loadDictionary(const QByteArray &dictionary) {
    const QList<QByteArray> lines = dictionary.split('\n');
    if (lines.isEmpty())
        return false;
    // The first line names the encoding; the patterns follow, with a few
    // options among them. A second level (NEXTLEVEL) is for non-standard
    // hyphenation, which Nell doesn't do.
    const bool utf8 = lines[0].trimmed().toUpper() != "ISO8859-1" && !lines[0].trimmed().toUpper().startsWith("ISO-8859-1");
    for (int i = 1; i < lines.size(); ++i) {
        const QString line = (utf8 ? QString::fromUtf8(lines[i]) : QString::fromLatin1(lines[i])).trimmed();
        if (line.isEmpty() || line.startsWith('%'))
            continue;
        if (line == QLatin1String("NEXTLEVEL"))
            break;
        if (line.startsWith("LEFTHYPHENMIN"))
            m_leftMin = qMax(1, line.section(' ', 1).toInt());
        else if (line.startsWith("RIGHTHYPHENMIN"))
            m_rightMin = qMax(1, line.section(' ', 1).toInt());
        else if (line.at(0).isUpper() || line.contains('/'))
            continue;  // Other options, and replacement patterns.
        else
            addPattern(line);
    }
    return isValid();
}

void Hyphenator::addPattern(const QString &pattern) {
    if (pattern.isEmpty() || pattern.startsWith('%') || pattern.startsWith('#'))
        return;
    QString letters;
    QByteArray levels;
    levels.append(char(0));
    for (const QChar &c : pattern) {
        if (c.isDigit()) {
            levels[levels.size() - 1] = char(c.digitValue());
        } else {
            letters += c;
            levels.append(char(0));
        }
    }
    if (letters.isEmpty())
        return;
    m_patterns.insert(letters, levels);
    m_longest = qMax(m_longest, int(letters.size()));
}

void Hyphenator::addException(const QString &word) {
    if (word.isEmpty() || word.startsWith('%') || word.startsWith('#'))
        return;
    QList<int> breaks;
    QString letters;
    for (const QChar &c : word) {
        if (c == '-')
            breaks << letters.size();
        else
            letters += c;
    }
    m_exceptions.insert(letters, breaks);
}

QList<int> Hyphenator::breaks(const QString &word) const {
    if (word.size() < m_leftMin + m_rightMin)
        return {};
    for (const QChar &c : word)
        if (!c.isLetter())
            return {};
    const QString lower = word.toLower();
    if (lower.size() != word.size())
        return {};
    auto exception = m_exceptions.constFind(lower);
    if (exception != m_exceptions.constEnd())
        return *exception;

    const QString dotted = "." + lower + ".";
    QByteArray levels(dotted.size() + 1, char(0));
    for (int start = 0; start < dotted.size(); ++start) {
        for (int length = 1; length <= m_longest && start + length <= dotted.size(); ++length) {
            auto it = m_patterns.constFind(dotted.mid(start, length));
            if (it == m_patterns.constEnd())
                continue;
            const QByteArray &found = *it;
            for (int i = 0; i < found.size(); ++i)
                levels[start + i] = qMax(levels[start + i], found[i]);
        }
    }
    // levels[i] sits before dotted[i]; dotted[i] is lower[i - 1].
    QList<int> result;
    for (int i = 2; i < dotted.size() - 1; ++i) {
        const int position = i - 1;  // A break before lower[position].
        if (levels[i] % 2 == 1 && position >= m_leftMin && lower.size() - position >= m_rightMin)
            result << position;
    }
    return result;
}
