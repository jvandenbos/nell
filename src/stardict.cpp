#include "stardict.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextDocumentFragment>
#include <QtEndian>
#include <algorithm>
#include <cstring>
#include <numeric>
#include <zlib.h>

namespace {
// Ceilings on what a dictionary file may ask for. The biggest real ones (a
// full Webster's, a Wiktionary dump) are well inside them.
constexpr qint64 MaxIndexBytes = 512ll * 1024 * 1024;
constexpr qint64 MaxWholeDictBytes = 512ll * 1024 * 1024;
constexpr quint32 MaxArticleBytes = 1024 * 1024;
constexpr int MaxArticleCharacters = 6000;
constexpr int MaxArticlesPerWord = 8;

// StarDict's order for an index: ASCII letters without regard to case, as
// glib's g_ascii_strcasecmp has it. Everything else compares as bytes.
int compareFolded(const char *a, const char *b) {
    for (;; ++a, ++b) {
        unsigned char x = static_cast<unsigned char>(*a), y = static_cast<unsigned char>(*b);
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return x < y ? -1 : 1;
        if (!x)
            return 0;
    }
}

// Unpacks deflate data: a whole gzip file (windowBits 15 + 32), or one
// chunk of a dictzip file (-15: raw, and it ends at a flush, not at the end
// of the stream). *finished says whether the stream's own end was reached.
QByteArray inflateData(const QByteArray &input, int windowBits, qint64 limit, bool *finished = nullptr) {
    if (finished)
        *finished = false;
    z_stream stream;
    std::memset(&stream, 0, sizeof stream);
    if (inflateInit2(&stream, windowBits) != Z_OK)
        return {};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
    stream.avail_in = uInt(input.size());
    QByteArray output;
    char buffer[65536];
    bool ok = true;
    for (;;) {
        stream.next_out = reinterpret_cast<Bytef *>(buffer);
        stream.avail_out = sizeof buffer;
        const int status = inflate(&stream, Z_NO_FLUSH);
        const qsizetype produced = qsizetype(sizeof buffer - stream.avail_out);
        output.append(buffer, produced);
        if (output.size() > limit) {
            ok = false;
            break;
        }
        if (status == Z_STREAM_END) {
            if (finished)
                *finished = true;
            break;
        }
        // Out of input with nothing more to give: the end of a chunk.
        if (status == Z_BUF_ERROR && produced == 0)
            break;
        if (status != Z_OK && status != Z_BUF_ERROR) {
            ok = false;
            break;
        }
        if (stream.avail_in == 0 && stream.avail_out != 0)
            break;
    }
    inflateEnd(&stream);
    return ok ? output : QByteArray();
}

// Where each entry of an index starts. An entry is a word, a NUL, and
// `tail` bytes of numbers. False if the file doesn't parse to its end.
bool entryStarts(const QByteArray &index, int tail, std::vector<quint32> *starts) {
    const char *data = index.constData();
    const qsizetype size = index.size();
    qsizetype position = 0;
    while (position < size) {
        const void *nul = std::memchr(data + position, 0, size_t(size - position));
        if (!nul)
            return false;
        const qsizetype end = static_cast<const char *>(nul) - data;
        if (end + 1 + tail > size)
            return false;
        starts->push_back(quint32(position));
        position = end + 1 + tail;
    }
    return true;
}

// The entries in the order a binary search wants. A file that is already
// in it (as it should be) costs one pass.
std::vector<quint32> searchOrder(const QByteArray &index, const std::vector<quint32> &starts) {
    std::vector<quint32> order(starts.size());
    std::iota(order.begin(), order.end(), 0u);
    const char *data = index.constData();
    const auto before = [&](quint32 a, quint32 b) { return compareFolded(data + starts[a], data + starts[b]) < 0; };
    if (!std::is_sorted(order.begin(), order.end(), before))
        std::stable_sort(order.begin(), order.end(), before);
    return order;
}

QString fromMarkup(QString markup) {
    // Line breaks mean something in XDXF and Pango markup, and nothing in HTML.
    markup.replace('\n', "<br>");
    return QTextDocumentFragment::fromHtml(markup).toPlainText();
}

QString tidy(QString text) {
    text.replace(QChar::ParagraphSeparator, '\n').replace(QChar::LineSeparator, '\n').replace("\r\n", "\n").replace('\r', '\n');
    text.remove(QChar::ObjectReplacementCharacter);
    text.replace(QChar::Nbsp, ' ');
    static const QRegularExpression trailing("[ \\t]+\\n");
    text.replace(trailing, "\n");
    static const QRegularExpression gaps("\\n{3,}");
    text.replace(gaps, "\n\n");
    text = text.trimmed();
    if (text.size() > MaxArticleCharacters)
        text = text.left(MaxArticleCharacters).trimmed() + "…";
    return text;
}
}

