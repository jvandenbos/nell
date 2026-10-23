#pragma once

#include <QCache>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QRectF>
#include <QSizeF>
#include <QTimer>
#include <QVariantList>
#include <memory>
#include <vector>

#include "epub.h"

class BookDocument;
class ImageHandler;
class Library;
class Preferences;
class QPainter;
namespace Poppler { class Document; }

// The book that's open: its pages at the current window size and settings,
// where the reader is, and what they've marked.
//
// Every place in a book is a (chapter, offset) pair. In an EPUB the chapter
// is a spine document and the offset a character in it, which survives any
// change of font or window size; in a PDF the chapter is the page and the
// offset a word on it. Pages are counted across the whole book, and the view
// shows `columns` of them side by side, starting at `page`.
class Reader : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
    Q_PROPERTY(QString bookId READ bookId NOTIFY openChanged)
    Q_PROPERTY(QString format READ format NOTIFY openChanged)
    Q_PROPERTY(QString title READ title NOTIFY openChanged)
    Q_PROPERTY(QString author READ author NOTIFY openChanged)
    Q_PROPERTY(QString language READ language NOTIFY openChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY layoutChanged)
    Q_PROPERTY(int page READ page NOTIFY positionChanged)
    Q_PROPERTY(int columns READ columns NOTIFY layoutChanged)
    Q_PROPERTY(double progress READ progress NOTIFY positionChanged)
    Q_PROPERTY(QString chapterTitle READ chapterTitle NOTIFY positionChanged)
    Q_PROPERTY(int tocIndex READ tocIndex NOTIFY positionChanged)
    Q_PROPERTY(int pagesLeftInChapter READ pagesLeftInChapter NOTIFY positionChanged)
    Q_PROPERTY(int minutesLeftInChapter READ minutesLeftInChapter NOTIFY positionChanged)
    Q_PROPERTY(int minutesLeftInBook READ minutesLeftInBook NOTIFY positionChanged)
    Q_PROPERTY(int location READ location NOTIFY positionChanged)
    Q_PROPERTY(int locationCount READ locationCount NOTIFY layoutChanged)
    Q_PROPERTY(bool atEnd READ atEnd NOTIFY positionChanged)
    Q_PROPERTY(QVariantList toc READ toc NOTIFY layoutChanged)
    Q_PROPERTY(QVariantList bookmarks READ bookmarks NOTIFY annotationsChanged)
    Q_PROPERTY(QVariantList highlights READ highlights NOTIFY annotationsChanged)
    Q_PROPERTY(QVariantList vocabulary READ vocabulary NOTIFY annotationsChanged)
    Q_PROPERTY(bool bookmarked READ bookmarked NOTIFY annotationsChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY annotationsChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY annotationsChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QString selectionText READ selectionText NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedHighlight READ selectedHighlight NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchChanged)

public:
    static constexpr int CharactersPerLocation = 150;

    Reader(Library *library, Preferences *preferences, QObject *parent = nullptr);
    ~Reader() override;

    Q_INVOKABLE bool openBook(const QString &id);
    // For the command line: read a book's notes without moving its place
    // or its spot in the library.
    void setReadOnly(bool readOnly) { m_readOnly = readOnly; }
    Q_INVOKABLE void close();
    // The page area handed over by the view: the size of all columns
    // together, without margins, and the gap between two columns.
    Q_INVOKABLE void setGeometry(double width, double height, int columns, double gap);

    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void goToPage(int page);
    Q_INVOKABLE void goToFraction(double fraction);
    Q_INVOKABLE void goToToc(int index);
    Q_INVOKABLE void goToChapterStart(int direction);
    Q_INVOKABLE void goTo(int chapter, int offset);
    // Follows an internal link; returns an external URL for QML to open.
    Q_INVOKABLE QString follow(const QString &href);

    Q_INVOKABLE void toggleBookmark();
    Q_INVOKABLE void removeBookmark(int index);
    Q_INVOKABLE QString highlight(const QString &color);
    Q_INVOKABLE void setHighlightColor(const QString &id, const QString &color);
    Q_INVOKABLE void setNote(const QString &id, const QString &note);
    Q_INVOKABLE void removeHighlight(const QString &id);
    Q_INVOKABLE void goToHighlight(const QString &id);
    Q_INVOKABLE void addWord(const QString &word, const QString &definition);
    Q_INVOKABLE void removeWord(int index);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE QString notesMarkdown() const;
    Q_INVOKABLE bool exportNotes(const QUrl &url);

    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void goToResult(int index);
    Q_INVOKABLE void clearSearch();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE QString selectionContext() const;
    Q_INVOKABLE void copySelection() const;

    // For the page view, in its own coordinates.
    void paint(QPainter *painter, const QSizeF &size);
    int columnAt(const QPointF &point) const;
    int positionAt(int column, const QPointF &point) const;
    QString linkAt(int column, const QPointF &point) const;
    QString highlightAt(int column, const QPointF &point) const;
    void select(int column, int anchor, int position);
    void selectWord(int column, const QPointF &point);
    void selectHighlight(const QString &id);
    QRectF selectionBounds() const;

    bool isOpen() const { return !m_id.isEmpty(); }
    QString bookId() const { return m_id; }
    QString format() const { return m_format; }
    QString title() const { return m_title; }
    QString author() const { return m_author; }
    QString language() const { return m_language; }
    QString error() const { return m_error; }
    int pageCount() const;
    int page() const { return m_page; }
    int columns() const { return m_columns; }
    double progress() const;
    QString chapterTitle() const;
    int tocIndex() const;
    int pagesLeftInChapter() const;
    int minutesLeftInChapter() const;
    int minutesLeftInBook() const;
    int location() const;
    int locationCount() const;
    bool atEnd() const;
    QVariantList toc() const;
    QVariantList bookmarks() const;
    QVariantList highlights() const;
    QVariantList vocabulary() const { return m_vocabulary.toVariantList(); }
    bool bookmarked() const;
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    bool hasSelection() const { return m_selectionStart >= 0 && m_selectionEnd > m_selectionStart; }
    QString selectionText() const;
    QString selectedHighlight() const { return m_selectedHighlight; }
    QVariantList searchResults() const { return m_searchResults; }

    // A place as one number across the whole book, for progress and time.
    qint64 globalOffset(int chapter, int offset) const;

