#pragma once

#include <QJsonObject>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Library;
class Preferences;

// What's been read, day by day, kept in ~/.local/share/nell/reading.json:
// the time spent on pages and the characters turned past, in all and per
// book. From it come today's minutes against the daily goal, the streak,
// the week, the year, and the badges. Nothing in it leaves the machine.
class ReadingRecord : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY changed)

public:
    static constexpr int FormatVersion = 1;
    // A day counts toward a streak once this much has been read in it.
    static constexpr int StreakSeconds = 60;
    // Time on one page past this is a break, not reading, and isn't counted.
    static constexpr int LongestStay = 600;

    explicit ReadingRecord(Library *library, Preferences *preferences, const QString &dataDirectory = {},
                           QObject *parent = nullptr);

    // Counts time spent on a page of a book, and the characters read if the
    // page was turned onward. Saved at once, merged with whatever another
    // Nell has saved since.
    void add(const QString &bookId, double seconds, qint64 characters);

    // today {seconds, minutes, characters, goal, goalMet}, streak, longestStreak,
    // week [{date, minutes, goalMet}] ending today, year {books, hours},
    // total {books, hours, days}, badges [{id, name, blurb, earned, progress}].
    QVariantMap stats() const;
    Q_INVOKABLE double bookSeconds(const QString &bookId) const;
    QString path() const { return m_path; }

signals:
    void changed();
    // Today's reading has just reached the daily goal.
    void goalReached();
    void badgeEarned(const QString &name);

private:
    struct Day {
        double seconds = 0;
        qint64 characters = 0;
    };
    void load();
    bool save(const QString &bookId, double seconds, qint64 characters);
    QVariantList badges(int longestStreak, int booksFinished, double hours) const;
    QStringList earned(const QVariantList &badges) const;
    int streakEndingAt(const QDate &day) const;
    int longestStreak() const;
    int booksFinished(bool thisYearOnly) const;

    Library *m_library;
    Preferences *m_preferences;
    QString m_path;
    QJsonObject m_object;
    bool m_writable = true;
};