QByteArray StarDict::gunzip(const QString &path, qint64 limit) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit)
        return {};
    bool finished = false;
    const QByteArray data = inflateData(file.readAll(), 15 + 32, limit, &finished);
    return finished ? data : QByteArray();
}

std::unique_ptr<StarDict> StarDict::open(const QString &ifoPath, QString *error) {
    const auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return nullptr;
    };
    QFile ifo(ifoPath);
    if (!ifo.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("%1 can't be read.").arg(ifoPath));
    const QList<QByteArray> lines = ifo.read(64 * 1024).split('\n');
    if (lines.isEmpty() || !lines.first().trimmed().startsWith("StarDict's dict ifo file"))
        return fail(QStringLiteral("%1 isn't a StarDict dictionary.").arg(ifoPath));

    std::unique_ptr<StarDict> dictionary(new StarDict);
    dictionary->m_ifo = ifoPath;
    for (const QByteArray &line : lines) {
        const int equals = line.indexOf('=');
        if (equals < 0)
            continue;
        const QByteArray key = line.left(equals).trimmed(), value = line.mid(equals + 1).trimmed();
        if (key == "bookname")
            dictionary->m_name = QString::fromUtf8(value);
        else if (key == "wordcount")
            dictionary->m_wordCount = value.toLongLong();
        else if (key == "idxoffsetbits" && value == "64")
            dictionary->m_offsetBytes = 8;
        else if (key == "sametypesequence")
            dictionary->m_types = value;
    }
    const QFileInfo info(ifoPath);
    const QString base = info.path() + "/" + info.completeBaseName();
    if (dictionary->m_name.isEmpty())
        dictionary->m_name = info.completeBaseName();
    for (const char *suffix : {".idx", ".idx.gz"})
        if (dictionary->m_idxPath.isEmpty() && QFileInfo::exists(base + suffix))
            dictionary->m_idxPath = base + suffix;
    for (const char *suffix : {".dict.dz", ".dict"})
        if (dictionary->m_dictPath.isEmpty() && QFileInfo::exists(base + suffix))
            dictionary->m_dictPath = base + suffix;
    if (QFileInfo::exists(base + ".syn"))
        dictionary->m_synPath = base + ".syn";
    if (dictionary->m_idxPath.isEmpty() || dictionary->m_dictPath.isEmpty())
        return fail(QStringLiteral("%1 is missing its .idx or .dict file.").arg(ifoPath));
    return dictionary;
}