signals:
    void openChanged();
    void errorChanged();
    void layoutChanged();
    void positionChanged();
    void annotationsChanged();
    void selectionChanged();
    void searchChanged();
    void repaint();

private:
    struct Word {
        QRectF box;
        QString text;
        bool space = false;
    };
    struct TocTarget {
        QString title;
        int level = 0;
        int chapter = 0;
        int offset = 0;
    };

    void setError(const QString &message);
    bool loadEpub(const QString &directory);
    bool loadPdf(const QString &path);
    void loadChapters();
    void styleChapters();
    void paginate();
    void setPage(int page, bool keepPlace);
    int chapterOfPage(int page) const;
    int pageOf(int chapter, int offset) const;
    int firstPositionOnPage(int page) const;
    int viewEnd() const;
    int tocIndexAt(qint64 offset) const;
    qint64 tocOffset(int index) const;
    QRectF columnRect(int column) const;
    QTransform pdfTransform(int column) const;
    const QList<Word> &pdfWords(int page) const;
    QImage pdfImage(int page, const QSize &size, qreal ratio) const;
    void paintEpub(QPainter *painter, int column);
    void paintPdf(QPainter *painter, int column);
    QList<QRectF> pdfRects(int page, int start, int end) const;
    QString textOf(int chapter, int start, int end) const;
    void trackReading(int fromPage);
    void annotationsEdited();
    void checkpoint();
    void restore(const QJsonObject &snapshot);
    void scheduleSave();
    void save();

    Library *m_library;
    Preferences *m_preferences;
    QString m_id, m_path, m_format, m_title, m_author, m_language, m_error;

    std::vector<std::unique_ptr<BookDocument>> m_chapters;
    QString m_root;
    QStringList m_chapterPaths;
    QHash<QString, QSize> m_imageSizes;
    QCache<QString, QImage> m_bookImages;
    ImageHandler *m_imageHandler;
    QList<QHash<QString, int>> m_anchors;
    QList<qint64> m_charactersBefore;
    qint64 m_characterCount = 0;
    QList<int> m_pageStart;
    bool m_keepFonts = false;

    std::unique_ptr<Poppler::Document> m_pdf;
    mutable QHash<int, QList<Word>> m_pdfWordCache;
    mutable QCache<QString, QImage> m_pdfImages;
    double m_pdfCharactersPerPage = 1800;
    QHash<int, QList<QRectF>> m_pdfSearchHits;

    QList<TocTarget> m_toc;
    QSizeF m_area;
    QSizeF m_pageSize;
    int m_columns = 1;
    double m_gap = 0;

    int m_page = 0;
    int m_chapter = 0;
    int m_offset = 0;
    QElapsedTimer m_pageTimer;

    QJsonArray m_bookmarks, m_highlights, m_vocabulary;
    QList<QJsonObject> m_undo, m_redo;
    int m_selectionChapter = -1, m_selectionStart = -1, m_selectionEnd = -1;
    QString m_selectedHighlight;
    QVariantList m_searchResults;
    int m_searchChapter = -1, m_searchStart = -1, m_searchEnd = -1;
    QTimer m_saveTimer;
    QTimer m_layoutTimer;
    bool m_readOnly = false;
};
