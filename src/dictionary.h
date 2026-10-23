#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QVariantList>

#include <QNetworkReply>

// Looks a word up in Wiktionary, the one free dictionary that covers every
// language a book might be in. Only the selected word leaves the machine,
// and only when the reader asks for a definition.
class Dictionary : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString word READ word NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit Dictionary(QObject *parent = nullptr);

    QString word() const { return m_word; }
    bool busy() const { return m_reply; }
    QVariantList entries() const { return m_entries; }
    QString error() const { return m_error; }

    Q_INVOKABLE void define(const QString &text, const QString &language);
    Q_INVOKABLE QString summary() const;

    static QString cleanWord(const QString &text);
    // Picks the book's language from Wiktionary's answer, else English, else
    // whatever it has.
    static QVariantList parse(const QByteArray &json, const QString &language);

signals:
    void changed();

private:
    void request(const QString &word);

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QString m_word, m_language, m_error;
    QVariantList m_entries;
    bool m_triedLowercase = false;
};