bool StarDict::load() {
    m_loaded = true;
    if (m_idxPath.endsWith(".gz")) {
        m_idx = gunzip(m_idxPath, MaxIndexBytes);
    } else {
        QFile file(m_idxPath);
        if (file.open(QIODevice::ReadOnly) && file.size() <= MaxIndexBytes)
            m_idx = file.readAll();
    }
    if (m_idx.isEmpty() || !entryStarts(m_idx, m_offsetBytes + 4, &m_idxStarts)) {
        m_idx.clear();
        m_idxStarts.clear();
        m_problem = QStringLiteral("%1 can't be read, or is damaged.").arg(m_idxPath);
        return false;
    }
    m_idxOrder = searchOrder(m_idx, m_idxStarts);

    // Other spellings are a nicety: a .syn that doesn't parse is left out.
    if (!m_synPath.isEmpty()) {
        QFile file(m_synPath);
        if (file.open(QIODevice::ReadOnly) && file.size() <= MaxIndexBytes)
            m_syn = file.readAll();
        if (entryStarts(m_syn, 4, &m_synStarts)) {
            m_synOrder = searchOrder(m_syn, m_synStarts);
        } else {
            m_syn.clear();
            m_synStarts.clear();
        }
    }

    if (m_dictPath.endsWith(".dz") && !readDictzipHeader())
        m_dictInMemory = true;
    return true;
}

