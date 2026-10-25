#include "portalfilepicker.h"

#include <QDBusConnection>
#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QStandardPaths>

namespace {
struct PortalFilterRule {
    uint type;
    QString pattern;
};

using PortalFilterRules = QList<PortalFilterRule>;

struct PortalFileFilter {
    QString name;
    PortalFilterRules rules;
};

using PortalFileFilters = QList<PortalFileFilter>;

// The portal's combo-box "choices" option: (id, label, [(option-id, option-label)], initial).
struct PortalChoiceOption {
    QString id;
    QString label;
};

using PortalChoiceOptions = QList<PortalChoiceOption>;

struct PortalChoice {
    QString id;
    QString label;
    PortalChoiceOptions options;
    QString initial;
};

using PortalChoices = QList<PortalChoice>;

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFilterRule &rule) {
    argument.beginStructure();
    argument << rule.type << rule.pattern;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFilterRule &rule) {
    argument.beginStructure();
    argument >> rule.type >> rule.pattern;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFileFilter &filter) {
    argument.beginStructure();
    argument << filter.name << filter.rules;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFileFilter &filter) {
    argument.beginStructure();
    argument >> filter.name >> filter.rules;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const PortalChoiceOption &option) {
    argument.beginStructure();
    argument << option.id << option.label;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalChoiceOption &option) {
    argument.beginStructure();
    argument >> option.id >> option.label;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const PortalChoice &choice) {
    argument.beginStructure();
    argument << choice.id << choice.label << choice.options << choice.initial;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalChoice &choice) {
    argument.beginStructure();
    argument >> choice.id >> choice.label >> choice.options >> choice.initial;
    argument.endStructure();
    return argument;
}

void registerPortalFilterTypes() {
    static const bool registered = [] {
        qDBusRegisterMetaType<PortalFilterRule>();
        qDBusRegisterMetaType<PortalFilterRules>();
        qDBusRegisterMetaType<PortalFileFilter>();
        qDBusRegisterMetaType<PortalFileFilters>();
        qDBusRegisterMetaType<PortalChoiceOption>();
        qDBusRegisterMetaType<PortalChoiceOptions>();
        qDBusRegisterMetaType<PortalChoice>();
        qDBusRegisterMetaType<PortalChoices>();
        return true;
    }();
    Q_UNUSED(registered);
}

PortalFileFilter bookFilter() {
    return {
        QStringLiteral("Books"),
        {
            {1, QStringLiteral("application/epub+zip")},
            {1, QStringLiteral("application/pdf")},
            {0, QStringLiteral("*.epub")},
            {0, QStringLiteral("*.pdf")},
        },
    };
}

PortalFileFilters bookFilters() {
    return {bookFilter(), {QStringLiteral("All files"), {{0, QStringLiteral("*")}}}};
}

PortalFileFilter markdownFilter() {
    return {QStringLiteral("Markdown"), {{0, QStringLiteral("*.md")}}};
}

QString portalToken() {
    return QStringLiteral("nell_%1").arg(QRandomGenerator::global()->generate());
}

QByteArray portalPathBytes(const QString &path) {
    QByteArray bytes = path.toUtf8();
    bytes.append('\0');
    return bytes;
}

QString folder(QStandardPaths::StandardLocation location) {
    const QString path = QStandardPaths::writableLocation(location);
    return QDir(path).exists() ? path : QDir::homePath();
}

QString booksFolder() {
    const QString books = QDir::homePath() + "/Books";
    return QDir(books).exists() ? books : folder(QStandardPaths::DownloadLocation);
}
}

PortalFilePicker::PortalFilePicker(QObject *parent) : FilePicker(parent) {
    registerPortalFilterTypes();
}

void PortalFilePicker::openBooks() {
    QVariantMap options;
    options.insert(QStringLiteral("accept_label"), tr("Add"));
    options.insert(QStringLiteral("modal"), true);
    options.insert(QStringLiteral("multiple"), true);
    options.insert(QStringLiteral("current_folder"), portalPathBytes(booksFolder()));
    options.insert(QStringLiteral("filters"), QVariant::fromValue(bookFilters()));
    options.insert(QStringLiteral("current_filter"), QVariant::fromValue(bookFilter()));

    requestFile(QStringLiteral("OpenFile"), tr("Add Books"), options, Action::Open);
}

