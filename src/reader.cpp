#include "reader.h"

#include "library.h"
#include "preferences.h"

#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QGuiApplication>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextObjectInterface>
#include <QUrl>
#include <QUuid>
#include <cmath>
#include <poppler-qt6.h>

// A chapter of an EPUB as Qt's rich text document. It reads stylesheets
// itself, so the reader's font, size and colors win over the publisher's
// (colors only in a theme other than Original, which keeps the book's own),
// and never loads anything from outside the unpacked book.
class BookDocument : public QTextDocument {
public:
    BookDocument(const QString &path, const QString &root, bool keepFonts, bool keepColors)
        : m_root(root), m_keepFonts(keepFonts), m_keepColors(keepColors) {
        setBaseUrl(QUrl::fromLocalFile(path));
        setDocumentMargin(0);
        setUndoRedoEnabled(false);
    }

    // Images are drawn by the Reader's image handler, from a bounded cache,
    // so an illustrated book doesn't hold every picture decoded in memory.
    QString imagePath(const QString &name) const {
        const QUrl url = QUrl(name).isRelative() ? baseUrl().resolved(QUrl(name)) : QUrl(name);
        const QString path = QDir::cleanPath(url.toLocalFile());
        return path.startsWith(m_root + "/") ? path : QString();
    }

protected:
    QVariant loadResource(int type, const QUrl &name) override {
        if (type != StyleSheetResource)
            return {};
        QFile file(imagePath(name.toString()));
        if (file.fileName().isEmpty() || !file.open(QIODevice::ReadOnly))
            return QString();
        QString css = QString::fromUtf8(file.readAll());
        // Line spacing is always the reader's, and so are colors, except in
        // the Original theme. Typefaces and fixed type sizes are too, unless
        // they chose the publisher's font.
        static const QRegularExpression always(R"((?<![-\w])line-height\s*:[^;}]*;?)",
                                               QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression colors(R"((?<![-\w])(color|background(-color|-image)?)\s*:[^;}]*;?)",
                                               QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression fonts(R"((?<![-\w])(font-family|font-size\s*:\s*[\d.]+\s*(px|pt)|font-size\s*:\s*(x+-)?(small|medium|large))\s*[^;}]*;?)",
                                              QRegularExpression::CaseInsensitiveOption);
        css.remove(always);
        if (!m_keepColors)
            css.remove(colors);
        if (!m_keepFonts)
            css.remove(fonts);
        return css;
    }

private:
    QString m_root;
    bool m_keepFonts;
    bool m_keepColors;
};

class ImageHandler : public QObject, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)

public:
    explicit ImageHandler(QCache<QString, QImage> *cache, QObject *parent) : QObject(parent), m_cache(cache) {}

    QSizeF intrinsicSize(QTextDocument *, int, const QTextFormat &format) override {
        const auto image = format.toImageFormat();
        return {image.width(), image.height()};
    }

    void drawObject(QPainter *painter, const QRectF &rect, QTextDocument *document, int,
                    const QTextFormat &format) override {
        const QString path = static_cast<BookDocument *>(document)->imagePath(format.toImageFormat().name());
        const qreal ratio = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        const QSize size = (rect.size() * ratio).toSize();
        if (path.isEmpty() || size.isEmpty())
            return;
        const QString key = QStringLiteral("%1@%2x%3").arg(path).arg(size.width()).arg(size.height());
        if (!m_cache->contains(key)) {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            reader.setScaledSize(size);
            const QImage image = reader.read();
            if (image.isNull())
                return;
            m_cache->insert(key, new QImage(image), qMax<qsizetype>(1, image.sizeInBytes() / 1024));
        }
        painter->drawImage(rect, *m_cache->object(key));
    }

private:
    QCache<QString, QImage> *m_cache;
};

namespace {
QColor highlightColor(const QString &name) {
    if (name == QLatin1String("green")) return QColor("#5fd068");
    if (name == QLatin1String("blue")) return QColor("#5ab4fa");
    if (name == QLatin1String("pink")) return QColor("#ff6fa5");
    if (name == QLatin1String("purple")) return QColor("#b38cff");
    if (name == QLatin1String("underline")) return QColor("#ff453a");
    return QColor("#ffd60a");
}

QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); }

QString plain(QString text) {
    text.replace(QChar::ParagraphSeparator, '\n').replace(QChar::LineSeparator, '\n');
    text.remove(QChar::ObjectReplacementCharacter);
    return text.trimmed();
}

// The parts of an EPUB chapter Qt's rich text can't show: an SVG wrapped
// around a cover image is the common one.
QString prepareHtml(QString html) {
    static const QRegularExpression svgImage(
        R"(<svg\b[^>]*>.*?<image\b[^>]*?(?:xlink:)?href\s*=\s*["']([^"']+)["'][^>]*>.*?</svg>)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    html.replace(svgImage, R"(<img src="\1"/>)");
    return html;
}

constexpr int OriginalAlignment = QTextFormat::UserProperty;
constexpr int OriginalWidth = QTextFormat::UserProperty + 1;
}

Reader::Reader(Library *library, Preferences *preferences, QObject *parent)
    : QObject(parent), m_library(library), m_preferences(preferences),
      m_imageHandler(new ImageHandler(&m_bookImages, this)) {
    m_pdfImages.setMaxCost(160 * 1024);
    m_bookImages.setMaxCost(160 * 1024);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(800);
    connect(&m_saveTimer, &QTimer::timeout, this, &Reader::save);
    m_layoutTimer.setSingleShot(true);
    m_layoutTimer.setInterval(40);
    connect(&m_layoutTimer, &QTimer::timeout, this, &Reader::paginate);
    connect(m_preferences, &Preferences::layoutChanged, this, [this] {
        if (!isOpen())
            return;
        // Where a page's content is depends on whether running heads count.
        m_pdfContentCache.clear();
        if (m_format == QLatin1String("epub") && m_keepFonts != m_preferences->fontFamily().isEmpty())
            loadChapters();
        styleChapters();
        paginate();
    });
    connect(m_preferences, &Preferences::colorsChanged, this, [this] {
        m_pdfImages.clear();
        // Into or out of Original, the publisher's colors come or go, which
        // means reading the chapters again.
        if (isOpen() && m_format == QLatin1String("epub") && m_keepColors != keepColors()) {
            loadChapters();
            styleChapters();
            paginate();
        }
        emit repaint();
    });
}

Reader::~Reader() { close(); }

void Reader::setError(const QString &message) {
    if (message == m_error)
        return;
    m_error = message;
    emit errorChanged();
}

bool Reader::openBook(const QString &id) {
    close();
    const QVariantMap book = m_library->book(id);
    if (book.isEmpty()) {
        setError(tr("That book isn't in the library."));
        return false;
    }
    m_path = book["path"].toString();
    if (!QFileInfo::exists(m_path)) {
        setError(tr("%1 has moved or been deleted. Add it again from where it is now.").arg(m_path));
        return false;
    }
    // A file changed in place is a new revision: its notes start afresh,
    // and the old ones stay with the old entry rather than landing on text
    // they weren't made on.
    const QString current = m_library->checkFile(id, !m_readOnly);
    if (current.isEmpty()) {
        setError(tr("%1 can't be read.").arg(m_path));
        return false;
    }
    if (current != id) {
        if (m_readOnly) {
            setError(tr("%1 has changed since its notes were made.").arg(m_path));
            return false;
        }
        const QString path = m_path;
        const QString revision = m_library->add(path);
        if (revision.isEmpty()) {
            setError(m_library->error());
            return false;
        }
        m_library->markRevision(id, revision);
        if (!openBook(revision))
            return false;
        emit fileChanged(id, revision);
        return true;
    }
    m_format = book["format"].toString();
    m_title = book["title"].toString();
    m_author = book["author"].toString();
    m_language = book["language"].toString();
    m_previousRevision = book["revisionOf"].toString();
    const bool loaded = m_format == QLatin1String("pdf") ? loadPdf(m_path) : loadEpub(m_library->cacheDirectory(id), m_library->book(id)["sha256"].toString());
    if (!loaded) {
        m_chapters.clear();
        m_pdf.reset();
        return false;
    }
    setError({});
    m_id = id;

    Store store = m_library->annotations(id);
    m_notesLocked = !store.writable();
    if (m_notesLocked)
        m_library->reportStorageProblem(store.problem);
    m_savedAnnotations = store.object;
    const QJsonObject saved = m_unsaved.contains(id) && !m_notesLocked ? m_unsaved[id].first : store.object;
    if (m_unsaved.contains(id) && !m_notesLocked)
        m_savedAnnotations = m_unsaved[id].second;
    m_bookmarks = saved["bookmarks"].toArray();
    m_highlights = saved["highlights"].toArray();
    m_vocabulary = saved["vocabulary"].toArray();
    const QJsonObject position = saved["position"].toObject();
    m_chapter = qBound(0, position["chapter"].toInt(), qMax(0, int(m_chapterPaths.size()) - 1));
    m_offset = qMax(0, position["offset"].toInt());
    m_fitTextChoice = saved.contains("fitText") ? int(saved["fitText"].toBool()) : -1;
    m_undo.clear();
    m_redo.clear();
    m_pageTimer.start();

    emit openChanged();
    emit annotationsChanged();
    paginate();
    if (!m_readOnly)
        m_library->touch(m_id, progress());
    return true;
}

bool Reader::loadEpub(const QString &directory, const QString &digest) {
    QString error;
    Epub epub;
    if (!Epub::extract(m_path, directory, &error, digest) || !epub.load(directory, &error)) {
        setError(error);
        return false;
    }
    m_chapterPaths = epub.spine;
    m_root = QDir::cleanPath(directory);
    loadChapters();
    for (const auto &entry : epub.toc) {
        const int hash = entry.target.indexOf('#');
        const int chapter = m_chapterPaths.indexOf(hash < 0 ? entry.target : entry.target.left(hash));
        if (chapter < 0)
            continue;
        const int offset = hash < 0 ? 0 : m_anchors[chapter].value(entry.target.mid(hash + 1), 0);
        m_toc << TocTarget{entry.title, entry.level, chapter, offset};
    }
    styleChapters();
    return true;
}

