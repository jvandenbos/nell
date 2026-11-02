#pragma once

#include <QHash>
#include <QList>
#include <QString>

// Where a word may be broken, by Liang's algorithm (the one TeX uses) over
// the hyph-utf8 patterns bundled under src/hyphen, so hyphenation needs
// nothing installed. A pattern like "hy3ph" says a break is allowed before
// the "p" of any word containing "hyph" at that level; the highest level at
// each gap wins, and odd levels break. Exceptions ("ta-ble") win outright.
class Hyphenator {
public:
    // The bundled patterns for a language tag ("en", "en-GB", "de-DE"), or,
    // for a language Nell doesn't bundle, a libhyphen dictionary installed
    // under /usr/share/hyphen if there is one. Null when neither exists.
    static const Hyphenator *forLanguage(const QString &language);
    // The languages bundled, as tags.
    static QStringList bundled();

    bool isValid() const { return !m_patterns.isEmpty() || !m_exceptions.isEmpty(); }
    // The positions in word (counted in characters, before which a hyphen
    // may go), in order. Only words made of letters are hyphenated, never
    // within the first two or last three letters.
    QList<int> breaks(const QString &word) const;

    // For tests: patterns and exceptions in hyph-utf8's text form.
    bool loadPatterns(const QByteArray &patterns, const QByteArray &exceptions);
    // A libhyphen .dic: patterns after an encoding line, with its options.
    bool loadDictionary(const QByteArray &dictionary);

private:
    void addPattern(const QString &pattern);
    void addException(const QString &word);

    // Letters of the pattern (with '.' for a word edge) → the levels between
    // and around them, one more than there are letters.
    QHash<QString, QByteArray> m_patterns;
    QHash<QString, QList<int>> m_exceptions;
    int m_longest = 0;
    int m_leftMin = 2;
    int m_rightMin = 3;
};