void PortalFilePicker::saveNotes(const QString &suggestedName) {
    QVariantMap options;
    options.insert(QStringLiteral("accept_label"), tr("Export"));
    options.insert(QStringLiteral("modal"), true);
    options.insert(QStringLiteral("current_folder"), portalPathBytes(folder(QStandardPaths::DocumentsLocation)));
    options.insert(QStringLiteral("current_name"), suggestedName);
    options.insert(QStringLiteral("filters"), QVariant::fromValue(PortalFileFilters{markdownFilter()}));
    options.insert(QStringLiteral("current_filter"), QVariant::fromValue(markdownFilter()));

    requestFile(QStringLiteral("SaveFile"), tr("Export Notes"), options, Action::Export);
}

bool PortalFilePicker::connectToRequestPath(const QString &path) {
    m_pendingPath = path;
    return QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
        QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
        this, SLOT(handleResponse(uint,QVariantMap)));
}

void PortalFilePicker::closePendingRequest() {
    if (m_pendingPath.isEmpty())
        return;
    QDBusInterface request(QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
                           QStringLiteral("org.freedesktop.portal.Request"),
                           QDBusConnection::sessionBus());
    request.asyncCall(QStringLiteral("Close"));
}

void PortalFilePicker::requestFile(const QString &method, const QString &title,
                                   QVariantMap options, Action action) {
    // A request whose Response never arrives (the portal crashed or was
    // killed with the dialog up) must not wedge every later dialog behind
    // it: close the stuck one best-effort and replace it.
    if (m_pendingAction != Action::None) {
        closePendingRequest();
        clearPending();
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                          QStringLiteral("/org/freedesktop/portal/desktop"),
                          QStringLiteral("org.freedesktop.portal.FileChooser"),
                          bus);
    if (!portal.isValid()) {
        emit failed(tr("The desktop file chooser (xdg-desktop-portal) is not available."));
        return;
    }

    // Subscribe to the Response signal at the request path the portal will
    // derive from our handle_token *before* making the call, so a response
    // can't slip past while our match rule is still being installed.
    const QString token = portalToken();
    options.insert(QStringLiteral("handle_token"), token);
    QString sender = bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString predictedPath =
        QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);

    m_pendingAction = action;
    if (!connectToRequestPath(predictedPath)) {
        clearPending();
        emit failed(tr("Could not listen for the file chooser's answer."));
        return;
    }

    const int generation = ++m_pendingGeneration;
    auto *watcher = new QDBusPendingCallWatcher(
        portal.asyncCall(method, QString(), title, options), this);
    m_pendingWatcher = watcher;

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation]() {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (m_pendingWatcher == watcher)
            m_pendingWatcher = nullptr;

        // Superseded by a newer request, or a response may already have
        // been handled while the reply was in flight.
        if (generation != m_pendingGeneration || m_pendingAction == Action::None)
            return;

        if (reply.isError()) {
            clearPending();
            emit failed(tr("The file chooser failed: %1").arg(reply.error().message()));
            return;
        }

        // Old portal versions can hand back a different request path than the
        // handle_token predicts; move our subscription over if so.
        const QString actualPath = reply.value().path();
        if (actualPath != m_pendingPath) {
            QDBusConnection::sessionBus().disconnect(
                QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
                QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                this, SLOT(handleResponse(uint,QVariantMap)));
            if (!connectToRequestPath(actualPath)) {
                clearPending();
                emit failed(tr("Could not listen for the file chooser's answer."));
            }
        }
    });
}

void PortalFilePicker::handleResponse(uint response, const QVariantMap &results) {
    const Action action = m_pendingAction;
    clearPending();

    if (response != 0)
        return;

    const QStringList uris = results.value(QStringLiteral("uris")).toStringList();
    if (uris.isEmpty())
        return;

    if (action == Action::Open) {
        QList<QUrl> urls;
        for (const auto &uri : uris)
            urls << QUrl(uri);
        emit booksSelected(urls);
    } else if (action == Action::Export) {
        emit notesTargetSelected(QUrl(uris.first()));
    }
}

void PortalFilePicker::clearPending() {
    // A caller that never gets its reply must not outlive the request it
    // belonged to (repeated stuck requests would otherwise pile them up).
    if (m_pendingWatcher) {
        m_pendingWatcher->deleteLater();
        m_pendingWatcher = nullptr;
    }
    if (!m_pendingPath.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(
            QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
            QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
            this, SLOT(handleResponse(uint,QVariantMap)));
    }

    m_pendingPath.clear();
    m_pendingAction = Action::None;
}
