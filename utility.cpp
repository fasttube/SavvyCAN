#include "utility.h"

bool Utility::decimalMode = false;
QString Utility::timeFormat = "MMM-dd HH:mm:ss.zzz";
QDateTime Utility::startDate = QDateTime::fromString("2025-01-01T00:00:00.000000", Qt::ISODateWithMs);
bool Utility::useStartDate = false;
TimeStyle Utility::timeStyle = TS_MICROS;
QString Utility::fullyQualifiedNameSeperator = "::";
