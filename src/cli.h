#pragma once

#include <QStringList>

// Commands that need no display, so agents and scripts can use the library:
// nell list, add, info, notes, and skill. Each takes --json, exits 0 or 1,
// and writes errors to stderr.
bool isCliCommand(const QString &word);
int runCli(const QStringList &arguments);
QString cliSummary();
// Moves Quire's storage across before anything opens Nell's, and says so on
// stderr when part of it couldn't be done, rather than starting on half.
void adoptQuireStorage();
