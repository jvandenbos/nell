#pragma once

#include <QByteArray>
#include <QList>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

// One StarDict dictionary on disk: the .ifo that describes it, the .idx
// (or .idx.gz) of its headwords, the .dict (or .dict.dz) of its articles,
// and a .syn of other spellings if it has one. Nothing is read until a word
// is looked up, and then only the index and the articles asked for: a
// dictzip file is read a chunk at a time, never unpacked whole.
//
// The files are whatever the reader downloaded, so every size and offset in
// them is checked before it's used.
class StarDict {
public:
    struct Article {
        QString headword;
        QString text;
    };

    // Null, with the reason in *error, if the .ifo isn't one or its files
    // are missing.
    static std::unique_ptr<StarDict> open(const QString &ifoPath, QString *error = nullptr);

    QString name() const { return m_name; }
    QString path() const { return m_ifo; }
    qint64 wordCount() const { return m_wordCount; }
    // The language of its headwords, where that's known (Dictionaries).
    QLocale::Language language() const { return m_language; }
    void setLanguage(QLocale::Language language) { m_language = language; }

    // The articles for a word, whatever its case. Empty if it isn't there,
    // or the dictionary can't be read (why is then in problem()).
    QList<Article> lookup(const QString &word);
    QString problem() const { return m_problem; }

    // An article's fields as plain text, by its type letters (StarDict's
    // sametypesequence), or with a type letter before each field if empty.
    static QString plainText(const QByteArray &data, const QByteArray &types);

    // Reads a gzip file whole; a null array if it isn't one or is damaged.
    static QByteArray gunzip(const QString &path, qint64 limit);

private:
    StarDict() = default;
    bool load();
    // Indexes into an index's entries whose word is `word`, case aside.
    QList<int> find(const QByteArray &index, const std::vector<quint32> &starts, const std::vector<quint32> &order,
                    const QByteArray &word) const;
    QByteArray article(quint64 offset, quint32 size);
    bool readDictzipHeader();

    QString m_ifo, m_name, m_idxPath, m_dictPath, m_synPath, m_problem;
    QByteArray m_types;
    qint64 m_wordCount = 0;
    QLocale::Language m_language = QLocale::AnyLanguage;
    int m_offsetBytes = 4;
    bool m_loaded = false;

    // The index as it is on disk, where each entry starts in it, and the
    // entries in the order a binary search needs. The file is meant to be
    // sorted already; one that isn't is sorted here rather than trusted.
    QByteArray m_idx, m_syn;
    std::vector<quint32> m_idxStarts, m_idxOrder, m_synStarts, m_synOrder;

    // A dictzip file's chunks: each unpacks on its own.
    bool m_dictzip = false;
    quint32 m_chunkLength = 0;
    std::vector<quint64> m_chunkOffsets;
    std::vector<quint32> m_chunkSizes;
    // A .dict.dz that is plain gzip has to be unpacked whole.
    QByteArray m_dictWhole;
    bool m_dictInMemory = false;
};

// The dictionaries installed, looked for in the reader's own folder first.
//
// A German–English dictionary has "Seen" (lakes), and would answer for
// "seen" in an English book. The format doesn't say what language a
// dictionary's headwords are in, so where it's put does: one in a folder
// named for a language ("de"), or named as FreeDict names its own
// ("deu-eng"), answers only for books in that language. Any other answers
// for every book.
class Dictionaries {
public:
    // No directories means the standard ones.
    explicit Dictionaries(const QStringList &directories = {});
    ~Dictionaries();

    // ~/.local/share/nell/dictionaries: where the reader puts them.
    static QString userDirectory();
    QStringList directories() const { return m_directories; }

    struct Hit {
        QString dictionary;
        QString headword;
        QString text;
    };
    // The articles for the word from every dictionary that serves the
    // language (all of them, if it's empty or not one Qt knows). When none
    // has it and the language is English or unknown, the words it might be
    // a form of are tried: "hurried" finds "hurry".
    QList<Hit> lookup(const QString &word, const QString &language = {});
    // The names of the dictionaries found, after looking again.
    QStringList names();

    // What an English word might be an inflection of, likeliest first.
    static QStringList baseForms(const QString &word);
    // The language a dictionary at this path, relative to the folder it
    // was found in, is for; AnyLanguage if its place doesn't say.
    static QLocale::Language languageOf(const QString &relativeIfoPath);

private:
    void scan();
    QStringList m_directories;
    std::vector<std::unique_ptr<StarDict>> m_dictionaries;
    QStringList m_scanned;
};
