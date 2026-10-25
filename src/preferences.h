#pragma once

#include <QObject>
#include <QSettings>
#include <QStringList>

class Theme;

// How the reader likes their pages, kept in ~/.config/nell/nell.conf.
// Changes that move text around emit layoutChanged, so the Reader paginates
// again; color changes only need a repaint.
class Preferences : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString readingTheme READ readingTheme WRITE setReadingTheme NOTIFY colorsChanged)
    Q_PROPERTY(QString pageColor READ pageColor NOTIFY colorsChanged)
    Q_PROPERTY(QString textColor READ textColor NOTIFY colorsChanged)
    Q_PROPERTY(bool darkPage READ darkPage NOTIFY colorsChanged)
    Q_PROPERTY(QStringList readingThemes READ readingThemes CONSTANT)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY layoutChanged)
    Q_PROPERTY(QStringList fonts READ fonts CONSTANT)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY layoutChanged)
    Q_PROPERTY(double lineSpacing READ lineSpacing WRITE setLineSpacing NOTIFY layoutChanged)
    Q_PROPERTY(bool justify READ justify WRITE setJustify NOTIFY layoutChanged)
    Q_PROPERTY(int margins READ margins WRITE setMargins NOTIFY layoutChanged)
    Q_PROPERTY(bool spread READ spread WRITE setSpread NOTIFY layoutChanged)
    Q_PROPERTY(QString librarySort READ librarySort WRITE setLibrarySort NOTIFY librarySortChanged)
    Q_PROPERTY(int infoMode READ infoMode WRITE setInfoMode NOTIFY infoModeChanged)

public:
    explicit Preferences(Theme *theme, const QString &file = {}, QObject *parent = nullptr);

    QString readingTheme() const { return m_readingTheme; }
    void setReadingTheme(const QString &key);
    QString pageColor() const;
    QString textColor() const;
    bool darkPage() const;
    QStringList readingThemes() const;

    QString fontFamily() const { return m_fontFamily; }
    void setFontFamily(const QString &family);
    // Book-friendly faces that are installed, best first. An empty family
    // means the publisher's choice.
    QStringList fonts() const { return m_fonts; }
    int fontSize() const { return m_fontSize; }
    void setFontSize(int size);
    double lineSpacing() const { return m_lineSpacing; }
    void setLineSpacing(double spacing);
    bool justify() const { return m_justify; }
    void setJustify(bool justify);
    int margins() const { return m_margins; }
    void setMargins(int margins);
    bool spread() const { return m_spread; }
    void setSpread(bool spread);
    QString librarySort() const { return m_librarySort; }
    void setLibrarySort(const QString &sort);
    int infoMode() const { return m_infoMode; }
    void setInfoMode(int mode);

    // Characters read per second, learned from page turns like Kindle does,
    // so "minutes left" is about this reader rather than an average one.
    double readingSpeed() const { return m_readingSpeed; }
    void learnReadingSpeed(double charactersPerSecond);

signals:
    void colorsChanged();
    void layoutChanged();
    void librarySortChanged();
    void infoModeChanged();

private:
    QSettings m_settings;
    Theme *m_theme;
    QString m_readingTheme;
    QString m_fontFamily;
    QStringList m_fonts;
    int m_fontSize;
    double m_lineSpacing;
    bool m_justify;
    int m_margins;
    bool m_spread;
    QString m_librarySort;
    int m_infoMode;
    double m_readingSpeed;
};
