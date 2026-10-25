#pragma once

#include <QStringList>

// Commands that need no display, so agents and scripts can use the library:
// nell list, add, info, notes, and skill. Each takes --json, exits 0 or 1,
// and writes errors to stderr.
bool isCliCommand(const QString &word);
int runCli(const QStringList &arguments);
QString cliSummary();
