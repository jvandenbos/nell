#include "dictionary.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

Dictionary::Dictionary(QObject *parent, const QStringList &dictionaryDirectories)
    : QObject(parent), m_installed(dictionaryDirectories) {}

QString Dictionary::cleanWord(const QString &text) {
    QString word = text.simplified();
    static const QRegularExpression edges(R"(^[^\w]+|[^\w]+$)", QRegularExpression::UseUnicodePropertiesOption);
    word.remove(edges);
    // Possessives look the base word up: "Alice's" is "Alice".
    static const QRegularExpression possessive(R"(['’]s$)");
    word.remove(possessive);
    return word;
}

void Dictionary::define(const QString &text, const QString &language) {
    if (m_reply)
        m_reply->abort();
    m_word = cleanWord(text);
    m_language = language.section('-', 0, 0).toLower();
    m_entries.clear();
    m_error.clear();
    m_triedLowercase = false;
    m_offline = false;
    m_source.clear();
    if (m_word.isEmpty()) {
        m_error = tr("Select a word to look it up.");
        emit changed();
        return;
    }
    const QList<Dictionaries::Hit> hits = m_installed.lookup(m_word, m_language);
    if (!hits.isEmpty()) {
        QStringList names;
        for (const auto &hit : hits)
            if (!names.contains(hit.dictionary))
                names << hit.dictionary;
        m_entries = entriesFrom(hits, m_word);
        m_source = names.join(", ");
        m_offline = true;
        emit changed();
        return;
    }
    request(m_word);
}

void Dictionary::defineOnline() {
    if (m_word.isEmpty() || m_reply)
        return;
    m_entries.clear();
    m_error.clear();
    m_triedLowercase = false;
    m_offline = false;
    request(m_word);
}

QVariantList Dictionary::entriesFrom(const QList<Dictionaries::Hit> &hits, const QString &word) {
    QVariantList entries;
    for (const auto &hit : hits) {
        const bool same = hit.headword.compare(word, Qt::CaseInsensitive) == 0;
        entries << QVariantMap{{"partOfSpeech", same ? hit.dictionary : hit.dictionary + " · " + hit.headword},
                               {"definitions", QStringList()},
                               {"text", hit.text}};
    }
    return entries;
}

void Dictionary::request(const QString &word) {
    m_source = QStringLiteral("Wiktionary");
    QNetworkRequest networkRequest(QUrl("https://en.wiktionary.org/api/rest_v1/page/definition/" +
                                 QString::fromUtf8(QUrl::toPercentEncoding(word))));
    // Wikimedia asks every client to name itself.
    networkRequest.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Nell/%1 (Omarchy e-book reader)").arg(QCoreApplication::applicationVersion()));
    networkRequest.setTransferTimeout(10000);
    m_reply = m_network.get(networkRequest);
    emit changed();
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::OperationCanceledError && reply != m_reply)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404 && !m_triedLowercase && m_word != m_word.toLower()) {
            // A capitalised word at the start of a sentence is usually an
            // ordinary word.
            m_triedLowercase = true;
            request(m_word.toLower());
            return;
        }
        m_reply = nullptr;
        if (status == 404)
            m_error = tr("No definition found for “%1”.").arg(m_word);
        else if (reply->error() != QNetworkReply::NoError) {
            m_error = tr("Couldn't reach Wiktionary: %1").arg(reply->errorString());
            // The way to not need it.
            if (m_installed.names().isEmpty())
                m_error += "\n" + tr("To look words up offline, put a StarDict dictionary in %1.")
                                      .arg(QStringLiteral("~/.local/share/nell/dictionaries"));
        }
        else
            m_entries = parse(reply->readAll(), m_language);
        if (m_error.isEmpty() && m_entries.isEmpty())
            m_error = tr("No definition found for “%1”.").arg(m_word);
        emit changed();
    });
}

QVariantList Dictionary::parse(const QByteArray &json, const QString &language) {
    const QJsonObject languages = QJsonDocument::fromJson(json).object();
    QJsonArray chosen = languages.value(language).toArray();
    if (chosen.isEmpty())
        chosen = languages.value("en").toArray();
    if (chosen.isEmpty() && !languages.isEmpty())
        chosen = languages.begin().value().toArray();

    static const QRegularExpression tags("<[^>]*>");
    QVariantList entries;
    for (const auto &value : chosen) {
        const QJsonObject entry = value.toObject();
        QStringList definitions;
        for (const auto &definition : entry.value("definitions").toArray()) {
            QString text = definition.toObject().value("definition").toString();
            text.remove(tags);
            text = text.simplified();
            if (!text.isEmpty() && definitions.size() < 4)
                definitions << text;
        }
        if (!definitions.isEmpty())
            entries << QVariantMap{{"partOfSpeech", entry.value("partOfSpeech").toString()},
                                   {"definitions", definitions}};
    }
    return entries;
}

QString Dictionary::summary() const {
    if (m_entries.isEmpty())
        return {};
    const auto entry = m_entries.first().toMap();
    if (entry.contains("text")) {
        // An article runs long; the Words list wants its opening. Not its
        // first paragraph, which is often only the pronunciation.
        QString opening = entry["text"].toString().simplified();
        if (opening.size() > 240)
            opening = opening.left(240).trimmed() + "…";
        return opening;
    }
    return entry["partOfSpeech"].toString() + ": " + entry["definitions"].toStringList().value(0);
}
