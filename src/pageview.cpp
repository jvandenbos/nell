#include "pageview.h"

#include <QCursor>
#include <QGuiApplication>
#include <QPainter>
#include <QStyleHints>

PageView::PageView(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setAntialiasing(true);
    m_tapTimer.setSingleShot(true);
    connect(&m_tapTimer, &QTimer::timeout, this, [this] { emit tapped(m_tapPoint.x(), m_tapPoint.y()); });
}

void PageView::setReader(Reader *reader) {
    if (reader == m_reader)
        return;
    if (m_reader)
        disconnect(m_reader, nullptr, this, nullptr);
    m_reader = reader;
    if (m_reader) {
        connect(m_reader, &Reader::repaint, this, [this] { update(); });
        connect(m_reader, &Reader::layoutChanged, this, [this] { update(); });
        connect(m_reader, &Reader::selectionChanged, this, &PageView::updateSelectionRect);
        connect(m_reader, &Reader::positionChanged, this, &PageView::updateSelectionRect);
        connect(m_reader, &Reader::openChanged, this, &PageView::updateGeometry);
    }
    updateGeometry();
    emit readerChanged();
}

void PageView::setColumns(int columns) {
    if (columns == m_columns)
        return;
    m_columns = columns;
    updateGeometry();
    emit columnsChanged();
}

void PageView::setGap(double gap) {
    if (qFuzzyCompare(gap + 1, m_gap + 1))
        return;
    m_gap = gap;
    updateGeometry();
    emit gapChanged();
}

void PageView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    updateGeometry();
}

void PageView::updateGeometry() {
    if (m_reader && m_reader->isOpen() && width() > 0 && height() > 0)
        m_reader->setGeometry(width(), height(), m_columns, m_gap);
}

void PageView::paint(QPainter *painter) {
    if (m_reader)
        m_reader->paint(painter, size());
}

void PageView::updateSelectionRect() {
    const QRectF rect = m_reader ? m_reader->selectionBounds() : QRectF();
    if (rect == m_selectionRect)
        return;
    m_selectionRect = rect;
    emit selectionRectChanged();
}

void PageView::mousePressEvent(QMouseEvent *event) {
    if (!m_reader)
        return;
    m_pressed = true;
    m_dragging = false;
    m_pressPoint = event->position();
    m_anchorColumn = m_reader->columnAt(m_pressPoint);
    m_anchor = m_reader->positionAt(m_anchorColumn, m_pressPoint);
    event->accept();
}

void PageView::mouseMoveEvent(QMouseEvent *event) {
    if (!m_reader || !m_pressed)
        return;
    if (!m_dragging && (event->position() - m_pressPoint).manhattanLength() < 6)
        return;
    m_dragging = true;
    // A selection stays in the chapter it started in (on a PDF, the page).
    m_reader->select(m_anchorColumn, m_anchor, m_reader->dragPosition(m_anchorColumn, event->position()));
}

void PageView::mouseReleaseEvent(QMouseEvent *event) {
    if (!m_reader || !m_pressed)
        return;
    m_pressed = false;
    if (m_dragging) {
        m_dragging = false;
        if (m_reader->hasSelection())
            emit selectionFinished();
        return;
    }
    const QPointF point = event->position();
    const int column = m_reader->columnAt(point);
    const QString link = m_reader->linkAt(column, point);
    if (!link.isEmpty()) {
        const QString external = m_reader->follow(link);
        if (!external.isEmpty())
            emit externalLink(external);
        return;
    }
    const QString highlight = m_reader->highlightAt(column, point);
    if (!highlight.isEmpty() && !m_reader->hasSelection()) {
        m_reader->selectHighlight(highlight);
        emit highlightClicked(highlight);
        return;
    }
    m_tapPoint = point;
    m_tapTimer.start(QGuiApplication::styleHints()->mouseDoubleClickInterval());
}

void PageView::mouseDoubleClickEvent(QMouseEvent *event) {
    if (!m_reader)
        return;
    m_pressed = false;
    m_tapTimer.stop();
    m_reader->selectWord(m_reader->columnAt(event->position()), event->position());
    if (m_reader->hasSelection())
        emit selectionFinished();
}

void PageView::hoverMoveEvent(QHoverEvent *event) {
    if (!m_reader)
        return;
    const QPointF point = event->position();
    const int column = m_reader->columnAt(point);
    if (!m_reader->linkAt(column, point).isEmpty() || !m_reader->highlightAt(column, point).isEmpty())
        setCursor(Qt::PointingHandCursor);
    else
        setCursor(Qt::IBeamCursor);
}