void Reader::loadChapters() {
    m_keepFonts = m_preferences->fontFamily().isEmpty();
    m_keepColors = keepColors();
    m_chapters.clear();
    m_anchors.clear();
    m_charactersBefore.clear();
    m_characterCount = 0;
    m_bookImages.clear();
    for (const auto &path : m_chapterPaths) {
        auto document = std::make_unique<BookDocument>(path, m_root, m_keepFonts, m_keepColors);
        document->documentLayout()->registerHandler(QTextFormat::ImageObject, m_imageHandler);
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
            document->setHtml(prepareHtml(QString::fromUtf8(file.readAll())));

        // Colors belong to the reading theme, so drop the publisher's unless
        // it's Original, and collect every anchor a link or the table of
        // contents can point at.
        QHash<QString, int> anchors;
        QTextCursor cursor(document.get());
        cursor.beginEditBlock();
        for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
            QTextBlockFormat blockFormat = block.blockFormat();
            if (!m_keepColors && blockFormat.hasProperty(QTextFormat::BackgroundBrush)) {
                blockFormat.clearBackground();
                cursor.setPosition(block.position());
                cursor.setBlockFormat(blockFormat);
            }
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                QTextCharFormat format = fragment.charFormat();
                for (const auto &name : format.anchorNames())
                    anchors.insert(name, fragment.position());
                if (!m_keepColors &&
                    (format.hasProperty(QTextFormat::ForegroundBrush) || format.hasProperty(QTextFormat::BackgroundBrush))) {
                    format.clearForeground();
                    format.clearBackground();
                    cursor.setPosition(fragment.position());
                    cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
                    cursor.setCharFormat(format);
                }
            }
        }
        cursor.endEditBlock();
        m_anchors << anchors;
        m_charactersBefore << m_characterCount;
        m_characterCount += document->characterCount();
        m_chapters.push_back(std::move(document));
    }
}

bool Reader::keepColors() const { return m_preferences->readingTheme() == QLatin1String("original"); }

void Reader::styleChapters() {
    if (m_format != QLatin1String("epub"))
        return;
    QFont font(m_preferences->fontFamily().isEmpty() ? m_preferences->fonts().value(0, "serif") : m_preferences->fontFamily());
    font.setPixelSize(m_preferences->fontSize());
    font.setHintingPreference(QFont::PreferNoHinting);
    const bool justify = m_preferences->justify();
    const double spacing = m_preferences->lineSpacing() * 100;
    const QSizeF page = m_pageSize.isEmpty() ? QSizeF(600, 800) : m_pageSize;

    for (auto &document : m_chapters) {
        document->setDefaultFont(font);
        QTextCursor cursor(document.get());
        cursor.beginEditBlock();
        for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
            QTextBlockFormat format = block.blockFormat();
            if (!format.hasProperty(OriginalAlignment))
                format.setProperty(OriginalAlignment, int(format.alignment()));
            const auto original = Qt::Alignment(format.intProperty(OriginalAlignment));
            const bool flowing = !(original & (Qt::AlignHCenter | Qt::AlignRight));
            const bool imageOnly = block.length() == 2 && block.text().at(0) == QChar::ObjectReplacementCharacter;
            if (imageOnly)
                format.setAlignment(Qt::AlignHCenter);
            else if (flowing)
                format.setAlignment(justify ? Qt::AlignJustify : Qt::AlignLeft);
            // Proportional spacing would stretch a picture's line as well.
            if (imageOnly)
                format.setLineHeight(100, QTextBlockFormat::SingleHeight);
            else
                format.setLineHeight(spacing, QTextBlockFormat::ProportionalHeight);
            cursor.setPosition(block.position());
            cursor.setBlockFormat(format);

            // Pictures fit the page, and one alone on its page (a cover, a
            // plate) fills it.
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                if (!fragment.charFormat().isImageFormat())
                    continue;
                QTextImageFormat image = fragment.charFormat().toImageFormat();
                const QString path = document->imagePath(image.name());
                if (!m_imageSizes.contains(path))
                    m_imageSizes.insert(path, QImageReader(path).size());
                QSizeF natural = m_imageSizes.value(path);
                if (natural.isEmpty())
                    continue;
                if (!image.hasProperty(OriginalWidth))
                    image.setProperty(OriginalWidth, image.width());
                const double wanted = image.doubleProperty(OriginalWidth);
                if (wanted > 0)
                    natural = QSizeF(wanted, natural.height() * wanted / natural.width());
                const double room = page.height() * 0.97;
                double scale = qMin(page.width() / natural.width(), room / natural.height());
                if (!imageOnly)
                    scale = qMin(scale, 1.0);
                image.setWidth(std::floor(natural.width() * scale));
                image.setHeight(std::floor(natural.height() * scale));
                cursor.setPosition(fragment.position());
                cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
                cursor.setCharFormat(image);
            }
        }
        cursor.endEditBlock();
    }
}

bool Reader::loadPdf(const QString &path) {
    m_pdf = Poppler::Document::load(path);
    if (!m_pdf || m_pdf->isLocked()) {
        setError(m_pdf ? tr("This PDF is password protected.") : tr("This PDF is damaged or not a PDF."));
        return false;
    }
    m_pdf->setRenderHint(Poppler::Document::Antialiasing);
    m_pdf->setRenderHint(Poppler::Document::TextAntialiasing);
    m_pdf->setRenderHint(Poppler::Document::TextHinting);
    std::function<void(const QList<Poppler::OutlineItem> &, int)> walk = [&](const QList<Poppler::OutlineItem> &items, int level) {
        for (const auto &item : items) {
            const auto destination = item.destination();
            if (destination && destination->pageNumber() > 0)
                m_toc << TocTarget{item.name().simplified(), level, destination->pageNumber() - 1, 0};
            walk(item.children(), level + 1);
        }
    };
    walk(m_pdf->outline(), 0);
    for (int i = 0; i < m_pdf->numPages(); ++i)
        m_chapterPaths << QString::number(i);
    measurePdf();
    return true;
}

// The body size is the height most of the text is set at, by characters, so
// headings and footnotes don't sway it. Poppler's word boxes are the font's
// full height, which is its size or a little more. Ten pages are plenty and
// keep opening a long PDF quick.
//
// Running heads and feet are words near the top or bottom edge that recur in
// the same place on most pages; digits are ignored so page numbers count.
namespace {
// A word as a running head or foot would recur: its place down the page and
// its text, digits left out so page numbers match.
QString runningKey(const QRectF &box, const QString &text) {
    static const QRegularExpression digits("\\d");
    return QString("%1:%2").arg(qRound(box.top())).arg(QString(text).remove(digits));
}
}

void Reader::measurePdf() {
    QHash<int, qint64> characters;
    QHash<QString, int> pagesWith;
    const int measured = qMin(10, m_pdf->numPages());
    const double pageHeight = measured ? m_pdf->page(0)->pageSizeF().height() : 0;
    for (int page = 0; page < measured; ++page) {
        QSet<QString> seen;
        for (const auto &word : pdfWords(page)) {
            characters[qRound(word.box.height() * 2)] += word.text.size();
            if (word.box.bottom() < pageHeight * 0.15 || word.box.top() > pageHeight * 0.85)
                seen.insert(runningKey(word.box, word.text));
        }
        for (const auto &found : seen)
            ++pagesWith[found];
    }
    int best = 0;
    for (auto it = characters.begin(); it != characters.end(); ++it)
        if (it.key() > 0 && (best == 0 || it.value() > characters[best]))
            best = it.key();
    m_pdfBodySize = best / 2.0;

    m_pdfRunning.clear();
    m_pdfHeadCut = 0;
    m_pdfFootCut = 1e9;
    if (measured < 3)
        return;
    for (int page = 0; page < measured; ++page)
        for (const auto &word : pdfWords(page)) {
            const QString key = runningKey(word.box, word.text);
            if (pagesWith.value(key) < qMax(3, measured * 6 / 10))
                continue;
            m_pdfRunning.insert(key);
            if (word.box.bottom() < pageHeight * 0.15)
                m_pdfHeadCut = qMax(m_pdfHeadCut, word.box.bottom() + word.box.height() * 1.5);
            else if (word.box.top() > pageHeight * 0.85)
                m_pdfFootCut = qMin(m_pdfFootCut, word.box.top() - word.box.height() * 1.5);
        }
}

void Reader::close() {
    if (!isOpen())
        return;
    save();
    m_id.clear();
    m_chapters.clear();
    m_chapterPaths.clear();
    m_anchors.clear();
    m_toc.clear();
    m_pageStart.clear();
    m_imageSizes.clear();
    m_bookImages.clear();
    m_pdf.reset();
    m_pdfWordCache.clear();
    m_pdfImages.clear();
    m_pdfSearchHits.clear();
    m_pdfContentCache.clear();
    m_sliceCache.clear();
    m_pdfBodySize = 0;
    m_pdfRunning.clear();
    m_pdfHeadCut = 0;
    m_pdfFootCut = 1e9;
    m_fitTextChoice = -1;
    if (m_fitText) {
        m_fitText = false;
        applyColumns();
        emit fitTextChanged();
    }
    m_slice = 0;
    m_bookmarks = m_highlights = m_vocabulary = QJsonArray();
    m_savedAnnotations = QJsonObject();
    m_notesLocked = false;
    m_previousRevision.clear();
    m_searchResults.clear();
    m_searchChapter = m_searchStart = m_searchEnd = -1;
    m_selectionChapter = m_selectionStart = m_selectionEnd = -1;
    m_selectedHighlight.clear();
    m_page = m_chapter = m_offset = 0;
    emit openChanged();
    emit layoutChanged();
    emit positionChanged();
    emit annotationsChanged();
    emit selectionChanged();
    emit searchChanged();
}

