// The file dialog behind an interface, so tests can drive the app without a
// portal. After Omacut's (MIT, David Heinemeier Hansson).
#pragma once

#include <QList>
#include <QObject>
#include <QUrl>

class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    Q_INVOKABLE virtual void openBooks() = 0;
    Q_INVOKABLE virtual void saveNotes(const QString &suggestedName) = 0;

signals:
    void booksSelected(const QList<QUrl> &urls);
    void notesTargetSelected(const QUrl &url);
    void failed(const QString &message);
};