// A dictzip file is a gzip file whose header lists the packed size of each
// chunk (the "RA" extra field), so any chunk can be found and unpacked on
// its own. False if this one has no such list.
bool StarDict::readDictzipHeader() {
    QFile file(m_dictPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = file.read(160 * 1024);
    const auto *bytes = reinterpret_cast<const uchar *>(head.constData());
    const qsizetype size = head.size();
    if (size < 12 || bytes[0] != 0x1f || bytes[1] != 0x8b || bytes[2] != 8)
        return false;
    const int flags = bytes[3];
    if (!(flags & 4))
        return false;
    qsizetype position = 10;
    const qsizetype extraEnd = position + 2 + qFromLittleEndian<quint16>(bytes + position);
    position += 2;
    if (extraEnd > size)
        return false;
    bool found = false;
    while (position + 4 <= extraEnd) {
        const char first = char(bytes[position]), second = char(bytes[position + 1]);
        const qsizetype length = qFromLittleEndian<quint16>(bytes + position + 2);
        const qsizetype field = position + 4;
        if (field + length > extraEnd)
            return false;
        if (first == 'R' && second == 'A' && length >= 6) {
            m_chunkLength = qFromLittleEndian<quint16>(bytes + field + 2);
            const int count = qFromLittleEndian<quint16>(bytes + field + 4);
            if (m_chunkLength == 0 || 6 + 2 * count > length)
                return false;
            for (int i = 0; i < count; ++i)
                m_chunkSizes.push_back(qFromLittleEndian<quint16>(bytes + field + 6 + 2 * i));
            found = true;
        }
        position = field + length;
    }
    if (!found)
        return false;
    position = extraEnd;
    // The original file's name, then a comment, each ending in a NUL.
    for (const int flag : {8, 16})
        if (flags & flag) {
            const void *nul = position < size ? std::memchr(bytes + position, 0, size_t(size - position)) : nullptr;
            if (!nul)
                return false;
            position = static_cast<const uchar *>(nul) - bytes + 1;
        }
    if (flags & 2)
        position += 2;
    quint64 offset = quint64(position);
    for (const quint32 chunk : m_chunkSizes) {
        m_chunkOffsets.push_back(offset);
        offset += chunk;
    }
    m_dictzip = true;
    return true;
}

QByteArray StarDict::article(quint64 offset, quint32 size) {
    size = qMin(size, MaxArticleBytes);
    if (size == 0)
        return {};
    if (m_dictInMemory) {
        if (m_dictWhole.isNull()) {
            m_dictWhole = gunzip(m_dictPath, MaxWholeDictBytes);
            if (m_dictWhole.isNull()) {
                m_problem = QStringLiteral("%1 can't be read, or is damaged.").arg(m_dictPath);
                m_dictWhole = QByteArray("");
            }
        }
        if (offset >= quint64(m_dictWhole.size()))
            return {};
        return m_dictWhole.mid(qsizetype(offset), size);
    }
    QFile file(m_dictPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    if (!m_dictzip) {
        if (offset >= quint64(file.size()) || !file.seek(qint64(offset)))
            return {};
        return file.read(size);
    }
    const quint64 first = offset / m_chunkLength, last = (offset + size - 1) / m_chunkLength;
    if (last >= m_chunkSizes.size())
        return {};
    QByteArray unpacked;
    for (quint64 chunk = first; chunk <= last; ++chunk) {
        if (!file.seek(qint64(m_chunkOffsets[chunk])))
            return {};
        const QByteArray packed = file.read(m_chunkSizes[chunk]);
        const QByteArray plain = inflateData(packed, -15, m_chunkLength);
        // Every chunk but the file's last is full; a short one in the
        // middle would put everything after it in the wrong place.
        if (plain.isEmpty() || (chunk < last && quint32(plain.size()) != m_chunkLength))
            return {};
        unpacked += plain;
    }
    return unpacked.mid(qsizetype(offset - first * m_chunkLength), size);
}

QList<int> StarDict::find(const QByteArray &index, const std::vector<quint32> &starts, const std::vector<quint32> &order,
                          const QByteArray &word) const {
    const char *data = index.constData();
    const char *key = word.constData();
    const auto lower = std::lower_bound(order.begin(), order.end(), key, [&](quint32 entry, const char *wanted) {
        return compareFolded(data + starts[entry], wanted) < 0;
    });
    QList<int> found;
    for (auto it = lower; it != order.end() && compareFolded(data + starts[*it], key) == 0; ++it)
        found << int(*it);
    return found;
}

QList<StarDict::Article> StarDict::lookup(const QString &word) {
    if (!m_loaded)
        load();
    if (m_idxStarts.empty() || word.isEmpty())
        return {};
    // The index folds ASCII case itself; a word with other letters is also
    // tried as a dictionary would most likely head it.
    QList<QByteArray> spellings;
    const QString lower = word.toLower();
    for (const QString &spelling : {word, lower, lower.left(1).toUpper() + lower.mid(1), word.toUpper()})
        if (!spellings.contains(spelling.toUtf8()))
            spellings << spelling.toUtf8();

    QList<int> entries;
    for (const QByteArray &spelling : std::as_const(spellings)) {
        // A NUL in the word would end the comparison early.
        if (spelling.contains('\0'))
            continue;
        for (const int entry : find(m_idx, m_idxStarts, m_idxOrder, spelling))
            if (!entries.contains(entry))
                entries << entry;
        for (const int synonym : find(m_syn, m_synStarts, m_synOrder, spelling)) {
            const char *start = m_syn.constData() + m_synStarts[size_t(synonym)];
            const quint32 entry = qFromBigEndian<quint32>(start + std::strlen(start) + 1);
            if (entry < m_idxStarts.size() && !entries.contains(int(entry)))
                entries << int(entry);
        }
    }

    QList<Article> articles;
    for (const int entry : std::as_const(entries)) {
        if (articles.size() >= MaxArticlesPerWord)
            break;
        const char *start = m_idx.constData() + m_idxStarts[size_t(entry)];
        const char *numbers = start + std::strlen(start) + 1;
        const quint64 offset = m_offsetBytes == 8 ? qFromBigEndian<quint64>(numbers) : qFromBigEndian<quint32>(numbers);
        const quint32 size = qFromBigEndian<quint32>(numbers + m_offsetBytes);
        const QString text = plainText(article(offset, size), m_types);
        if (!text.isEmpty())
            articles << Article{QString::fromUtf8(start), text};
    }
    return articles;
}

QString StarDict::plainText(const QByteArray &data, const QByteArray &types) {
    QStringList parts;
    const auto add = [&](char type, const QByteArray &field) {
        QString text;
        switch (type) {
        case 'm': case 'l': case 'n': case 'w': case 'y':
            text = QString::fromUtf8(field);
            break;
        case 't':
            if (!field.trimmed().isEmpty())
                text = "/" + QString::fromUtf8(field).trimmed() + "/";
            break;
        case 'h':
            text = QTextDocumentFragment::fromHtml(QString::fromUtf8(field)).toPlainText();
            break;
        case 'x': {
            // XDXF repeats the headword in <k>; the card already shows it.
            static const QRegularExpression key("<k>.*?</k>", QRegularExpression::DotMatchesEverythingOption);
            text = fromMarkup(QString::fromUtf8(field).remove(key));
            break;
        }
        case 'g': case 'k':
            text = fromMarkup(QString::fromUtf8(field));
            break;
        default:
            // Sounds, pictures and resource lists have nothing to read.
            break;
        }
        text = tidy(text);
        if (!text.isEmpty())
            parts << text;
    };

    const char *position = data.constData();
    const char *const end = position + data.size();
    // A field whose type is a capital letter says its own size; any other
    // ends at a NUL. With the types given up front, the last one runs to
    // the end of the article instead.
    const auto field = [&](char type, bool last) {
        if (last) {
            add(type, QByteArray(position, end - position));
            position = end;
        } else if (type >= 'A' && type <= 'Z') {
            if (end - position < 4) {
                position = end;
                return;
            }
            const quint32 size = qFromBigEndian<quint32>(position);
            position += 4;
            const qsizetype length = qMin<qsizetype>(size, end - position);
            add(type, QByteArray(position, length));
            position += length;
        } else {
            const void *nul = std::memchr(position, 0, size_t(end - position));
            const char *stop = nul ? static_cast<const char *>(nul) : end;
            add(type, QByteArray(position, stop - position));
            position = nul ? stop + 1 : end;
        }
    };
    if (!types.isEmpty()) {
        for (qsizetype i = 0; i < types.size() && position < end; ++i)
            field(types[i], i == types.size() - 1);
    } else {
        while (position < end) {
            const char type = *position++;
            field(type, false);
        }
    }
    return tidy(parts.join("\n"));
}

Dictionaries::Dictionaries(const QStringList &directories) : m_directories(directories) {
    if (m_directories.isEmpty())
        m_directories = {userDirectory(), QDir::homePath() + "/.stardict/dic", "/usr/share/stardict/dic"};
}

Dictionaries::~Dictionaries() = default;

QString Dictionaries::userDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/nell/dictionaries";
}

// Looks for dictionaries again, so one dropped in while Nell is open is
// found by the next lookup. Those already open stay open, index and all.
void Dictionaries::scan() {
    QStringList found;
    QHash<QString, QString> roots;
    for (const QString &directory : std::as_const(m_directories)) {
        QStringList here;
        QDirIterator it(directory, {"*.ifo"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            here << it.next();
        here.sort();
        for (const QString &path : std::as_const(here))
            if (!roots.contains(path)) {
                roots.insert(path, directory);
                found << path;
            }
    }
    if (found == m_scanned)
        return;
    std::vector<std::unique_ptr<StarDict>> dictionaries;
    for (const QString &path : std::as_const(found)) {
        auto existing = std::find_if(m_dictionaries.begin(), m_dictionaries.end(),
                                     [&](const std::unique_ptr<StarDict> &d) { return d && d->path() == path; });
        if (existing != m_dictionaries.end()) {
            dictionaries.push_back(std::move(*existing));
            continue;
        }
        QString error;
        if (auto opened = StarDict::open(path, &error)) {
            opened->setLanguage(languageOf(QDir(roots.value(path)).relativeFilePath(path)));
            dictionaries.push_back(std::move(opened));
        } else
            qWarning("nell: %s", qPrintable(error));
    }
    m_dictionaries = std::move(dictionaries);
    m_scanned = found;
}

QLocale::Language Dictionaries::languageOf(const QString &relativeIfoPath) {
    QStringList parts = relativeIfoPath.split('/', Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return QLocale::AnyLanguage;
    parts.last() = QFileInfo(parts.last()).completeBaseName();
    const auto language = [](const QString &code) {
        const QLocale::Language found = QLocale::codeToLanguage(code.toLower());
        return found == QLocale::C ? QLocale::AnyLanguage : found;
    };
    static const QRegularExpression pair("^([A-Za-z]{2,3})-([A-Za-z]{2,3})$");
    for (qsizetype i = parts.size() - 1; i >= 0; --i) {
        const auto match = pair.match(parts[i]);
        if (match.hasMatch() && language(match.captured(1)) != QLocale::AnyLanguage &&
            language(match.captured(2)) != QLocale::AnyLanguage)
            return language(match.captured(1));
        // A folder, and only a two-letter one: three letters spell too
        // many ordinary names ("new" is Newari's code).
        if (i < parts.size() - 1 && parts[i].size() == 2 && language(parts[i]) != QLocale::AnyLanguage)
            return language(parts[i]);
    }
    return QLocale::AnyLanguage;
}

QStringList Dictionaries::names() {
    scan();
    QStringList names;
    for (const auto &dictionary : m_dictionaries)
        names << dictionary->name();
    return names;
}

QList<Dictionaries::Hit> Dictionaries::lookup(const QString &word, const QString &language) {
    scan();
    QLocale::Language wanted = QLocale::codeToLanguage(language.section(QRegularExpression("[-_]"), 0, 0).toLower());
    if (wanted == QLocale::C)
        wanted = QLocale::AnyLanguage;
    const auto in = [&](const QString &spelling) {
        QList<Hit> hits;
        for (const auto &dictionary : m_dictionaries) {
            if (wanted != QLocale::AnyLanguage && dictionary->language() != QLocale::AnyLanguage &&
                dictionary->language() != wanted)
                continue;
            for (const auto &article : dictionary->lookup(spelling))
                hits << Hit{dictionary->name(), article.headword, article.text};
        }
        return hits;
    };
    QList<Hit> hits = in(word);
    if (hits.isEmpty() && (language.isEmpty() || language.startsWith("en", Qt::CaseInsensitive)))
        for (const QString &form : baseForms(word)) {
            hits = in(form);
            if (!hits.isEmpty())
                break;
        }
    return hits;
}

QStringList Dictionaries::baseForms(const QString &word) {
    const QString lower = word.toLower();
    QStringList forms;
    const auto add = [&](const QString &form) {
        if (form.size() >= 2 && form != lower && !forms.contains(form))
            forms << form;
    };
    const auto stem = [&](int ending) { return lower.left(lower.size() - ending); };
    // "stopped", "running": the consonant doubled before the ending.
    const auto doubled = [&](int ending) {
        const QString rest = stem(ending);
        return rest.size() >= 3 && rest.at(rest.size() - 1) == rest.at(rest.size() - 2) &&
               !QStringLiteral("aeiou").contains(rest.at(rest.size() - 1));
    };
    if (lower.endsWith("ies"))
        add(stem(3) + "y");
    if (lower.endsWith("ves")) {
        add(stem(3) + "f");
        add(stem(3) + "fe");
    }
    if (lower.endsWith("es"))
        add(stem(2));
    if (lower.endsWith("s") && !lower.endsWith("ss"))
        add(stem(1));
    if (lower.endsWith("ied"))
        add(stem(3) + "y");
    if (lower.endsWith("ed")) {
        add(stem(2));
        add(stem(1));
        if (doubled(2))
            add(stem(3));
    }
    if (lower.endsWith("ing")) {
        add(stem(3));
        add(stem(3) + "e");
        if (doubled(3))
            add(stem(4));
    }
    if (lower.endsWith("iest"))
        add(stem(4) + "y");
    if (lower.endsWith("ier") || lower.endsWith("ily"))
        add(stem(3) + "y");
    if (lower.endsWith("est")) {
        add(stem(3));
        add(stem(2));
        if (doubled(3))
            add(stem(4));
    }
    if (lower.endsWith("er")) {
        add(stem(2));
        add(stem(1));
        if (doubled(2))
            add(stem(3));
    }
    if (lower.endsWith("ly"))
        add(stem(2));
    return forms;
}