void Reader::setGeometry(double width, double height, int columns, double gap) {
    const QSizeF area(width, height);
    columns = qMax(1, columns);
    if (area == m_area && columns == m_requestedColumns && qFuzzyCompare(gap + 1, m_gap + 1))
        return;
    const bool first = m_area.isEmpty();
    m_area = area;
    m_requestedColumns = columns;
    m_gap = gap;
    applyColumns();
    // Resizing a window sends a stream of sizes; paginate once it settles.
    if (first)
        paginate();
    else
        m_layoutTimer.start();
}

void Reader::applyColumns() {
    m_columns = m_fitText ? 1 : m_requestedColumns;
    m_pageSize = QSizeF((m_area.width() - m_gap * (m_columns - 1)) / m_columns, m_area.height());
}

void Reader::paginate() {
    if (!isOpen() || m_pageSize.width() < 50 || m_pageSize.height() < 50) {
        emit layoutChanged();
        return;
    }
    m_pageStart.clear();
    if (m_format == QLatin1String("epub")) {
        styleChapters();
        int total = 0;
        for (auto &document : m_chapters) {
            m_pageStart << total;
            document->setPageSize(m_pageSize);
            total += qMax(1, document->pageCount());
        }
        m_pageStart << total;
    } else {
        m_pdfImages.clear();
        m_sliceCache.clear();
        const bool fit = m_fitTextChoice < 0 ? smallPrint() : m_fitTextChoice == 1;
        if (fit != m_fitText) {
            m_fitText = fit;
            QMetaObject::invokeMethod(this, &Reader::fitTextChanged, Qt::QueuedConnection);
        }
        applyColumns();
    }
    emit layoutChanged();
    setPage(pageOf(m_chapter, m_offset), true);
}

int Reader::pageCount() const {
    if (m_pdf)
        return m_pdf->numPages();
    return m_pageStart.isEmpty() ? 0 : m_pageStart.last();
}

int Reader::chapterOfPage(int page) const {
    if (m_pdf)
        return page;
    if (m_pageStart.size() < 2)
        return 0;
    const auto it = std::upper_bound(m_pageStart.begin(), m_pageStart.end() - 1, page);
    return qMax(0, int(it - m_pageStart.begin()) - 1);
}

int Reader::pageOf(int chapter, int offset) const {
    if (m_pdf)
        return qBound(0, chapter, qMax(0, pageCount() - 1));
    if (chapter < 0 || chapter >= int(m_chapters.size()) || m_pageStart.size() < 2)
        return 0;
    const auto *document = m_chapters[chapter].get();
    const QTextBlock block = document->findBlock(offset);
    int local = 0;
    if (block.isValid()) {
        const QRectF rect = document->documentLayout()->blockBoundingRect(block);
        const QTextLine line = block.layout()->lineForTextPosition(offset - block.position());
        const double y = rect.top() + (line.isValid() ? line.y() : 0);
        local = int(std::floor((y + 0.5) / m_pageSize.height()));
    }
    const int pages = m_pageStart[chapter + 1] - m_pageStart[chapter];
    return m_pageStart[chapter] + qBound(0, local, pages - 1);
}

int Reader::firstPositionOnPage(int page) const {
    if (m_pdf)
        return 0;
    const int chapter = chapterOfPage(page);
    if (chapter >= int(m_chapters.size()))
        return 0;
    const auto *document = m_chapters[chapter].get();
    const double top = (page - m_pageStart[chapter]) * m_pageSize.height();
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        const QRectF rect = document->documentLayout()->blockBoundingRect(block);
        if (rect.bottom() <= top + 0.5)
            continue;
        const QTextLayout *layout = block.layout();
        for (int i = 0; i < layout->lineCount(); ++i) {
            const QTextLine line = layout->lineAt(i);
            if (rect.top() + line.y() >= top - 0.5)
                return block.position() + line.textStart();
        }
    }
    return qMax(0, document->characterCount() - 1);
}

void Reader::setPage(int page, bool keepPlace) {
    const int count = pageCount();
    if (count == 0)
        return;
    page = qBound(0, page, count - 1);
    page -= page % m_columns;
    m_page = page;
    // After a relayout the place is kept as it was, so text never drifts
    // when the reader tries a few sizes; a page turn moves it.
    if (!keepPlace) {
        m_chapter = chapterOfPage(page);
        m_offset = m_fitText ? firstWordInSlice(page, 0) : firstPositionOnPage(page);
    }
    m_slice = m_fitText && keepPlace ? sliceOf(page, m_offset) : 0;
    emit positionChanged();
    emit annotationsChanged();
    emit repaint();
    scheduleSave();
}

void Reader::next() {
    if (!isOpen() || atEnd())
        return;
    trackReading(m_page);
    if (m_slice + 1 < sliceCount()) {
        setSlice(m_slice + 1);
        return;
    }
    setPage(m_page + m_columns, false);
}

void Reader::previous() {
    if (!isOpen() || (m_page == 0 && m_slice == 0))
        return;
    m_pageTimer.restart();
    if (m_slice > 0) {
        setSlice(m_slice - 1);
        return;
    }
    setPage(m_page - m_columns, false);
    // Back from the top of a page lands at the foot of the one before, as
    // scrolling up would.
    if (m_fitText)
        setSlice(sliceCount() - 1);
}

void Reader::setSlice(int slice) {
    m_slice = qBound(0, slice, sliceCount() - 1);
    m_offset = firstWordInSlice(m_page, m_slice);
    emit positionChanged();
    emit annotationsChanged();
    emit repaint();
    scheduleSave();
}

int Reader::sliceCount() const {
    if (!m_fitText || !m_pdf || m_page >= pageCount())
        return 1;
    return int(slices(m_page).size());
}

bool Reader::smallPrint() const {
    return m_pdf && m_pdfBodySize > 0 && !m_area.isEmpty() && m_pdfBodySize * pageFitScale() < SmallPrintPixels;
}

void Reader::setFitText(bool fit) { setFitTextMode(fit ? "on" : "off"); }

QString Reader::fitTextMode() const {
    return m_fitTextChoice < 0 ? QStringLiteral("auto") : m_fitTextChoice == 1 ? QStringLiteral("on") : QStringLiteral("off");
}

void Reader::setFitTextMode(const QString &mode) {
    const int choice = mode == QLatin1String("on") ? 1 : mode == QLatin1String("off") ? 0 : mode == QLatin1String("auto") ? -1 : -2;
    if (!m_pdf || choice == -2 || choice == m_fitTextChoice)
        return;
    m_fitTextChoice = choice;
    const bool before = m_fitText;
    paginate();
    // paginate() announces a change of fit text itself; a change of mode
    // alone still needs saying.
    if (before == m_fitText)
        QMetaObject::invokeMethod(this, &Reader::fitTextChanged, Qt::QueuedConnection);
    scheduleSave();
}

// How big a whole page is drawn when it fits one column of the view, as the
// pages are drawn without fit text: in a spread that's half the width. The
// first page stands for the book, so the answer doesn't change from page to
// page.
double Reader::pageFitScale() const {
    const QSizeF points = m_pdf->page(0)->pageSizeF();
    const int columns = qMax(1, m_requestedColumns);
    const double width = (m_area.width() - m_gap * (columns - 1)) / columns;
    return qMin(width / points.width(), m_area.height() / points.height());
}

// Body text at the reader's font size, unless the page's content would then
// be wider than the window; and never smaller than the whole page would be.
double Reader::fitScale(int page) const {
    const double target = qMax(m_preferences->fontSize() / m_pdfBodySize, pageFitScale());
    return qMin(target, m_pageSize.width() / qMax(1.0, pdfContent(page).width()));
}

// Where the ink is: a coarse render finds pictures and rules as well as text,
// so cropping the margins never cuts into a figure.
QRectF Reader::pdfContent(int page) const {
    auto it = m_pdfContentCache.find(page);
    if (it != m_pdfContentCache.end())
        return *it;
    const auto pdfPage = m_pdf->page(page);
    const QRectF whole(QPointF(0, 0), pdfPage ? pdfPage->pageSizeF() : QSizeF(1, 1));
    // Running heads and feet are left out, and so is ink beside them, but
    // never so far that a word of the page's own text is lost. A word is
    // only left out if it is one of the recurring ones, where it recurs, so
    // a heading that happens to sit high on its page stays.
    const bool hide = m_preferences->hideRunningHeads();
    QList<Word> body;
    for (const auto &word : pdfWords(page))
        if (!hide || !m_pdfRunning.contains(runningKey(word.box, word.text)))
            body << word;
    double from = hide ? m_pdfHeadCut : 0, to = hide ? m_pdfFootCut : whole.bottom();
    for (const auto &word : body) {
        from = qMin(from, word.box.top() - 2);
        to = qMax(to, word.box.bottom() + 2);
    }
    QRectF content;
    constexpr double Dpi = 24;
    const QImage image = pdfPage ? pdfPage->renderToImage(Dpi, Dpi).convertToFormat(QImage::Format_RGB32) : QImage();
    int left = image.width(), top = image.height(), right = -1, bottom = -1;
    for (int y = qMax(0, int(std::ceil(from * Dpi / 72))); y < qMin(image.height(), int(to * Dpi / 72)); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (qRed(line[x]) < 235 || qGreen(line[x]) < 235 || qBlue(line[x]) < 235) {
                left = qMin(left, x);
                right = qMax(right, x);
                top = qMin(top, y);
                bottom = qMax(bottom, y);
            }
    }
    if (right >= 0)
        content = QRectF(left, top, right - left + 1, bottom - top + 1).adjusted(-1, -1, 1, 1);
    content = QRectF(content.topLeft() * 72 / Dpi, content.size() * 72 / Dpi);
    for (const auto &word : body)
        content = content.isNull() ? word.box : content.united(word.box);
    content = content.isNull() ? whole : content.adjusted(-6, -6, 6, 6).intersected(whole);
    return *m_pdfContentCache.insert(page, content);
}

