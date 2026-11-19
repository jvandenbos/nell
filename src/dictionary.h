#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QVariantList>

#include <QNetworkReply>

#include "stardict.h"

// Looks a word up: first in the StarDict dictionaries the reader has
// installed, which works offline and sends nothing anywhere, and when none
// has the word (or none is installed) in Wiktionary, the one free dictionary
// that covers every language a book might be in. Then only the selected
// word leaves the machine, and only when the reader asks for a definition.
class Dictionary : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString word READ word NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // Where the entries came from: the installed dictionaries' names, or
    // Wiktionary.
    Q_PROPERTY(QString source READ source NOTIFY changed)
    Q_PROPERTY(bool offline READ offline NOTIFY changed)

public:
    // No directories means the standard ones (Dictionaries).
    explicit Dictionary(QObject *parent = nullptr, const QStringList &dictionaryDirectories = {});

    QString word() const { return m_word; }
    bool busy() const { return m_reply; }
    QVariantList entries() const { return m_entries; }
    QString error() const { return m_error; }
    QString source() const { return m_source; }
    bool offline() const { return m_offline; }

    Q_INVOKABLE void define(const QString &text, const QString &language);
    // Asks Wiktionary for the word an installed dictionary answered.
    Q_INVOKABLE void defineOnline();
    Q_INVOKABLE QString summary() const;

    static QString cleanWord(const QString &text);
    // Picks the book's language from Wiktionary's answer, else English, else
    // whatever it has.
    static QVariantList parse(const QByteArray &json, const QString &language);
    // Installed dictionaries' articles as entries: each headed by its
    // dictionary, and by the headword when that isn't the word asked for.
    static QVariantList entriesFrom(const QList<Dictionaries::Hit> &hits, const QString &word);

signals:
    void changed();

private:
    void request(const QString &word);

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    Dictionaries m_installed;
    QString m_word, m_language, m_error, m_source;
    bool m_offline = false;
    QVariantList m_entries;
    bool m_triedLowercase = false;
};
