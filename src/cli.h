#pragma once

#include <QStringList>

// The commands that need no window: `tactile status`, `tactile set …`, and
// the rest. True for a first argument that names one.
bool isCommand(const QString &word);
// Runs one; the arguments start with the command. Returns the exit status.
int runCommand(const QStringList &arguments);