// The part of a page each screenful shows, top and bottom in points. A cut is
// made only where it crosses no word on the page, moving up from a full
// screen until it does. When that would leave less than three quarters of a
// screen (staggered columns, a tall caption), there's no clean cut: the
// screen is shown full, cutting a line at its foot, and the next one starts
// at the top of that line, so it's read whole there. Lines repeat; none is
// lost.
const QList<QPair<double, double>> &Reader::slices(int page) const {
    auto it = m_sliceCache.find(page);
    if (it != m_sliceCache.end())
        return *it;
    const QRectF content = pdfContent(page);
    const double height = m_pageSize.height() / fitScale(page);
    const auto &words = pdfWords(page);
    // The highest top of any word the cut goes through, above the floor;
    // the cut itself if it goes through none.
    auto highestCrossing = [&](double cut, double floor) {
        double highest = cut;
        for (const auto &word : words) {
            // Accents and ascenders reach a little above the box Poppler gives.
            const double above = word.box.top() - word.box.height() / 4;
            if (above < cut - 0.01 && word.box.bottom() > cut + 0.01 && above > floor)
                highest = qMin(highest, above);
        }
        return highest;
    };
    QList<QPair<double, double>> spans;
    double top = content.top();
    while (height > 1 && top + height < content.bottom()) {
        const double full = top + height;
        double bottom = full;
        for (double up; bottom > top + height / 4 && (up = highestCrossing(bottom, -1e9)) < bottom;)
            bottom = up;
        if (bottom > top + height / 4) {
            spans << qMakePair(top, bottom);
            top = bottom;
            continue;
        }
        spans << qMakePair(top, full);
        // Only a word taller than the screen can't be shown whole; it's cut.
        const double next = highestCrossing(full, top + 1);
        top = next < full ? next : full;
    }
    spans << qMakePair(top, content.bottom());
    return *m_sliceCache.insert(page, spans);
}

QPair<double, double> Reader::sliceSpan(int page, int slice) const {
    const auto &spans = slices(page);
    return spans[qBound(0, slice, int(spans.size()) - 1)];
}

QPair<double, double> Reader::visibleSpan() const {
    if (!m_pdf || !isOpen())
        return {};
    if (!m_fitText)
        return {0, m_pdf->page(m_page)->pageSizeF().height()};
    return sliceSpan(m_page, m_slice);
}

// The first slice that shows the word whole; where lines repeat, a word is
// in two, and it belongs to the first.
int Reader::sliceOf(int page, int word) const {
    const auto &words = pdfWords(page);
    if (word < 0 || word >= words.size())
        return 0;
    const QRectF box = words[word].box;
    const auto &spans = slices(page);
    for (int slice = 0; slice < spans.size(); ++slice)
        if (box.top() >= spans[slice].first - 0.01 && box.bottom() <= spans[slice].second + 0.01)
            return slice;
    // Taller than a screen: where its middle is.
    int slice = 0;
    while (slice + 1 < spans.size() && spans[slice + 1].first <= box.center().y())
        ++slice;
    return slice;
}

int Reader::firstWordInSlice(int page, int slice) const {
    const auto &words = pdfWords(page);
    for (int i = 0; i < words.size(); ++i)
        if (sliceOf(page, i) == slice)
            return i;
    const auto span = sliceSpan(page, slice);
    for (int i = 0; i < words.size(); ++i) {
        const double y = words[i].box.center().y();
        if (y >= span.first && y < span.second)
            return i;
    }
    return 0;
}

void Reader::goToPage(int page) {
    if (!isOpen())
        return;
    m_pageTimer.restart();
    setPage(page, false);
}

void Reader::goToEnd() {
    goToPage(pageCount() - 1);
    if (m_fitText)
        setSlice(sliceCount() - 1);
}

void Reader::goToFraction(double fraction) {
    goToPage(int(std::round(qBound(0.0, fraction, 1.0) * (pageCount() - 1))));
}

void Reader::goTo(int chapter, int offset) {
    if (!isOpen())
        return;
    m_pageTimer.restart();
    m_chapter = qBound(0, chapter, int(m_chapterPaths.size()) - 1);
    m_offset = qMax(0, offset);
    setPage(pageOf(m_chapter, m_offset), true);
}

void Reader::goToToc(int index) {
    if (index >= 0 && index < m_toc.size())
        goTo(m_toc[index].chapter, m_toc[index].offset);
}

void Reader::goToChapterStart(int direction) {
    if (m_toc.isEmpty())
        return;
    const int current = tocIndex();
    if (direction > 0) {
        if (current + 1 < m_toc.size())
            goToToc(current + 1);
        return;
    }
    // Back goes to the start of this chapter, or the one before when
    // already there, like a CD player.
    const bool atStart = current >= 0 && pageOf(m_toc[current].chapter, m_toc[current].offset) >= m_page;
    goToToc(qMax(0, atStart ? current - 1 : current));
}

QString Reader::follow(const QString &target) {
    const QUrl url(target);
    if (!url.scheme().isEmpty() && !url.isLocalFile())
        return target;
    const int hash = target.indexOf('#');
    const int chapter = m_chapterPaths.indexOf(hash < 0 ? target : target.left(hash));
    if (chapter >= 0)
        goTo(chapter, hash < 0 ? 0 : m_anchors[chapter].value(target.mid(hash + 1), 0));
    return {};
}

bool Reader::atEnd() const { return m_page + m_columns >= pageCount() && m_slice + 1 >= sliceCount(); }

int Reader::viewEnd() const {
    const int next = m_page + m_columns;
    if (m_pdf)
        return qMin(next, pageCount());
    return next;
}

qint64 Reader::globalOffset(int chapter, int offset) const {
    if (m_pdf)
        return chapter;
    if (chapter < 0 || chapter >= m_charactersBefore.size())
        return 0;
    return m_charactersBefore[chapter] + offset;
}

double Reader::progress() const {
    if (!isOpen())
        return 0;
    // Before the first layout there are no pages, but the place is known,
    // and reporting 0% then would overwrite the saved progress.
    if (pageCount() > 0 && atEnd())
        return 1;
    if (m_pdf) {
        const double shown = m_fitText ? double(m_slice + 1) / sliceCount() : m_columns;
        return pageCount() ? (m_page + shown) / pageCount() : 0;
    }
    return m_characterCount ? double(globalOffset(m_chapter, m_offset)) / m_characterCount : 0;
}

qint64 Reader::tocOffset(int index) const { return globalOffset(m_toc[index].chapter, m_toc[index].offset); }

int Reader::tocIndexAt(qint64 offset) const {
    int found = -1;
    for (int i = 0; i < m_toc.size(); ++i)
        if (tocOffset(i) <= offset && (found < 0 || tocOffset(i) >= tocOffset(found)))
            found = i;
    return found;
}

int Reader::tocIndex() const {
    // A heading near the top of the view already belongs to what's on screen.
    return tocIndexAt(m_pdf ? m_page : globalOffset(m_chapter, m_offset) + 1);
}

QString Reader::chapterTitle() const {
    const int index = tocIndex();
    return index >= 0 ? m_toc[index].title : m_title;
}

namespace {
template <typename Offset>
qint64 nextTocOffset(int index, qint64 current, qint64 end, int count, Offset offset) {
    for (int i = index + 1; i < count; ++i)
        if (offset(i) > current)
            return offset(i);
    return end;
}
}

int Reader::pagesLeftInChapter() const {
    if (!isOpen())
        return 0;
    const qint64 current = globalOffset(m_chapter, m_offset);
    const qint64 next = nextTocOffset(tocIndex(), current, -1, m_toc.size(), [this](int i) { return tocOffset(i); });
    int nextPage = pageCount();
    if (next >= 0)
        for (int i = 0; i < m_toc.size(); ++i)
            if (tocOffset(i) == next) {
                nextPage = pageOf(m_toc[i].chapter, m_toc[i].offset);
                break;
            }
    return qMax(0, nextPage - (m_page + m_columns));
}

// In fit text, the part of the PDF page in view that's still below.
double Reader::unreadOfPage() const {
    if (!m_pdf || !m_fitText)
        return 0;
    return double(sliceCount() - m_slice - 1) / sliceCount();
}

int Reader::minutesLeftInChapter() const {
    if (!isOpen())
        return 0;
    const double speed = m_preferences->readingSpeed();
    if (m_pdf)
        return int(std::ceil((pagesLeftInChapter() + unreadOfPage()) * m_pdfCharactersPerPage / speed / 60));
    const qint64 current = globalOffset(m_chapter, m_offset);
    const qint64 next = nextTocOffset(tocIndex(), current, m_characterCount, m_toc.size(), [this](int i) { return tocOffset(i); });
    return int(std::ceil((next - current) / speed / 60));
}

int Reader::minutesLeftInBook() const {
    if (!isOpen())
        return 0;
    const double speed = m_preferences->readingSpeed();
    if (m_pdf)
        return int(std::ceil((qMax(0, pageCount() - viewEnd()) + unreadOfPage()) * m_pdfCharactersPerPage / speed / 60));
    return int(std::ceil((m_characterCount - globalOffset(m_chapter, m_offset)) / speed / 60));
}

int Reader::location() const {
    if (m_pdf)
        return m_page + 1;
    return int(globalOffset(m_chapter, m_offset) / CharactersPerLocation) + 1;
}

int Reader::locationCount() const {
    if (m_pdf)
        return pageCount();
    return int(m_characterCount / CharactersPerLocation) + 1;
}

// Kindle's trick: time how long each page took, and learn the reader's pace
// from the pages that look like reading rather than flipping or a break.
void Reader::trackReading(int fromPage) {
    const double seconds = m_pageTimer.restart() / 1000.0;
    qint64 characters = 0;
    if (m_pdf && m_fitText) {
        // A slice is part of a page, so it says nothing about a page's length.
        const auto span = sliceSpan(fromPage, m_slice);
        for (const auto &word : pdfWords(fromPage))
            if (word.box.center().y() >= span.first && word.box.center().y() < span.second)
                characters += word.text.size() + 1;
    } else if (m_pdf) {
        for (int page = fromPage; page < viewEnd(); ++page)
            for (const auto &word : pdfWords(page))
                characters += word.text.size() + 1;
        if (characters > 0)
            m_pdfCharactersPerPage = m_pdfCharactersPerPage * 0.8 + double(characters) / m_columns * 0.2;
    } else {
        const int endPage = fromPage + m_columns;
        const qint64 end = endPage >= pageCount() ? m_characterCount
                                                  : globalOffset(chapterOfPage(endPage), firstPositionOnPage(endPage));
        characters = end - globalOffset(m_chapter, m_offset);
    }
    if (seconds > 4 && seconds < 600 && characters > 200)
        m_preferences->learnReadingSpeed(characters / seconds);
}

