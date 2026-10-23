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
// itself, so the reader's font, size and colors win over the publisher's,
// and never loads anything from outside the unpacked book.
class BookDocument : public QTextDocument {
public:
    BookDocument(const QString &path, const QString &root, bool keepFonts)
        : m_root(root), m_keepFonts(keepFonts) {
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
        // Line spacing and colors are always the reader's. Typefaces and
        // fixed type sizes are too, unless they chose the publisher's font.
        static const QRegularExpression always(R"((?<![-\w])(line-height|color|background(-color|-image)?)\s*:[^;}]*;?)",
                                               QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression fonts(R"((?<![-\w])(font-family|font-size\s*:\s*[\d.]+\s*(px|pt)|font-size\s*:\s*(x+-)?(small|medium|large))\s*[^;}]*;?)",
                                              QRegularExpression::CaseInsensitiveOption);
        css.remove(always);
        if (!m_keepFonts)
            css.remove(fonts);
        return css;
    }

private:
    QString m_root;
    bool m_keepFonts;
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
        if (m_format == QLatin1String("epub") && m_keepFonts != m_preferences->fontFamily().isEmpty())
            loadChapters();
        styleChapters();
        paginate();
    });
    connect(m_preferences, &Preferences::colorsChanged, this, [this] {
        m_pdfImages.clear();
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
    m_format = book["format"].toString();
    m_title = book["title"].toString();
    m_author = book["author"].toString();
    m_language = book["language"].toString();
    const bool loaded = m_format == QLatin1String("pdf") ? loadPdf(m_path) : loadEpub(m_library->cacheDirectory(id));
    if (!loaded) {
        m_chapters.clear();
        m_pdf.reset();
        return false;
    }
    setError({});
    m_id = id;

    const QJsonObject saved = m_library->annotations(id);
    m_bookmarks = saved["bookmarks"].toArray();
    m_highlights = saved["highlights"].toArray();
    m_vocabulary = saved["vocabulary"].toArray();
    const QJsonObject position = saved["position"].toObject();
    m_chapter = qBound(0, position["chapter"].toInt(), qMax(0, int(m_chapterPaths.size()) - 1));
    m_offset = qMax(0, position["offset"].toInt());
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

bool Reader::loadEpub(const QString &directory) {
    QString error;
    Epub epub;
    if (!Epub::extract(m_path, directory, &error) || !epub.load(directory, &error)) {
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
    m_chapters.clear();
    m_anchors.clear();
    m_charactersBefore.clear();
    m_characterCount = 0;
    m_bookImages.clear();
    for (const auto &path : m_chapterPaths) {
        auto document = std::make_unique<BookDocument>(path, m_root, m_keepFonts);
        document->documentLayout()->registerHandler(QTextFormat::ImageObject, m_imageHandler);
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
            document->setHtml(prepareHtml(QString::fromUtf8(file.readAll())));

        // Colors belong to the reading theme, so drop the publisher's, and
        // collect every anchor a link or the table of contents can point at.
        QHash<QString, int> anchors;
        QTextCursor cursor(document.get());
        cursor.beginEditBlock();
        for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
            QTextBlockFormat blockFormat = block.blockFormat();
            if (blockFormat.hasProperty(QTextFormat::BackgroundBrush)) {
                blockFormat.clearBackground();
                cursor.setPosition(block.position());
                cursor.setBlockFormat(blockFormat);
            }
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                QTextCharFormat format = fragment.charFormat();
                for (const auto &name : format.anchorNames())
                    anchors.insert(name, fragment.position());
                if (format.hasProperty(QTextFormat::ForegroundBrush) || format.hasProperty(QTextFormat::BackgroundBrush)) {
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
    return true;
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
    m_bookmarks = m_highlights = m_vocabulary = QJsonArray();
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
    if (area == m_area && columns == m_columns && qFuzzyCompare(gap + 1, m_gap + 1))
        return;
    const bool first = m_area.isEmpty();
    m_area = area;
    m_columns = columns;
    m_gap = gap;
    m_pageSize = QSizeF((width - gap * (columns - 1)) / columns, height);
    // Resizing a window sends a stream of sizes; paginate once it settles.
    if (first)
        paginate();
    else
        m_layoutTimer.start();
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
        m_offset = firstPositionOnPage(page);
    }
    emit positionChanged();
    emit annotationsChanged();
    emit repaint();
    scheduleSave();
}

void Reader::next() {
    if (!isOpen() || atEnd())
        return;
    trackReading(m_page);
    setPage(m_page + m_columns, false);
}

void Reader::previous() {
    if (!isOpen() || m_page == 0)
        return;
    m_pageTimer.restart();
    setPage(m_page - m_columns, false);
}

void Reader::goToPage(int page) {
    if (!isOpen())
        return;
    m_pageTimer.restart();
    setPage(page, false);
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

bool Reader::atEnd() const { return m_page + m_columns >= pageCount(); }

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
    if (!isOpen() || pageCount() == 0)
        return 0;
    if (atEnd())
        return 1;
    if (m_pdf)
        return double(m_page + m_columns) / pageCount();
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

int Reader::minutesLeftInChapter() const {
    if (!isOpen())
        return 0;
    const double speed = m_preferences->readingSpeed();
    if (m_pdf)
        return int(std::ceil(pagesLeftInChapter() * m_pdfCharactersPerPage / speed / 60));
    const qint64 current = globalOffset(m_chapter, m_offset);
    const qint64 next = nextTocOffset(tocIndex(), current, m_characterCount, m_toc.size(), [this](int i) { return tocOffset(i); });
    return int(std::ceil((next - current) / speed / 60));
}

int Reader::minutesLeftInBook() const {
    if (!isOpen())
        return 0;
    const double speed = m_preferences->readingSpeed();
    if (m_pdf)
        return int(std::ceil(qMax(0, pageCount() - viewEnd()) * m_pdfCharactersPerPage / speed / 60));
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
    if (m_pdf) {
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
    const QImage image = pdfImage(page, (target.size() * ratio).toSize(), ratio);
    if (!image.isNull())
        painter->drawImage(target, image);

    const bool dark = m_preferences->darkPage();
    painter->save();
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
        int best = -1;
        double bestDistance = 1e9;
        for (int i = 0; i < words.size(); ++i) {
            const QRectF box = words[i].box;
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

bool Reader::bookmarked() const {
    if (!isOpen())
        return false;
    for (const auto &value : m_bookmarks) {
        const auto bookmark = value.toObject();
        const int page = pageOf(bookmark["chapter"].toInt(), bookmark["offset"].toInt());
        if (page >= m_page && page < m_page + m_columns)
            return true;
    }
    return false;
}

void Reader::toggleBookmark() {
    if (!isOpen())
        return;
    checkpoint();
    for (int i = m_bookmarks.size() - 1; i >= 0; --i) {
        const auto bookmark = m_bookmarks[i].toObject();
        const int page = pageOf(bookmark["chapter"].toInt(), bookmark["offset"].toInt());
        if (page >= m_page && page < m_page + m_columns) {
            m_bookmarks.removeAt(i);
            annotationsEdited();
            return;
        }
    }
    const int chapter = m_pdf ? m_page : m_chapter;
    const int offset = m_pdf ? 0 : m_offset;
    QString snippet = m_pdf ? textOf(m_page, 0, 30) : textOf(chapter, offset, offset + 160);
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
            goTo(highlight["chapter"].toInt(), m_pdf ? 0 : highlight["start"].toInt());
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

QString Reader::notesMarkdown() const {
    QString markdown = "# " + m_title + "\n";
    if (!m_author.isEmpty())
        markdown += "\n*" + m_author + "*\n";
    QString section = "\n";
    for (const auto &value : highlights()) {
        const auto highlight = value.toMap();
        if (highlight["section"].toString() != section) {
            section = highlight["section"].toString();
            if (!section.isEmpty())
                markdown += "\n## " + section + "\n";
        }
        markdown += "\n> " + highlight["text"].toString().replace("\n", "\n> ") + "\n";
        markdown += "\n" + tr("Page %1").arg(highlight["page"].toInt()) + "\n";
        if (!highlight["note"].toString().isEmpty())
            markdown += "\n" + highlight["note"].toString() + "\n";
    }
    if (!m_vocabulary.isEmpty()) {
        markdown += "\n## " + tr("Words") + "\n\n";
        for (const auto &value : m_vocabulary) {
            const auto word = value.toObject();
            markdown += "- **" + word["word"].toString() + "**";
            if (!word["context"].toString().isEmpty())
                markdown += " — " + word["context"].toString();
            markdown += "\n";
        }
    }
    return markdown;
}

bool Reader::exportNotes(const QUrl &url) {
    QSaveFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    const QByteArray markdown = notesMarkdown().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(markdown) != markdown.size() || !file.commit()) {
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
        goToPage(chapter);
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

void Reader::save() {
    m_saveTimer.stop();
    if (!isOpen() || m_readOnly)
        return;
    const int chapter = m_pdf ? m_page : m_chapter;
    m_library->saveAnnotations(m_id, QJsonObject{{"position", QJsonObject{{"chapter", chapter}, {"offset", m_offset}}},
                                                 {"bookmarks", m_bookmarks},
                                                 {"highlights", m_highlights},
                                                 {"vocabulary", m_vocabulary}});
    m_library->touch(m_id, progress());
}

#include "reader.moc"
