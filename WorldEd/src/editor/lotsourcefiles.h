/*
 * Copyright 2026 Unjammer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef LOTSOURCEFILES_H
#define LOTSOURCEFILES_H

#include <QString>
#include <QMap>
#include <QStringList>
#include <QImage>

class LotSourceFiles
{
public:
    struct NavigationOptions
    {
        QStringList tileDefinitions;
        QMap<QString, QString> overrides;
    };

    static bool exportCell(const QString &directory, int cellX, int cellY,
                           QString *error,
                           const QByteArray &automaticNavigation = QByteArray(),
                           const QImage &overrides = QImage(),
                           const QStringList &roomDependencies = QStringList());
    static bool importCell(const QString &directory, int cellX, int cellY,
                           QString *error);
    static bool processDirectory(const QString &directory, bool import,
                                 int *cellCount, QString *error,
                                 bool checkOnly = false,
                                 const NavigationOptions *navigation = nullptr);
    static bool validate(QString *error);
    static bool validateNavigation(QString *error);
};

#endif