QVariantList Reader::toc() const {
    QVariantList entries;
    for (const auto &entry : m_toc)
        entries << QVariantMap{{"title", entry.title}, {"level", entry.level},
                               {"page", pageOf(entry.chapter, entry.offset) + 1}};
    return entries;
}

// ---- Painting -------------------------------------------------------------

QRectF Reader::columnRect(int column) const {
    return QRectF(column * (m_pageSize.width() + m_gap), 0, m_pageSize.width(), m_pageSize.height());
}

void Reader::paint(QPainter *painter, const QSizeF &) {
    if (!isOpen() || pageCount() == 0)
        return;
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    for (int column = 0; column < m_columns && m_page + column < pageCount(); ++column) {
        if (m_pdf)
            paintPdf(painter, column);
        else
            paintEpub(painter, column);
    }
}

void Reader::paintEpub(QPainter *painter, int column) {
    const int page = m_page + column;
    const int chapter = chapterOfPage(page);
    auto *document = m_chapters[chapter].get();
    const double top = (page - m_pageStart[chapter]) * m_pageSize.height();
    const QRectF rect = columnRect(column);
    const bool dark = m_preferences->darkPage();

    QAbstractTextDocumentLayout::PaintContext context;
    context.clip = QRectF(0, top, rect.width(), rect.height());
    const QColor text(m_preferences->textColor());
    context.palette.setColor(QPalette::Text, text);
    context.palette.setColor(QPalette::Link, text);

    auto selection = [&](int start, int end, const QTextCharFormat &format) {
        QTextCursor cursor(document);
        cursor.setPosition(qMin(start, document->characterCount() - 1));
        cursor.setPosition(qMin(end, document->characterCount() - 1), QTextCursor::KeepAnchor);
        context.selections << QAbstractTextDocumentLayout::Selection{cursor, format};
    };
    for (const auto &value : m_highlights) {
        const auto highlight = value.toObject();
        if (highlight["chapter"].toInt() != chapter)
            continue;
        QTextCharFormat format;
        QColor color = highlightColor(highlight["color"].toString());
        if (highlight["color"] == QLatin1String("underline")) {
            format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
            format.setUnderlineColor(color);
        } else {
            color.setAlphaF(dark ? 0.42 : 0.38);
            format.setBackground(color);
        }
        if (!highlight["note"].toString().isEmpty() && highlight["color"] != QLatin1String("underline")) {
            format.setUnderlineStyle(QTextCharFormat::DotLine);
            format.setUnderlineColor(highlightColor(highlight["color"].toString()).darker(dark ? 80 : 160));
        }
        selection(highlight["start"].toInt(), highlight["end"].toInt(), format);
    }
    if (m_searchChapter == chapter) {
        QTextCharFormat format;
        QColor color("#ff9f0a");
        color.setAlphaF(0.55);
        format.setBackground(color);
        selection(m_searchStart, m_searchEnd, format);
    }
    if (hasSelection() && m_selectionChapter == chapter) {
        QTextCharFormat format;
        QColor color("#3d8bfd");
        color.setAlphaF(dark ? 0.5 : 0.32);
        format.setBackground(color);
        selection(m_selectionStart, m_selectionEnd, format);
    }

    painter->save();
    painter->translate(rect.topLeft() - QPointF(0, top));
    painter->setClipRect(context.clip);
    document->documentLayout()->draw(painter, context);
    painter->restore();
}

QTransform Reader::pdfTransform(int column) const {
    const int page = m_page + column;
    const QSizeF points = m_pdf->page(page)->pageSizeF();
    const QRectF rect = columnRect(column);
    if (m_fitText) {
        const double scale = fitScale(page);
        const QRectF content = pdfContent(page);
        QTransform transform;
        transform.translate(rect.x() + (rect.width() - content.width() * scale) / 2 - content.left() * scale,
                            rect.y() - sliceSpan(page, m_slice).first * scale);
        transform.scale(scale, scale);
        return transform;
    }
    const double scale = qMin(rect.width() / points.width(), rect.height() / points.height());
    const QSizeF size = points * scale;
    // A spread meets in the middle, like an open book; a single page centers.
    double x = rect.x() + (rect.width() - size.width()) / 2;
    if (m_columns == 2)
        x = column == 0 ? rect.right() - size.width() : rect.left();
    const double y = rect.y() + (rect.height() - size.height()) / 2;
    QTransform transform;
    transform.translate(x, y);
    transform.scale(scale, scale);
    return transform;
}

QImage Reader::pdfImage(int page, const QSize &size, qreal ratio) const {
    const bool dark = m_preferences->darkPage();
    const QString key = QStringLiteral("%1:%2x%3:%4").arg(page).arg(size.width()).arg(size.height()).arg(dark);
    if (QImage *cached = m_pdfImages.object(key))
        return *cached;
    const auto pdfPage = m_pdf->page(page);
    if (!pdfPage)
        return {};
    const double dpi = 72.0 * size.width() / pdfPage->pageSizeF().width();
    QImage image = pdfPage->renderToImage(dpi, dpi);
    if (image.isNull())
        return {};
    // Dark themes read a PDF as light text on dark, like the rest of the book.
    if (dark)
        image.invertPixels();
    image.setDevicePixelRatio(ratio);
    m_pdfImages.insert(key, new QImage(image), qMax<qsizetype>(1, image.sizeInBytes() / 1024));
    return image;
}

void Reader::paintPdf(QPainter *painter, int column) {
    const int page = m_page + column;
    const QTransform transform = pdfTransform(column);
    const QSizeF points = m_pdf->page(page)->pageSizeF();
    const QRectF target = transform.mapRect(QRectF(QPointF(0, 0), points));
    const qreal ratio = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    painter->save();
    // In fit text only this slice shows: the next one starts with the line
    // this one stopped above.
    QRectF clip = columnRect(column);
    if (m_fitText) {
        const auto span = sliceSpan(page, m_slice);
        clip.setHeight(qMin(clip.height(), (span.second - span.first) * transform.m22()));
    }
    painter->setClipRect(clip);
    const QImage image = pdfImage(page, (target.size() * ratio).toSize(), ratio);
    if (!image.isNull())
        painter->drawImage(target, image);

    const bool dark = m_preferences->darkPage();
    painter->setCompositionMode(dark ? QPainter::CompositionMode_Screen : QPainter::CompositionMode_Multiply);
    for (const auto &value : m_highlights) {
        const auto highlight = value.toObject();
        if (highlight["chapter"].toInt() != page)
            continue;
        const QColor color = highlightColor(highlight["color"].toString());
        for (const auto &rect : pdfRects(page, highlight["start"].toInt(), highlight["end"].toInt())) {
            const QRectF box = transform.mapRect(rect);
            if (highlight["color"] == QLatin1String("underline"))
                painter->fillRect(QRectF(box.left(), box.bottom() - 1.5, box.width(), 2), color);
            else
                painter->fillRect(box, dark ? color.darker(260) : color.lighter(115));
        }
    }
    for (const auto &rect : m_pdfSearchHits.value(page))
        painter->fillRect(transform.mapRect(rect), dark ? QColor("#6a3c00") : QColor("#ffc46b"));
    if (hasSelection() && m_selectionChapter == page)
        for (const auto &rect : pdfRects(page, m_selectionStart, m_selectionEnd))
            painter->fillRect(transform.mapRect(rect), dark ? QColor("#17407a") : QColor("#a9cbff"));
    painter->restore();
}

const QList<Reader::Word> &Reader::pdfWords(int page) const {
    auto it = m_pdfWordCache.find(page);
    if (it != m_pdfWordCache.end())
        return *it;
    QList<Word> words;
    if (const auto pdfPage = m_pdf->page(page))
        for (const auto &box : pdfPage->textList())
            words << Word{box->boundingBox(), box->text(), box->hasSpaceAfter()};
    return *m_pdfWordCache.insert(page, words);
}

// One rectangle per run of words on a line, so a highlight reads as a band
// rather than a row of boxes.
QList<QRectF> Reader::pdfRects(int page, int start, int end) const {
    const auto &words = pdfWords(page);
    QList<QRectF> rects;
    for (int i = qMax(0, start); i < qMin(end, int(words.size())); ++i) {
        const QRectF box = words[i].box;
        if (!rects.isEmpty() && std::abs(rects.last().center().y() - box.center().y()) < box.height() / 2 &&
            box.left() >= rects.last().left())
            rects.last() = rects.last().united(box);
        else
            rects << box;
    }
    return rects;
}

// ---- Pointing and selecting -------------------------------------------------

int Reader::columnAt(const QPointF &point) const {
    for (int column = m_columns - 1; column > 0; --column)
        if (point.x() >= columnRect(column).left() - m_gap / 2)
            return column;
    return 0;
}

