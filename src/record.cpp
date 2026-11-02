#include "record.h"

#include "library.h"
#include "preferences.h"

#include <QDate>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QStandardPaths>
#include <cmath>

namespace {
QString today() { return QDate::currentDate().toString(Qt::ISODate); }

double secondsOf(const QJsonObject &days, const QString &date) { return days.value(date).toObject().value("seconds").toDouble(); }
}  // namespace

ReadingRecord::ReadingRecord(Library *library, Preferences *preferences, const QString &dataDirectory, QObject *parent)
    : QObject(parent), m_library(library), m_preferences(preferences),
      m_path((dataDirectory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/nell"
                                      : dataDirectory) +
             "/reading.json") {
    load();
    if (m_library)
        connect(m_library, &Library::changed, this, &ReadingRecord::changed);
    if (m_preferences)
        connect(m_preferences, &Preferences::goalChanged, this, &ReadingRecord::changed);
}

void ReadingRecord::load() {
    const Store store = Library::readStore(m_path);
    m_object = store.object;
    m_writable = store.writable();
    if (!m_writable && m_library)
        m_library->reportStorageProblem(store.problem);
}

void ReadingRecord::add(const QString &bookId, double seconds, qint64 characters) {
    if (bookId.isEmpty() || seconds <= 0 || seconds > LongestStay)
        return;
    const QVariantMap before = stats();
    const QStringList had = earned(before["badges"].toList());
    if (!save(bookId, seconds, characters))
        return;
    const QVariantMap after = stats();
    emit changed();
    if (!before["today"].toMap()["goalMet"].toBool() && after["today"].toMap()["goalMet"].toBool())
        emit goalReached();
    for (const auto &name : earned(after["badges"].toList()))
        if (!had.contains(name))
            emit badgeEarned(name);
}

// The file is read again under a lock and only this reading's share added
// to it, so two Nells reading at once both count.
bool ReadingRecord::save(const QString &bookId, double seconds, qint64 characters) {
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QLockFile lock(m_path + ".lock");
    if (!lock.tryLock(Library::LockTimeout))
        return false;
    const Store store = Library::readStore(m_path);
    if (!store.writable()) {
        m_writable = false;
        if (m_library)
            m_library->reportStorageProblem(store.problem);
        return false;
    }
    QJsonObject object = store.object;
    QJsonObject days = object["days"].toObject();
    QJsonObject day = days[today()].toObject();
    QJsonObject books = day["books"].toObject();
    QJsonObject book = books[bookId].toObject();
    day["seconds"] = day["seconds"].toDouble() + seconds;
    day["characters"] = day["characters"].toDouble() + double(characters);
    book["seconds"] = book["seconds"].toDouble() + seconds;
    book["characters"] = book["characters"].toDouble() + double(characters);
    books[bookId] = book;
    day["books"] = books;
    days[today()] = day;
    object["days"] = days;
    object["version"] = FormatVersion;
    if (!Library::writeJson(m_path, object))
        return false;
    m_object = object;
    m_writable = true;
    return true;
}

double ReadingRecord::bookSeconds(const QString &bookId) const {
    double total = 0;
    const QJsonObject days = m_object["days"].toObject();
    for (auto it = days.begin(); it != days.end(); ++it)
        total += it.value().toObject()["books"].toObject()[bookId].toObject()["seconds"].toDouble();
    return total;
}

int ReadingRecord::streakEndingAt(const QDate &day) const {
    const QJsonObject days = m_object["days"].toObject();
    int streak = 0;
    for (QDate d = day; secondsOf(days, d.toString(Qt::ISODate)) >= StreakSeconds; d = d.addDays(-1))
        ++streak;
    return streak;
}

int ReadingRecord::longestStreak() const {
    const QJsonObject days = m_object["days"].toObject();
    QStringList dates = days.keys();
    std::sort(dates.begin(), dates.end());
    int longest = 0, run = 0;
    QDate previous;
    for (const auto &date : dates) {
        if (secondsOf(days, date) < StreakSeconds)
            continue;
        const QDate d = QDate::fromString(date, Qt::ISODate);
        run = previous.isValid() && previous.daysTo(d) == 1 ? run + 1 : 1;
        longest = qMax(longest, run);
        previous = d;
    }
    return longest;
}

