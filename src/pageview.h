#pragma once

#include <QPointer>
#include <QQuickPaintedItem>
#include <QTimer>

#include "reader.h"

// Paints the open pages and turns the pointer into reading gestures: click
// the outer edges to turn, drag to select, double-click a word, click a link
// or a highlight. The Reader keeps the state; this only translates.
class PageView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Reader *source READ reader WRITE setReader NOTIFY readerChanged)
    Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY columnsChanged)
    Q_PROPERTY(double gap READ gap WRITE setGap NOTIFY gapChanged)
    Q_PROPERTY(QRectF selectionRect READ selectionRect NOTIFY selectionRectChanged)

public:
    explicit PageView(QQuickItem *parent = nullptr);

    Reader *reader() const { return m_reader; }
    void setReader(Reader *reader);
    int columns() const { return m_columns; }
    void setColumns(int columns);
    double gap() const { return m_gap; }
    void setGap(double gap);
    QRectF selectionRect() const { return m_selectionRect; }

    void paint(QPainter *painter) override;

signals:
    void readerChanged();
    void columnsChanged();
    void gapChanged();
    void selectionRectChanged();
    // A plain click that wasn't on a link or highlight: the view decides
    // whether it turns the page or shows the controls.
    void tapped(double x, double y);
    void selectionFinished();
    void highlightClicked(const QString &id);
    void externalLink(const QString &url);

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;

private:
    void updateGeometry();
    void updateSelectionRect();

    QPointer<Reader> m_reader;
    int m_columns = 1;
    double m_gap = 0;
    QPointF m_pressPoint;
    int m_anchorColumn = 0;
    int m_anchor = -1;
    bool m_dragging = false;
    bool m_pressed = false;
    QRectF m_selectionRect;
    // A click waits out the double-click interval, so the first half of a
    // double-click on a word never turns the page.
    QTimer m_tapTimer;
    QPointF m_tapPoint;
};