int Reader::positionAt(int column, const QPointF &point) const {
    const int page = m_page + column;
    if (!isOpen() || page >= pageCount())
        return -1;
    if (m_pdf) {
        const QPointF local = pdfTransform(column).inverted().map(point);
        const auto &words = pdfWords(page);
        // In fit text only this slice's words can be pointed at.
        const auto span = m_fitText ? sliceSpan(page, m_slice) : QPair<double, double>(-1e9, 1e9);
        int best = -1;
        double bestDistance = 1e9;
        for (int i = 0; i < words.size(); ++i) {
            const QRectF box = words[i].box;
            if (box.center().y() < span.first || box.center().y() >= span.second)
                continue;
            const double dx = qMax(0.0, qMax(box.left() - local.x(), local.x() - box.right()));
            // Lines are close together, so missing vertically costs more.
            const double dy = qMax(0.0, qMax(box.top() - local.y(), local.y() - box.bottom())) * 3;
            const double distance = dx + dy;
            if (distance < bestDistance) {
                best = i;
                bestDistance = distance;
            }
        }
        return best;
    }
    const int chapter = chapterOfPage(page);
    const double top = (page - m_pageStart[chapter]) * m_pageSize.height();
    const QRectF rect = columnRect(column);
    const QPointF local(qBound(0.0, point.x() - rect.x(), rect.width()),
                        qBound(0.0, point.y() - rect.y(), rect.height() - 1) + top);
    return m_chapters[chapter]->documentLayout()->hitTest(local, Qt::FuzzyHit);
}

int Reader::dragPosition(int anchorColumn, const QPointF &point) const {
    // A word index on one PDF page means nothing on the other.
    if (m_pdf)
        return positionAt(anchorColumn, point);
    const int column = columnAt(point);
    const int chapter = chapterOfPage(m_page + anchorColumn);
    if (column == anchorColumn || m_page + column >= pageCount() || chapterOfPage(m_page + column) == chapter)
        return positionAt(column, point);
    // The other page starts a new chapter, where positions count from its
    // own start: stop at the edge of this one.
    return column > anchorColumn ? m_chapters[chapter]->characterCount() - 1 : 0;
}

QString Reader::linkAt(int column, const QPointF &point) const {
    const int page = m_page + column;
    if (!isOpen() || m_pdf || page >= pageCount())
        return {};
    const int chapter = chapterOfPage(page);
    const double top = (page - m_pageStart[chapter]) * m_pageSize.height();
    const QRectF rect = columnRect(column);
    const QString href = m_chapters[chapter]->documentLayout()->anchorAt(point - rect.topLeft() + QPointF(0, top));
    if (href.isEmpty())
        return {};
    const QUrl url(href);
    if (!url.scheme().isEmpty())
        return href;
    const QUrl resolved = QUrl::fromLocalFile(m_chapterPaths[chapter]).resolved(url);
    const QString path = resolved.toLocalFile();
    return QDir::cleanPath(path) + (resolved.hasFragment() ? "#" + resolved.fragment() : QString());
}

QString Reader::highlightAt(int column, const QPointF &point) const {
    const int position = positionAt(column, point);
    if (position < 0)
        return {};
    const int chapter = m_pdf ? m_page + column : chapterOfPage(m_page + column);
    for (const auto &value : m_highlights) {
        const auto highlight = value.toObject();
        if (highlight["chapter"].toInt() == chapter && highlight["start"].toInt() <= position &&
            position < highlight["end"].toInt())
            return highlight["id"].toString();
    }
    return {};
}

void Reader::select(int column, int anchor, int position) {
    if (anchor < 0 || position < 0)
        return;
    m_selectionChapter = m_pdf ? m_page + column : chapterOfPage(m_page + column);
    m_selectionStart = qMin(anchor, position);
    // Words in a PDF are picked whole, so the end word is included.
    m_selectionEnd = qMax(anchor, position) + (m_pdf ? 1 : 0);
    if (!m_pdf) {
        // Whole words, as Kindle selects: nobody means to highlight half of one.
        QTextCursor cursor(m_chapters[m_selectionChapter].get());
        cursor.setPosition(m_selectionStart);
        if (!cursor.atBlockStart() && cursor.document()->characterAt(m_selectionStart - 1).isLetterOrNumber())
            cursor.movePosition(QTextCursor::StartOfWord);
        m_selectionStart = cursor.position();
        cursor.setPosition(m_selectionEnd);
        if (cursor.document()->characterAt(m_selectionEnd).isLetterOrNumber() && m_selectionEnd > m_selectionStart)
            cursor.movePosition(QTextCursor::EndOfWord);
        m_selectionEnd = cursor.position();
    }
    m_selectedHighlight.clear();
    emit selectionChanged();
    emit repaint();
}

void Reader::selectWord(int column, const QPointF &point) {
    const int position = positionAt(column, point);
    if (position < 0)
        return;
    if (m_pdf) {
        select(column, position, position);
        return;
    }
    const int chapter = chapterOfPage(m_page + column);
    QTextCursor cursor(m_chapters[chapter].get());
    cursor.setPosition(position);
    cursor.select(QTextCursor::WordUnderCursor);
    if (!cursor.hasSelection())
        return;
    m_selectionChapter = chapter;
    m_selectionStart = cursor.selectionStart();
    m_selectionEnd = cursor.selectionEnd();
    m_selectedHighlight.clear();
    emit selectionChanged();
    emit repaint();
}

void Reader::selectHighlight(const QString &id) {
    for (const auto &value : m_highlights) {
        const auto highlight = value.toObject();
        if (highlight["id"] != id)
            continue;
        m_selectionChapter = highlight["chapter"].toInt();
        m_selectionStart = highlight["start"].toInt();
        m_selectionEnd = highlight["end"].toInt();
        m_selectedHighlight = id;
        emit selectionChanged();
        emit repaint();
        return;
    }
}

void Reader::clearSelection() {
    if (m_selectionStart < 0 && m_selectedHighlight.isEmpty())
        return;
    m_selectionChapter = m_selectionStart = m_selectionEnd = -1;
    m_selectedHighlight.clear();
    emit selectionChanged();
    emit repaint();
}

QRectF Reader::selectionBounds() const {
    if (!hasSelection())
        return {};
    if (m_pdf) {
        const int column = m_selectionChapter - m_page;
        if (column < 0 || column >= m_columns)
            return {};
        QRectF bounds;
        for (const auto &rect : pdfRects(m_selectionChapter, m_selectionStart, m_selectionEnd))
            bounds = bounds.united(pdfTransform(column).mapRect(rect));
        return bounds;
    }
    const auto *document = m_chapters[m_selectionChapter].get();
    auto pointOf = [&](int position, bool bottom) {
        const QTextBlock block = document->findBlock(position);
        const QRectF rect = document->documentLayout()->blockBoundingRect(block);
        const QTextLine line = block.layout()->lineForTextPosition(position - block.position());
        if (!line.isValid())
            return QPointF();
        const double x = rect.left() + line.cursorToX(position - block.position());
        const double y = rect.top() + line.y() + (bottom ? line.height() : 0);
        const int page = pageOf(m_selectionChapter, position);
        const int column = qBound(0, page - m_page, m_columns - 1);
        return QPointF(x, y - (page - m_pageStart[m_selectionChapter]) * m_pageSize.height()) + columnRect(column).topLeft();
    };
    const QPointF start = pointOf(m_selectionStart, false);
    const QPointF end = pointOf(m_selectionEnd, true);
    return QRectF(start, end).normalized();
}

QString Reader::textOf(int chapter, int start, int end) const {
    if (m_pdf) {
        const auto &words = pdfWords(chapter);
        QString text;
        for (int i = qMax(0, start); i < qMin(end, int(words.size())); ++i)
            text += words[i].text + (words[i].space && i + 1 < end ? " " : "");
        return text.simplified();
    }
    if (chapter < 0 || chapter >= int(m_chapters.size()))
        return {};
    QTextCursor cursor(m_chapters[chapter].get());
    cursor.setPosition(qMin(start, m_chapters[chapter]->characterCount() - 1));
    cursor.setPosition(qMin(end, m_chapters[chapter]->characterCount() - 1), QTextCursor::KeepAnchor);
    return plain(cursor.selectedText());
}

QString Reader::selectionText() const {
    return hasSelection() ? textOf(m_selectionChapter, m_selectionStart, m_selectionEnd) : QString();
}

void Reader::copySelection() const {
    if (hasSelection())
        QGuiApplication::clipboard()->setText(selectionText());
}

