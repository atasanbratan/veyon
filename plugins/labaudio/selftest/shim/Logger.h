// Minimal stand-in for Veyon's Logger.h so the Lab Audio classes build outside Veyon (tests only).
#pragma once
#include <QDebug>
#define vDebug qDebug
#define vInfo qInfo
#define vWarning qWarning
#define vCritical qCritical