int ReadingRecord::booksFinished(bool thisYearOnly) const {
    if (!m_library)
        return 0;
    const QString year = QString::number(QDate::currentDate().year());
    int count = 0;
    for (const auto &value : m_library->books()) {
        const auto book = value.toMap();
        if (!book["finished"].toBool())
            continue;
        if (!thisYearOnly || book["finishedOn"].toString().startsWith(year))
            ++count;
    }
    return count;
}

QVariantList ReadingRecord::badges(int longestStreak, int booksFinished, double hours) const {
    QVariantList list;
    auto badge = [&](const QString &id, const QString &name, const QString &blurb, double have, double need) {
        list << QVariantMap{{"id", id},           {"name", name},
                            {"blurb", blurb},     {"earned", have >= need},
                            {"progress", qMin(1.0, have / need)}};
    };
    badge("streak-3", tr("Three in a Row"), tr("Read three days running"), longestStreak, 3);
    badge("streak-7", tr("A Week of Pages"), tr("Read seven days running"), longestStreak, 7);
    badge("streak-30", tr("A Month of Pages"), tr("Read thirty days running"), longestStreak, 30);
    badge("streak-100", tr("A Hundred Days"), tr("Read a hundred days running"), longestStreak, 100);
    badge("books-1", tr("The End"), tr("Finish a book"), booksFinished, 1);
    badge("books-5", tr("Five Finished"), tr("Finish five books"), booksFinished, 5);
    badge("books-10", tr("Ten Finished"), tr("Finish ten books"), booksFinished, 10);
    badge("books-25", tr("A Shelf of Twenty-Five"), tr("Finish twenty-five books"), booksFinished, 25);
    badge("books-50", tr("Fifty Finished"), tr("Finish fifty books"), booksFinished, 50);
    badge("hours-10", tr("Ten Hours In"), tr("Read for ten hours in all"), hours, 10);
    badge("hours-50", tr("Fifty Hours In"), tr("Read for fifty hours in all"), hours, 50);
    badge("hours-100", tr("A Hundred Hours"), tr("Read for a hundred hours in all"), hours, 100);
    badge("hours-500", tr("Five Hundred Hours"), tr("Read for five hundred hours in all"), hours, 500);
    return list;
}

QStringList ReadingRecord::earned(const QVariantList &badges) const {
    QStringList names;
    for (const auto &value : badges)
        if (value.toMap()["earned"].toBool())
            names << value.toMap()["name"].toString();
    return names;
}

QVariantMap ReadingRecord::stats() const {
    const QJsonObject days = m_object["days"].toObject();
    const QDate now = QDate::currentDate();
    const int goal = m_preferences ? m_preferences->dailyGoal() : 30;
    const double todaySeconds = secondsOf(days, today());
    const QJsonObject todayObject = days.value(today()).toObject();

    QVariantList week;
    for (int i = 6; i >= 0; --i) {
        const QDate d = now.addDays(-i);
        const int minutes = int(std::floor(secondsOf(days, d.toString(Qt::ISODate)) / 60));
        week << QVariantMap{{"date", d.toString(Qt::ISODate)}, {"minutes", minutes}, {"goalMet", minutes >= goal}};
    }

    double totalSeconds = 0, yearSeconds = 0;
    int readingDays = 0;
    const QString year = QString::number(now.year());
    for (auto it = days.begin(); it != days.end(); ++it) {
        const double seconds = it.value().toObject()["seconds"].toDouble();
        totalSeconds += seconds;
        if (it.key().startsWith(year))
            yearSeconds += seconds;
        if (seconds >= StreakSeconds)
            ++readingDays;
    }

    // Today keeps a streak alive until it ends, so a streak that stopped
    // yesterday still shows until midnight.
    const int streak = todaySeconds >= StreakSeconds ? streakEndingAt(now) : streakEndingAt(now.addDays(-1));
    const int longest = qMax(longestStreak(), streak);
    const int finished = booksFinished(false);
    const double hours = totalSeconds / 3600;
    return {
        {"today", QVariantMap{{"seconds", todaySeconds},
                              {"minutes", int(std::floor(todaySeconds / 60))},
                              {"characters", todayObject["characters"].toDouble()},
                              {"goal", goal},
                              {"goalMet", todaySeconds >= goal * 60}}},
        {"streak", streak},
        {"longestStreak", longest},
        {"week", week},
        {"year", QVariantMap{{"books", booksFinished(true)}, {"hours", yearSeconds / 3600}}},
        {"total", QVariantMap{{"books", finished}, {"hours", hours}, {"days", readingDays}}},
        {"badges", badges(longest, finished, hours)},
    };
}