// The sentence around the selection, kept with a looked-up word the way
// Kindle's Vocabulary Builder does.
QString Reader::selectionContext() const {
    if (!hasSelection())
        return {};
    QString text;
    int start;
    if (m_pdf) {
        text = textOf(m_selectionChapter, m_selectionStart - 25, m_selectionEnd + 25);
        start = text.indexOf(selectionText());
    } else {
        const QTextBlock block = m_chapters[m_selectionChapter]->findBlock(m_selectionStart);
        text = block.text();
        start = m_selectionStart - block.position();
    }
    if (start < 0)
        return text.left(200);
    static const QRegularExpression stop(R"([.!?…]["'”’)]?\s)");
    int from = 0;
    for (auto it = stop.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        if (match.capturedEnd() > start)
            break;
        from = match.capturedEnd();
    }
    const auto end = stop.match(text, start);
    return text.mid(from, (end.hasMatch() ? end.capturedEnd() : text.size()) - from).trimmed();
}

// ---- Annotations ------------------------------------------------------------

// A bookmark marks a view: a page, or in fit text the slice of a page.
bool Reader::bookmarkInView(const QJsonObject &bookmark) const {
    const int page = pageOf(bookmark["chapter"].toInt(), bookmark["offset"].toInt());
    if (page < m_page || page >= m_page + m_columns)
        return false;
    return !m_pdf || !m_fitText || sliceOf(page, bookmark["offset"].toInt()) == m_slice;
}

bool Reader::bookmarked() const {
    if (!isOpen())
        return false;
    for (const auto &value : m_bookmarks)
        if (bookmarkInView(value.toObject()))
            return true;
    return false;
}

void Reader::toggleBookmark() {
    if (!isOpen())
        return;
    checkpoint();
    for (int i = m_bookmarks.size() - 1; i >= 0; --i) {
        if (bookmarkInView(m_bookmarks[i].toObject())) {
            m_bookmarks.removeAt(i);
            annotationsEdited();
            return;
        }
    }
    // In fit text, the slice's first word, so the bookmark returns to it.
    const int chapter = m_pdf ? m_page : m_chapter;
    const int offset = m_pdf && !m_fitText ? 0 : m_offset;
    QString snippet = m_pdf ? textOf(m_page, offset, offset + 30) : textOf(chapter, offset, offset + 160);
    snippet = snippet.simplified().left(120);
    m_bookmarks.append(QJsonObject{{"chapter", chapter}, {"offset", offset}, {"title", chapterTitle()},
                                   {"snippet", snippet}, {"created", now()}});
    annotationsEdited();
}

void Reader::removeBookmark(int index) {
    if (index < 0 || index >= m_bookmarks.size())
        return;
    checkpoint();
    m_bookmarks.removeAt(index);
    annotationsEdited();
}

QString Reader::highlight(const QString &color) {
    if (!m_selectedHighlight.isEmpty()) {
        setHighlightColor(m_selectedHighlight, color);
        return m_selectedHighlight;
    }
    if (!hasSelection())
        return {};
    checkpoint();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    m_highlights.append(QJsonObject{{"id", id},
                                    {"chapter", m_selectionChapter},
                                    {"start", m_selectionStart},
                                    {"end", m_selectionEnd},
                                    {"text", selectionText()},
                                    {"color", color},
                                    {"note", QString()},
                                    {"created", now()}});
    m_selectedHighlight = id;
    annotationsEdited();
    emit selectionChanged();
    return id;
}

void Reader::setHighlightColor(const QString &id, const QString &color) {
    for (int i = 0; i < m_highlights.size(); ++i) {
        auto highlight = m_highlights[i].toObject();
        if (highlight["id"] != id || highlight["color"] == color)
            continue;
        checkpoint();
        highlight["color"] = color;
        m_highlights[i] = highlight;
        annotationsEdited();
        return;
    }
}

void Reader::setNote(const QString &id, const QString &note) {
    for (int i = 0; i < m_highlights.size(); ++i) {
        auto highlight = m_highlights[i].toObject();
        if (highlight["id"] != id || highlight["note"].toString() == note.trimmed())
            continue;
        checkpoint();
        highlight["note"] = note.trimmed();
        m_highlights[i] = highlight;
        annotationsEdited();
        return;
    }
}

void Reader::removeHighlight(const QString &id) {
    for (int i = 0; i < m_highlights.size(); ++i)
        if (m_highlights[i].toObject()["id"] == id) {
            checkpoint();
            m_highlights.removeAt(i);
            if (m_selectedHighlight == id)
                clearSelection();
            annotationsEdited();
            return;
        }
}

void Reader::goToHighlight(const QString &id) {
    for (const auto &value : m_highlights) {
        const auto highlight = value.toObject();
        if (highlight["id"] == id) {
            goTo(highlight["chapter"].toInt(), highlight["start"].toInt());
            return;
        }
    }
}

void Reader::addWord(const QString &word, const QString &definition) {
    if (!isOpen() || word.trimmed().isEmpty())
        return;
    for (const auto &value : m_vocabulary)
        if (value.toObject()["word"].toString().compare(word.trimmed(), Qt::CaseInsensitive) == 0)
            return;
    checkpoint();
    m_vocabulary.prepend(QJsonObject{{"word", word.trimmed()},
                                     {"definition", definition},
                                     {"context", selectionContext()},
                                     {"chapter", m_selectionChapter},
                                     {"offset", m_selectionStart},
                                     {"created", now()}});
    annotationsEdited();
}

void Reader::removeWord(int index) {
    if (index < 0 || index >= m_vocabulary.size())
        return;
    checkpoint();
    m_vocabulary.removeAt(index);
    annotationsEdited();
}

QVariantList Reader::bookmarks() const {
    QVariantList list;
    for (int i = 0; i < m_bookmarks.size(); ++i) {
        auto bookmark = m_bookmarks[i].toObject().toVariantMap();
        bookmark["index"] = i;
        bookmark["page"] = pageOf(bookmark["chapter"].toInt(), bookmark["offset"].toInt()) + 1;
        list << bookmark;
    }
    std::stable_sort(list.begin(), list.end(), [this](const QVariant &a, const QVariant &b) {
        return globalOffset(a.toMap()["chapter"].toInt(), a.toMap()["offset"].toInt()) <
               globalOffset(b.toMap()["chapter"].toInt(), b.toMap()["offset"].toInt());
    });
    return list;
}

QVariantList Reader::highlights() const {
    QVariantList list;
    for (const auto &value : m_highlights) {
        auto highlight = value.toObject().toVariantMap();
        highlight["page"] = pageOf(highlight["chapter"].toInt(), m_pdf ? 0 : highlight["start"].toInt()) + 1;
        const int index = tocIndexAt(m_pdf ? highlight["chapter"].toInt()
                                           : globalOffset(highlight["chapter"].toInt(), highlight["start"].toInt()) + 1);
        highlight["section"] = index >= 0 ? m_toc[index].title : QString();
        list << highlight;
    }
    std::stable_sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        const auto x = a.toMap(), y = b.toMap();
        return x["chapter"].toInt() != y["chapter"].toInt() ? x["chapter"].toInt() < y["chapter"].toInt()
                                                            : x["start"].toInt() < y["start"].toInt();
    });
    return list;
}

// ---- Exporting notes -------------------------------------------------------

namespace {
QJsonArray sortedMarks(const QJsonArray &marks, const char *offsetKey) {
    QVariantList list = marks.toVariantList();
    std::stable_sort(list.begin(), list.end(), [offsetKey](const QVariant &a, const QVariant &b) {
        const auto x = a.toMap(), y = b.toMap();
        return x["chapter"].toInt() != y["chapter"].toInt() ? x["chapter"].toInt() < y["chapter"].toInt()
                                                            : x[offsetKey].toInt() < y[offsetKey].toInt();
    });
    return QJsonArray::fromVariantList(list);
}

// Where a mark is, in words that don't depend on the window: a PDF's page,
// an EPUB's Kindle-style location, or, with the book unavailable, its
// chapter (the spine document, counted from 1).
QString placeOf(const QJsonObject &mark, bool pdf) {
    if (pdf)
        return Reader::tr("Page %1").arg(mark["chapter"].toInt() + 1);
    if (mark.contains("location"))
        return Reader::tr("Location %1").arg(mark["location"].toInt());
    return Reader::tr("Chapter %1").arg(mark["chapter"].toInt() + 1);
}

QString colorName(const QString &color) {
    if (color == QLatin1String("green")) return Reader::tr("Green");
    if (color == QLatin1String("blue")) return Reader::tr("Blue");
    if (color == QLatin1String("pink")) return Reader::tr("Pink");
    if (color == QLatin1String("purple")) return Reader::tr("Purple");
    if (color == QLatin1String("underline")) return Reader::tr("Underline");
    return Reader::tr("Yellow");
}
}

QJsonObject Reader::notesJson(const QVariantMap &book, const QJsonObject &annotations) {
    QJsonObject notes{{"version", Library::NotesFormatVersion},
                      {"id", book["id"].toString()},
                      {"sha256", book["sha256"].toString()},
                      {"title", book["title"].toString()},
                      {"author", book["author"].toString()},
                      {"format", book["format"].toString()},
                      {"bookmarks", sortedMarks(annotations["bookmarks"].toArray(), "offset")},
                      {"highlights", sortedMarks(annotations["highlights"].toArray(), "start")},
                      {"vocabulary", annotations["vocabulary"].toArray()}};
    if (annotations.contains("position"))
        notes["position"] = annotations["position"];
    if (annotations.contains("fitText"))
        notes["fitText"] = annotations["fitText"];
    // A PDF's page is fixed by the file, so it's known without opening it.
    if (book["format"] == QLatin1String("pdf"))
        for (const char *key : {"bookmarks", "highlights", "vocabulary"}) {
            QJsonArray marks = notes[key].toArray();
            for (qsizetype i = 0; i < marks.size(); ++i) {
                QJsonObject mark = marks[i].toObject();
                mark["page"] = mark["chapter"].toInt() + 1;
                mark["location"] = mark["chapter"].toInt() + 1;
                marks[i] = mark;
            }
            notes[key] = marks;
        }
    return notes;
}

QJsonObject Reader::notesJson() const {
    if (!isOpen())
        return {};
    QJsonObject notes = notesJson(m_library->book(m_id), {{"bookmarks", m_bookmarks},
                                                          {"highlights", m_highlights},
                                                          {"vocabulary", m_vocabulary},
                                                          {"position", QJsonObject{{"chapter", m_pdf ? m_page : m_chapter},
                                                                                   {"offset", m_offset}}}});
    if (m_fitTextChoice >= 0)
        notes["fitText"] = m_fitTextChoice == 1;
    auto place = [this](QJsonArray marks, const char *offsetKey) {
        for (qsizetype i = 0; i < marks.size(); ++i) {
            QJsonObject mark = marks[i].toObject();
            const int chapter = mark["chapter"].toInt(), offset = mark[offsetKey].toInt();
            if (chapter < 0 || chapter >= m_chapterPaths.size())
                continue;
            const qint64 global = globalOffset(chapter, offset);
            mark["location"] = m_pdf ? chapter + 1 : int(global / CharactersPerLocation) + 1;
            const int index = tocIndexAt(m_pdf ? chapter : global + 1);
            if (index >= 0)
                mark["section"] = m_toc[index].title;
            marks[i] = mark;
        }
        return marks;
    };
    notes["bookmarks"] = place(notes["bookmarks"].toArray(), "offset");
    notes["highlights"] = place(notes["highlights"].toArray(), "start");
    notes["vocabulary"] = place(notes["vocabulary"].toArray(), "offset");
    return notes;
}

QString Reader::notesMarkdown(const QJsonObject &notes) {
    const bool pdf = notes["format"] == QLatin1String("pdf");
    QString markdown = "# " + notes["title"].toString() + "\n";
    if (!notes["author"].toString().isEmpty())
        markdown += "\n*" + notes["author"].toString() + "*\n";
    QString section = "\n";
    for (const auto &value : notes["highlights"].toArray()) {
        const auto highlight = value.toObject();
        if (highlight["section"].toString() != section) {
            section = highlight["section"].toString();
            if (!section.isEmpty())
                markdown += "\n## " + section + "\n";
        }
        markdown += "\n> " + highlight["text"].toString().replace("\n", "\n> ") + "\n";
        markdown += "\n" + placeOf(highlight, pdf) + " · " + colorName(highlight["color"].toString()) + "\n";
        if (!highlight["note"].toString().isEmpty())
            markdown += "\n" + highlight["note"].toString() + "\n";
    }
    const QJsonArray bookmarks = notes["bookmarks"].toArray();
    if (!bookmarks.isEmpty()) {
        markdown += "\n## " + tr("Bookmarks") + "\n\n";
        for (const auto &value : bookmarks) {
            const auto bookmark = value.toObject();
            markdown += "- " + placeOf(bookmark, pdf);
            const QString title = bookmark["section"].toString(bookmark["title"].toString());
            if (!title.isEmpty())
                markdown += ", " + title;
            if (!bookmark["snippet"].toString().isEmpty())
                markdown += ": " + bookmark["snippet"].toString();
            markdown += "\n";
        }
    }
    const QJsonArray words = notes["vocabulary"].toArray();
    if (!words.isEmpty()) {
        markdown += "\n## " + tr("Words") + "\n\n";
        for (const auto &value : words) {
            const auto word = value.toObject();
            markdown += "- **" + word["word"].toString() + "**";
            if (!word["definition"].toString().isEmpty())
                markdown += ": " + word["definition"].toString().simplified();
            if (!word["context"].toString().isEmpty())
                markdown += "\n  > " + word["context"].toString().simplified();
            if (word["chapter"].toInt(-1) >= 0)
                markdown += "\n  " + placeOf(word, pdf);
            markdown += "\n";
        }
    }
    return markdown;
}

QString Reader::notesMarkdown() const { return notesMarkdown(notesJson()); }

bool Reader::exportNotes(const QUrl &url) { return exportNotesFor(m_id, url); }

bool Reader::exportNotesFor(const QString &id, const QUrl &url) {
    QJsonObject notes;
    if (isOpen() && id == m_id) {
        notes = notesJson();
    } else {
        // From what's stored alone, so notes can be had when the book can't.
        const QVariantMap book = m_library->book(id);
        const Store store = m_library->annotations(id);
        if (book.isEmpty() || store.state == StoreState::Missing || store.state == StoreState::Corrupt) {
            setError(book.isEmpty() ? tr("That book isn't in the library.") : tr("There are no notes to export."));
            return false;
        }
        notes = notesJson(book, store.object);
    }
    QSaveFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    const QByteArray bytes = file.fileName().endsWith(".json", Qt::CaseInsensitive) ? QJsonDocument(notes).toJson()
                                                                                    : notesMarkdown(notes).toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setError(tr("Could not export notes to %1: %2").arg(file.fileName(), file.errorString()));
        return false;
    }
    return true;
}

void Reader::checkpoint() {
    m_undo << QJsonObject{{"bookmarks", m_bookmarks}, {"highlights", m_highlights}, {"vocabulary", m_vocabulary}};
    if (m_undo.size() > 100)
        m_undo.removeFirst();
    m_redo.clear();
}

void Reader::restore(const QJsonObject &snapshot) {
    m_bookmarks = snapshot["bookmarks"].toArray();
    m_highlights = snapshot["highlights"].toArray();
    m_vocabulary = snapshot["vocabulary"].toArray();
    m_selectedHighlight.clear();
    annotationsEdited();
    emit selectionChanged();
}

void Reader::undo() {
    if (m_undo.isEmpty())
        return;
    m_redo << QJsonObject{{"bookmarks", m_bookmarks}, {"highlights", m_highlights}, {"vocabulary", m_vocabulary}};
    restore(m_undo.takeLast());
}

void Reader::redo() {
    if (m_redo.isEmpty())
        return;
    m_undo << QJsonObject{{"bookmarks", m_bookmarks}, {"highlights", m_highlights}, {"vocabulary", m_vocabulary}};
    restore(m_redo.takeLast());
}

void Reader::annotationsEdited() {
    emit annotationsChanged();
    emit repaint();
    scheduleSave();
}

// ---- Search -----------------------------------------------------------------

void Reader::search(const QString &query) {
    m_searchResults.clear();
    m_pdfSearchHits.clear();
    m_searchChapter = m_searchStart = m_searchEnd = -1;
    const QString needle = query.trimmed();
    constexpr int limit = 500;
    if (isOpen() && needle.size() >= 2) {
        auto snippet = [](const QString &text, int at, int length) {
            const int from = qMax(0, at - 50);
            QString before = text.mid(from, at - from), after = text.mid(at + length, 70);
            return QVariantMap{{"before", (from > 0 ? "…" : "") + before.simplified() + (before.endsWith(' ') ? " " : "")},
                               {"match", text.mid(at, length)},
                               {"after", (after.startsWith(' ') ? " " : "") + after.simplified() + "…"}};
        };
        if (m_pdf) {
            for (int page = 0; page < pageCount() && m_searchResults.size() < limit; ++page) {
                const QString text = m_pdf->page(page)->text(QRectF());
                for (int at = text.indexOf(needle, 0, Qt::CaseInsensitive); at >= 0 && m_searchResults.size() < limit;
                     at = text.indexOf(needle, at + needle.size(), Qt::CaseInsensitive)) {
                    auto result = snippet(text, at, needle.size());
                    result["chapter"] = page;
                    result["start"] = at;
                    result["page"] = page + 1;
                    m_searchResults << result;
                }
            }
        } else {
            for (int chapter = 0; chapter < int(m_chapters.size()) && m_searchResults.size() < limit; ++chapter) {
                auto *document = m_chapters[chapter].get();
                for (QTextCursor found = document->find(needle); !found.isNull() && m_searchResults.size() < limit;
                     found = document->find(needle, found)) {
                    const QTextBlock block = found.block();
                    auto result = snippet(block.text(), found.selectionStart() - block.position(), needle.size());
                    result["chapter"] = chapter;
                    result["start"] = found.selectionStart();
                    result["end"] = found.selectionEnd();
                    result["page"] = pageOf(chapter, found.selectionStart()) + 1;
                    m_searchResults << result;
                }
            }
        }
    }
    emit searchChanged();
    emit repaint();
}

void Reader::goToResult(int index) {
    if (index < 0 || index >= m_searchResults.size())
        return;
    const auto result = m_searchResults[index].toMap();
    const int chapter = result["chapter"].toInt();
    if (m_pdf) {
        m_pdfSearchHits.clear();
        if (!m_searchResults.isEmpty())
            m_pdfSearchHits[chapter] = m_pdf->page(chapter)->search(result["match"].toString(),
                                                                    Poppler::Page::IgnoreCase);
        // Which match on its page this result is: Poppler finds them in the
        // same order as the text search did.
        int occurrence = 0;
        for (int i = 0; i < index; ++i)
            if (m_searchResults[i].toMap()["chapter"].toInt() == chapter)
                ++occurrence;
        // The word the hit starts on, so fit text opens the slice that shows it.
        int word = 0;
        const auto hits = m_pdfSearchHits.value(chapter);
        const auto &words = pdfWords(chapter);
        const QRectF hit = hits.isEmpty() ? QRectF() : hits[qMin(occurrence, int(hits.size()) - 1)];
        for (int i = 0; !hits.isEmpty() && i < words.size(); ++i)
            if (words[i].box.intersects(hit)) {
                word = i;
                break;
            }
        goTo(chapter, word);
        return;
    }
    m_searchChapter = chapter;
    m_searchStart = result["start"].toInt();
    m_searchEnd = result["end"].toInt();
    goTo(chapter, m_searchStart);
}

void Reader::clearSearch() {
    m_searchResults.clear();
    m_pdfSearchHits.clear();
    m_searchChapter = m_searchStart = m_searchEnd = -1;
    emit searchChanged();
    emit repaint();
}

// ---- Saving -----------------------------------------------------------------

void Reader::scheduleSave() {
    if (isOpen() && !m_readOnly)
        m_saveTimer.start();
}

bool Reader::save() {
    m_saveTimer.stop();
    if (!isOpen() || m_readOnly)
        return true;
    // Notes another book couldn't save get another try.
    for (auto it = m_unsaved.begin(); it != m_unsaved.end();)
        it = it.key() != m_id && m_library->saveAnnotations(it.key(), it->first, it->second) ? m_unsaved.erase(it)
                                                                                              : std::next(it);

    const int chapter = m_pdf ? m_page : m_chapter;
    QJsonObject annotations = m_savedAnnotations;
    QJsonObject position = annotations["position"].toObject();
    position["chapter"] = chapter;
    position["offset"] = m_offset;
    annotations["position"] = position;
    annotations["bookmarks"] = m_bookmarks;
    annotations["highlights"] = m_highlights;
    annotations["vocabulary"] = m_vocabulary;
    if (m_fitTextChoice >= 0)
        annotations["fitText"] = m_fitTextChoice == 1;
    else
        annotations.remove("fitText");
    bool saved = false;
    if (!m_notesLocked) {
        QJsonObject written;
        saved = m_library->saveAnnotations(m_id, annotations, m_savedAnnotations, &written);
        if (saved) {
            m_unsaved.remove(m_id);
            m_savedAnnotations = written;
            // Marks another Nell saved meanwhile now show here too.
            if (written["bookmarks"].toArray() != m_bookmarks || written["highlights"].toArray() != m_highlights ||
                written["vocabulary"].toArray() != m_vocabulary) {
                m_bookmarks = written["bookmarks"].toArray();
                m_highlights = written["highlights"].toArray();
                m_vocabulary = written["vocabulary"].toArray();
                emit annotationsChanged();
                emit repaint();
            }
        } else {
            m_unsaved.insert(m_id, {annotations, m_savedAnnotations});
            setError(m_library->error());
        }
    }
    // Progress lives in the library, which may still be writable.
    m_library->touch(m_id, progress());
    return saved;
}

#include "reader.moc"
