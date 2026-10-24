/*
 * Copyright 2026 Unjammer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "vehiclemeshpreview.h"
#include "../portablesettings.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <zlib.h>

namespace {

struct ProjectedTriangle
{
    QPointF points[3];
    QPointF texturePoints[3];
    QVector3D worldPoints[3];
    qreal depth = 0.0;
    int textureIndex = 0;
};

quint32 mixedSeed(quint32 value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

QString readTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    return stream.readAll();
}

QVector3D vectorValue(const QString &text)
{
    const QStringList values = text.split(
                QRegularExpression(QStringLiteral("\\s+")),
                Qt::SkipEmptyParts);
    if (values.size() < 3)
        return QVector3D();
    bool okX = false;
    bool okY = false;
    bool okZ = false;
    const qreal x = values.at(0).toDouble(&okX);
    const qreal y = values.at(1).toDouble(&okY);
    const qreal z = values.at(2).toDouble(&okZ);
    if (!okX || !okY || !okZ)
        return QVector3D();
    return QVector3D(x, y, z);
}

qreal directionAngle(const QString &direction)
{
    if (direction.compare(QLatin1String("E"), Qt::CaseInsensitive) == 0)
        return M_PI_2;
    if (direction.compare(QLatin1String("S"), Qt::CaseInsensitive) == 0)
        return M_PI;
    if (direction.compare(QLatin1String("W"), Qt::CaseInsensitive) == 0)
        return M_PI * 1.5;
    return 0.0;
}

bool imageTransform(const QPointF source[3], const QPointF target[3],
                    QTransform *transform)
{
    const qreal u1 = source[1].x() - source[0].x();
    const qreal v1 = source[1].y() - source[0].y();
    const qreal u2 = source[2].x() - source[0].x();
    const qreal v2 = source[2].y() - source[0].y();
    const qreal determinant = u1 * v2 - u2 * v1;
    if (qAbs(determinant) < 0.000001)
        return false;

    const qreal x1 = target[1].x() - target[0].x();
    const qreal y1 = target[1].y() - target[0].y();
    const qreal x2 = target[2].x() - target[0].x();
    const qreal y2 = target[2].y() - target[0].y();

    const qreal m11 = (x1 * v2 - x2 * v1) / determinant;
    const qreal m21 = (x2 * u1 - x1 * u2) / determinant;
    const qreal m12 = (y1 * v2 - y2 * v1) / determinant;
    const qreal m22 = (y2 * u1 - y1 * u2) / determinant;
    const qreal dx = target[0].x()
            - m11 * source[0].x() - m21 * source[0].y();
    const qreal dy = target[0].y()
            - m12 * source[0].x() - m22 * source[0].y();
    *transform = QTransform(m11, m12, m21, m22, dx, dy);
    return true;
}

class BinaryFbxReader
{
public:
    bool read(const QString &path, QVector<qreal> *vertices,
              QVector<int> *polygonIndices, QVector<qreal> *uv,
              QVector<int> *uvIndices, QVector3D *modelScale,
              const QString &geometryName = QString())
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;
        mData = file.readAll();
        if (!mData.startsWith("Kaydara FBX Binary  \x00\x1a\x00")
                || mData.size() < 27) {
            return false;
        }
        mVersion = int(unsignedValue<quint32>(23));
        mGeometryName = geometryName.toUtf8();
        if (!mGeometryName.isEmpty()) {
            mDiscovery = true;
            qint64 discoveryOffset = 27;
            while (discoveryOffset < mData.size()) {
                const qint64 previous = discoveryOffset;
                if (!readNode(&discoveryOffset, false, false, 0))
                    break;
                if (discoveryOffset <= previous)
                    return false;
            }
            for (const QPair<qint64, qint64> &connection
                 : qAsConst(mConnections)) {
                if (connection.second == mTargetModelId) {
                    mTargetGeometryId = connection.first;
                    break;
                }
            }
            if (mTargetModelId == 0 || mTargetGeometryId == 0)
                return false;
            mDiscovery = false;
        }
        qint64 offset = 27;
        while (offset < mData.size()) {
            const qint64 previous = offset;
            if (!readNode(&offset, false, false, 0))
                break;
            if (offset <= previous)
                return false;
        }
        *vertices = mVertices;
        *polygonIndices = mPolygonIndices;
        *uv = mUv;
        *uvIndices = mUvIndices;
        if (mTargetModelId == 0) {
            for (const QPair<qint64, qint64> &connection
                 : qAsConst(mConnections)) {
                if (connection.first == mTargetGeometryId) {
                    mTargetModelId = connection.second;
                    break;
                }
            }
        }
        *modelScale = mModelScales.value(
                    mTargetModelId, QVector3D(1.0f, 1.0f, 1.0f));
        return !mVertices.isEmpty() && !mPolygonIndices.isEmpty()
                && !mUv.isEmpty() && !mUvIndices.isEmpty();
    }

private:
    struct Property
    {
        char type = 0;
        qint64 integer = 0;
        qreal real = 0.0;
        QByteArray data;
        quint32 count = 0;
    };

    template<typename T>
    T unsignedValue(qint64 offset) const
    {
        T result = 0;
        if (offset < 0 || offset + qint64(sizeof(T)) > mData.size())
            return result;
        std::memcpy(&result, mData.constData() + offset, sizeof(T));
        return result;
    }

    bool readProperty(qint64 *offset, Property *property)
    {
        if (!offset || !property || *offset >= mData.size())
            return false;
        property->type = mData.at((*offset)++);
        switch (property->type) {
        case 'Y':
            property->integer = qint16(unsignedValue<quint16>(*offset));
            *offset += 2;
            return true;
        case 'C':
            property->integer = quint8(mData.at(*offset));
            *offset += 1;
            return true;
        case 'I':
            property->integer = qint32(unsignedValue<quint32>(*offset));
            *offset += 4;
            return true;
        case 'L':
            property->integer = qint64(unsignedValue<quint64>(*offset));
            *offset += 8;
            return true;
        case 'F': {
            if (*offset + 4 > mData.size())
                return false;
            float value = 0.0f;
            std::memcpy(&value, mData.constData() + *offset, 4);
            property->real = value;
            *offset += 4;
            return true;
        }
        case 'D': {
            if (*offset + 8 > mData.size())
                return false;
            double value = 0.0;
            std::memcpy(&value, mData.constData() + *offset, 8);
            property->real = value;
            *offset += 8;
            return true;
        }
        case 'S':
        case 'R': {
            const quint32 length = unsignedValue<quint32>(*offset);
            *offset += 4;
            if (*offset + length > mData.size())
                return false;
            property->data = mData.mid(*offset, length);
            *offset += length;
            return *offset <= mData.size();
        }
        case 'f':
        case 'd':
        case 'i':
        case 'l':
        case 'b': {
            property->count = unsignedValue<quint32>(*offset);
            const quint32 encoding = unsignedValue<quint32>(*offset + 4);
            const quint32 encodedLength = unsignedValue<quint32>(*offset + 8);
            *offset += 12;
            if (*offset + encodedLength > mData.size())
                return false;
            const QByteArray encoded = mData.mid(*offset, encodedLength);
            *offset += encodedLength;
            int elementSize = 1;
            if (property->type == 'f' || property->type == 'i')
                elementSize = 4;
            else if (property->type == 'd' || property->type == 'l')
                elementSize = 8;
            const quint64 expectedSize = quint64(property->count)
                    * quint64(elementSize);
            if (expectedSize > quint64(std::numeric_limits<int>::max()))
                return false;
            if (encoding == 0) {
                property->data = encoded;
                return property->data.size() == int(expectedSize);
            }
            property->data.resize(int(expectedSize));
            uLongf destinationLength = uLongf(expectedSize);
            const int result = uncompress(
                        reinterpret_cast<Bytef *>(property->data.data()),
                        &destinationLength,
                        reinterpret_cast<const Bytef *>(encoded.constData()),
                        uLong(encoded.size()));
            if (result != Z_OK || destinationLength != expectedSize) {
                property->data.clear();
                return false;
            }
            return true;
        }
        default:
            return false;
        }
    }

    QVector<qreal> realArray(const Property &property) const
    {
        QVector<qreal> result;
        result.reserve(int(property.count));
        if (property.type == 'd') {
            for (quint32 index = 0; index < property.count; ++index) {
                double value = 0.0;
                std::memcpy(&value,
                            property.data.constData() + index * 8, 8);
                result.append(value);
            }
        } else if (property.type == 'f') {
            for (quint32 index = 0; index < property.count; ++index) {
                float value = 0.0f;
                std::memcpy(&value,
                            property.data.constData() + index * 4, 4);
                result.append(value);
            }
        }
        return result;
    }

    QVector<int> intArray(const Property &property) const
    {
        QVector<int> result;
        if (property.type != 'i')
            return result;
        result.reserve(int(property.count));
        for (quint32 index = 0; index < property.count; ++index) {
            qint32 value = 0;
            std::memcpy(&value,
                        property.data.constData() + index * 4, 4);
            result.append(value);
        }
        return result;
    }

    bool readNode(qint64 *offset, bool inUvZero, bool inGeometry,
                  qint64 modelId)
    {
        if (!offset)
            return false;
        const bool wide = mVersion >= 7500;
        const int headerSize = wide ? 25 : 13;
        if (*offset + headerSize > mData.size())
            return false;
        const quint64 endOffset = wide
                ? unsignedValue<quint64>(*offset)
                : unsignedValue<quint32>(*offset);
        if (endOffset == 0)
            return false;
        const quint64 propertyCount = wide
                ? unsignedValue<quint64>(*offset + 8)
                : unsignedValue<quint32>(*offset + 4);
        const quint64 propertyLength = wide
                ? unsignedValue<quint64>(*offset + 16)
                : unsignedValue<quint32>(*offset + 8);
        const quint8 nameLength = quint8(mData.at(
                    *offset + (wide ? 24 : 12)));
        const qint64 nameOffset = *offset + headerSize;
        if (nameOffset + nameLength > mData.size()
                || endOffset > quint64(mData.size())
                || endOffset <= quint64(*offset)) {
            return false;
        }
        const QByteArray name = mData.mid(nameOffset, nameLength);
        qint64 propertyOffset = nameOffset + nameLength;
        QVector<Property> properties;
        properties.reserve(int(qMin<quint64>(propertyCount, 64)));
        for (quint64 index = 0; index < propertyCount; ++index) {
            Property property;
            if (!readProperty(&propertyOffset, &property))
                return false;
            properties.append(property);
        }
        const qint64 expectedPropertyEnd = nameOffset + nameLength
                + qint64(propertyLength);
        if (propertyOffset != expectedPropertyEnd)
            propertyOffset = expectedPropertyEnd;

        bool childUvZero = inUvZero;
        bool childGeometry = inGeometry;
        qint64 childModelId = modelId;
        if (name == "Model" && !properties.isEmpty()
                && properties.first().type == 'L') {
            childModelId = properties.first().integer;
        }
        if (mDiscovery && name == "Model" && properties.size() >= 2
                && properties.at(0).type == 'L'
                && properties.at(1).type == 'S'
                && properties.at(1).data.toLower().contains(
                    mGeometryName.toLower())) {
            mTargetModelId = properties.at(0).integer;
        }
        if (name == "C" && properties.size() >= 3
                && properties.at(1).type == 'L'
                && properties.at(2).type == 'L') {
            mConnections.append(qMakePair(properties.at(1).integer,
                                          properties.at(2).integer));
        }
        if (name == "Geometry") {
            if (mGeometryName.isEmpty()) {
                childGeometry = !mGeometryChosen;
                if (childGeometry && !properties.isEmpty()
                        && properties.first().type == 'L') {
                    mTargetGeometryId = properties.first().integer;
                }
            } else {
                childGeometry = !properties.isEmpty()
                        && properties.first().type == 'L'
                        && properties.first().integer
                        == mTargetGeometryId;
            }
            if (childGeometry)
                mGeometryChosen = true;
        }
        if (name == "LayerElementUV") {
            childUvZero = !properties.isEmpty()
                    && properties.first().integer == 0;
        }
        if (name == "P" && childModelId != 0
                && properties.size() >= 7
                && properties.first().type == 'S'
                && properties.first().data == "Lcl Scaling"
                && (properties.at(4).type == 'D'
                    || properties.at(4).type == 'F')
                && (properties.at(5).type == 'D'
                    || properties.at(5).type == 'F')
                && (properties.at(6).type == 'D'
                    || properties.at(6).type == 'F')) {
            mModelScales.insert(childModelId, QVector3D(
                                    properties.at(4).real,
                                    properties.at(5).real,
                                    properties.at(6).real));
        }
        if (!properties.isEmpty()) {
            if (childGeometry && name == "Vertices"
                    && mVertices.isEmpty())
                mVertices = realArray(properties.first());
            else if (childGeometry && name == "PolygonVertexIndex"
                     && mPolygonIndices.isEmpty())
                mPolygonIndices = intArray(properties.first());
            else if (childGeometry && childUvZero
                     && name == "UV" && mUv.isEmpty())
                mUv = realArray(properties.first());
            else if (childGeometry && childUvZero
                     && name == "UVIndex"
                     && mUvIndices.isEmpty())
                mUvIndices = intArray(properties.first());
        }

        const int sentinelSize = wide ? 25 : 13;
        qint64 childOffset = propertyOffset;
        const qint64 childrenEnd = qint64(endOffset) - sentinelSize;
        while (childOffset < childrenEnd) {
            const qint64 previous = childOffset;
            if (!readNode(&childOffset, childUvZero, childGeometry,
                          childModelId))
                break;
            if (childOffset <= previous)
                return false;
        }
        *offset = qint64(endOffset);
        return true;
    }

    QByteArray mData;
    QByteArray mGeometryName;
    int mVersion = 0;
    bool mGeometryChosen = false;
    bool mDiscovery = false;
    qint64 mTargetModelId = 0;
    qint64 mTargetGeometryId = 0;
    QVector<QPair<qint64, qint64>> mConnections;
    QHash<qint64, QVector3D> mModelScales;
    QVector<qreal> mVertices;
    QVector<int> mPolygonIndices;
    QVector<qreal> mUv;
    QVector<int> mUvIndices;
};

}

VehicleMeshPreview *VehicleMeshPreview::instance()
{
    static VehicleMeshPreview preview;
    return &preview;
}

VehicleMeshPreview::VehicleMeshPreview()
{
}

VehicleMeshPreview::~VehicleMeshPreview()
{
    saveAtlases();
}

void VehicleMeshPreview::clear()
{
    saveAtlases();
    mGameDirectory.clear();
    mMediaDirectory.clear();
    mModels.clear();
    mVehicles.clear();
    mZoneDefinitions.clear();
    mSpecialZoneNames.clear();
    mMeshes.clear();
    mRendered.clear();
    mAtlases.clear();
    mCatalogFingerprint.clear();
    mAtlasDiskHits = 0;
    mCatalogReady = false;
}

void VehicleMeshPreview::clearRendered()
{
    saveAtlases();
    mRendered.clear();
    mAtlases.clear();
}

QVector<VehicleMeshPreview::TextBlock> VehicleMeshPreview::blocks(
        const QString &text, const QString &pattern)
{
    QVector<TextBlock> result;
    QRegularExpression expression(pattern,
            QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator matches = expression.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const int openingBrace = text.indexOf(QLatin1Char('{'),
                                              match.capturedEnd(0) - 1);
        if (openingBrace < 0)
            continue;
        const QString body = blockAt(text, openingBrace);
        if (body.isNull())
            continue;
        TextBlock block;
        block.name = match.captured(1);
        block.body = body;
        result.append(block);
    }
    return result;
}

QString VehicleMeshPreview::blockAt(const QString &text, int openingBrace)
{
    if (openingBrace < 0 || openingBrace >= text.size()
            || text.at(openingBrace) != QLatin1Char('{')) {
        return QString();
    }
    int depth = 0;
    for (int index = openingBrace; index < text.size(); ++index) {
        const QChar character = text.at(index);
        if (character == QLatin1Char('{')) {
            ++depth;
        } else if (character == QLatin1Char('}')) {
            --depth;
            if (depth == 0)
                return text.mid(openingBrace + 1,
                                index - openingBrace - 1);
        }
    }
    return QString();
}

QString VehicleMeshPreview::value(const QString &text, const QString &name)
{
    const QRegularExpression expression(
                QStringLiteral("(?:^|\\n)\\s*%1\\s*=\\s*([^,\\r\\n}]+)")
                .arg(QRegularExpression::escape(name)),
                QRegularExpression::MultilineOption);
    const QRegularExpressionMatch match = expression.match(text);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

QVector<qreal> VehicleMeshPreview::numericArray(const QString &text,
                                                const QString &name)
{
    const QRegularExpression expression(
                QStringLiteral("(?:^|\\n)\\s*%1\\s*:\\s*\\*\\d+\\s*\\{\\s*a\\s*:\\s*")
                .arg(QRegularExpression::escape(name)),
                QRegularExpression::MultilineOption);
    const QRegularExpressionMatch match = expression.match(text);
    if (!match.hasMatch())
        return QVector<qreal>();
    const int end = text.indexOf(QLatin1Char('}'), match.capturedEnd());
    if (end < 0)
        return QVector<qreal>();
    const QStringList parts = text.mid(match.capturedEnd(),
                                       end - match.capturedEnd())
            .split(QLatin1Char(','), Qt::SkipEmptyParts);
    QVector<qreal> result;
    result.reserve(parts.size());
    for (const QString &part : parts) {
        bool ok = false;
        const qreal number = part.trimmed().toDouble(&ok);
        if (ok)
            result.append(number);
    }
    return result;
}

QVector<int> VehicleMeshPreview::integerArray(const QString &text,
                                              const QString &name)
{
    const QVector<qreal> numbers = numericArray(text, name);
    QVector<int> result;
    result.reserve(numbers.size());
    for (qreal number : numbers)
        result.append(int(number));
    return result;
}

QString VehicleMeshPreview::normalizedVehicleName(const QString &name)
{
    QString result = name.trimmed();
    if (!result.contains(QLatin1Char('.')))
        result.prepend(QLatin1String("Base."));
    return result;
}

bool VehicleMeshPreview::ensureCatalog(const QString &gameDirectory)
{
    const QFileInfo directoryInfo(gameDirectory);
    const QString absolute = directoryInfo.absoluteFilePath();
    if (mCatalogReady
            && absolute.compare(mGameDirectory, Qt::CaseInsensitive) == 0) {
        return true;
    }
    clear();
    return loadCatalog(absolute);
}

bool VehicleMeshPreview::loadCatalog(const QString &gameDirectory)
{
    const QDir root(gameDirectory);
    const QString media = root.filePath(QStringLiteral("media"));
    const QDir scripts(QDir(media).filePath(
                               QStringLiteral("scripts/generated/vehicles")));
    const QString zoneFile = QDir(media).filePath(
                QStringLiteral("lua/shared/VehicleZoneDefinition.lua"));
    if (!QDir(media).exists() || !scripts.exists()
            || !QFileInfo::exists(zoneFile)) {
        return false;
    }

    QHash<QString, QString> templateTextures;
    QHash<QString, QVector3D> templateExtents;
    QHash<QString, QVector<WheelDefinition>> templateWheels;
    QVector<TextBlock> vehicleBlocks;
    QStringList scriptPaths;
    QDirIterator scriptFiles(scripts.absolutePath(),
                             QStringList() << QStringLiteral("*.txt"),
                             QDir::Files,
                             QDirIterator::Subdirectories);
    while (scriptFiles.hasNext())
        scriptPaths.append(scriptFiles.next());
    std::sort(scriptPaths.begin(), scriptPaths.end(),
              [](const QString &left, const QString &right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    QCryptographicHash catalogHash(QCryptographicHash::Sha256);
    catalogHash.addData("vehicle-atlas-schema-4");
    for (const QString &scriptPath : qAsConst(scriptPaths)) {
        const QFileInfo scriptInfo(scriptPath);
        catalogHash.addData(scripts.relativeFilePath(scriptPath)
                            .toLower().toUtf8());
        catalogHash.addData(QByteArray::number(scriptInfo.size()));
        catalogHash.addData(QByteArray::number(
                                scriptInfo.lastModified().toMSecsSinceEpoch()));
        const QString text = readTextFile(scriptPath);
        if (text.isEmpty())
            continue;

        const QVector<TextBlock> modelBlocks = blocks(
                    text,
                    QStringLiteral("^\\s*model\\s+([A-Za-z0-9_]+)\\s*\\{"));
        for (const TextBlock &block : modelBlocks) {
            ModelDefinition definition;
            definition.mesh = value(block.body, QStringLiteral("mesh"));
            definition.texture = value(block.body,
                                       QStringLiteral("texture"));
            bool ok = false;
            const qreal scale = value(block.body,
                                      QStringLiteral("scale")).toDouble(&ok);
            if (ok)
                definition.scale = scale;
            definition.invertX = value(block.body,
                                       QStringLiteral("invertX"))
                    .compare(QLatin1String("true"),
                             Qt::CaseInsensitive) == 0;
            if (!definition.mesh.isEmpty())
                mModels.insert(block.name.toLower(), definition);
        }

        const QVector<TextBlock> templates = blocks(
                    text,
                    QStringLiteral("^\\s*template\\s+vehicle\\s+([A-Za-z0-9_]+)\\s*\\{"));
        for (const TextBlock &block : templates) {
            const QVector<TextBlock> skins = blocks(
                        block.body,
                        QStringLiteral("^\\s*(skin)\\s*\\{"));
            if (!skins.isEmpty()) {
                const QString texture = value(skins.first().body,
                                              QStringLiteral("texture"));
                if (!texture.isEmpty())
                    templateTextures.insert(block.name.toLower(), texture);
            }
            const QVector3D extents = vectorValue(value(
                    block.body, QStringLiteral("extents")));
            if (!extents.isNull())
                templateExtents.insert(block.name.toLower(), extents);
            QVector<WheelDefinition> wheels;
            const QVector<TextBlock> wheelBlocks = blocks(
                        block.body,
                        QStringLiteral("^\\s*wheel\\s+([A-Za-z0-9_]+)\\s*\\{"));
            for (const TextBlock &wheelBlock : wheelBlocks) {
                WheelDefinition wheel;
                wheel.name = wheelBlock.name;
                wheel.offset = vectorValue(value(
                        wheelBlock.body, QStringLiteral("offset")));
                bool radiusOk = false;
                bool widthOk = false;
                const qreal radius = value(
                        wheelBlock.body, QStringLiteral("radius"))
                        .toDouble(&radiusOk);
                const qreal width = value(
                        wheelBlock.body, QStringLiteral("width"))
                        .toDouble(&widthOk);
                if (radiusOk && radius > 0.0)
                    wheel.radius = radius;
                if (widthOk && width > 0.0)
                    wheel.width = width;
                wheels.append(wheel);
            }
            if (!wheels.isEmpty())
                templateWheels.insert(block.name.toLower(), wheels);
        }

        vehicleBlocks += blocks(
                    text,
                    QStringLiteral("^\\s*vehicle\\s+([A-Za-z0-9_]+)\\s*\\{"));
    }
    const QFileInfo zoneInfo(zoneFile);
    catalogHash.addData(QByteArrayLiteral("VehicleZoneDefinition.lua"));
    catalogHash.addData(QByteArray::number(zoneInfo.size()));
    catalogHash.addData(QByteArray::number(
                            zoneInfo.lastModified().toMSecsSinceEpoch()));

    for (const TextBlock &block : qAsConst(vehicleBlocks)) {
        VehicleDefinition definition;
        definition.name = normalizedVehicleName(block.name);
        const QRegularExpression templateExpression(
                    QStringLiteral("(?:^|\\n)\\s*template!\\s*=\\s*([A-Za-z0-9_]+)"));
        const QRegularExpressionMatch templateMatch =
                templateExpression.match(block.body);
        if (templateMatch.hasMatch())
            definition.templateName = templateMatch.captured(1);

        const QRegularExpression modelExpression(
                    QStringLiteral("(?:^|\\n)\\s*model\\s*\\{"));
        const QRegularExpressionMatch modelMatch = modelExpression.match(
                    block.body);
        if (modelMatch.hasMatch()) {
            const int openingBrace = block.body.indexOf(
                        QLatin1Char('{'), modelMatch.capturedEnd() - 1);
            const QString modelBody = blockAt(block.body, openingBrace);
            definition.model = value(modelBody, QStringLiteral("file"));
            bool modelScaleOk = false;
            const qreal modelScale = value(
                        modelBody, QStringLiteral("scale"))
                    .toDouble(&modelScaleOk);
            if (modelScaleOk && modelScale > 0.0)
                definition.modelScale = modelScale;
            definition.modelOffset = vectorValue(value(
                        modelBody, QStringLiteral("offset")));
        }
        definition.extents = vectorValue(value(block.body,
                                                QStringLiteral("extents")));
        if (definition.extents.isNull())
            definition.extents = templateExtents.value(
                        definition.templateName.toLower());
        definition.wheels = templateWheels.value(
                    definition.templateName.toLower());
        const QVector<TextBlock> wheelBlocks = blocks(
                    block.body,
                    QStringLiteral("^\\s*wheel\\s+([A-Za-z0-9_]+)\\s*\\{"));
        if (!wheelBlocks.isEmpty()) {
            definition.wheels.clear();
            for (const TextBlock &wheelBlock : wheelBlocks) {
                WheelDefinition wheel;
                wheel.name = wheelBlock.name;
                wheel.offset = vectorValue(value(
                        wheelBlock.body, QStringLiteral("offset")));
                bool radiusOk = false;
                bool widthOk = false;
                const qreal radius = value(
                        wheelBlock.body, QStringLiteral("radius"))
                        .toDouble(&radiusOk);
                const qreal width = value(
                        wheelBlock.body, QStringLiteral("width"))
                        .toDouble(&widthOk);
                if (radiusOk && radius > 0.0)
                    wheel.radius = radius;
                if (widthOk && width > 0.0)
                    wheel.width = width;
                definition.wheels.append(wheel);
            }
        }
        definition.texture = templateTextures.value(
                    definition.templateName.toLower());
        const QVector<TextBlock> skins = blocks(
                    block.body,
                    QStringLiteral("^\\s*(skin)\\s*\\{"));
        if (!skins.isEmpty()) {
            const QString texture = value(skins.first().body,
                                          QStringLiteral("texture"));
            if (!texture.isEmpty())
                definition.texture = texture;
        }
        if (definition.texture.isEmpty())
            definition.texture = templateTextures.value(
                        block.name.toLower());
        if (definition.model.isEmpty())
            continue;
        mVehicles.insert(definition.name.toLower(), definition);
        mVehicles.insert(block.name.toLower(), definition);
    }

    const QString zoneText = readTextFile(zoneFile);
    QVector<QString> zoneOrder;
    auto rememberZone = [&zoneOrder](const QString &name) {
        if (!zoneOrder.contains(name))
            zoneOrder.append(name);
    };
    auto setVehicle = [](QVector<ZoneVehicle> *vehicles,
                         const ZoneVehicle &vehicle) {
        for (int index = 0; index < vehicles->size(); ++index) {
            if (vehicles->at(index).name.compare(
                        vehicle.name, Qt::CaseInsensitive) == 0) {
                (*vehicles)[index] = vehicle;
                return;
            }
        }
        vehicles->append(vehicle);
    };

    const QRegularExpression zoneInitExpression(
                QStringLiteral("(?:^|\\n)\\s*VehicleZoneDistribution\\.([A-Za-z0-9_]+)\\s*=\\s*\\{\\s*\\}\\s*;?"));
    QRegularExpressionMatchIterator zoneInitMatches =
            zoneInitExpression.globalMatch(zoneText);
    while (zoneInitMatches.hasNext()) {
        const QString name = zoneInitMatches.next().captured(1).toLower();
        mZoneDefinitions[name];
        rememberZone(name);
    }

    const QRegularExpression directVehicleExpression(
                QStringLiteral("VehicleZoneDistribution\\.([A-Za-z0-9_]+)\\.vehicles\\[\"([^\"]+)\"\\]\\s*=\\s*\\{[^}]*spawnChance\\s*=\\s*([0-9.]+)"));
    QRegularExpressionMatchIterator directVehicleMatches =
            directVehicleExpression.globalMatch(zoneText);
    while (directVehicleMatches.hasNext()) {
        const QRegularExpressionMatch match = directVehicleMatches.next();
        const QString zoneName = match.captured(1).toLower();
        ZoneVehicle vehicle;
        vehicle.name = normalizedVehicleName(match.captured(2));
        vehicle.weight = match.captured(3).toDouble();
        setVehicle(&mZoneDefinitions[zoneName].vehicles, vehicle);
        rememberZone(zoneName);
    }

    QHash<QString, QVector<ZoneVehicle>> vehicleTables;
    const QRegularExpression tableVehicleExpression(
                QStringLiteral("(?:^|\\n)\\s*([A-Za-z0-9_]+)\\[\"([^\"]+)\"\\]\\s*=\\s*\\{[^}]*spawnChance\\s*=\\s*([0-9.]+)"));
    QRegularExpressionMatchIterator tableVehicleMatches =
            tableVehicleExpression.globalMatch(zoneText);
    while (tableVehicleMatches.hasNext()) {
        const QRegularExpressionMatch match = tableVehicleMatches.next();
        ZoneVehicle vehicle;
        vehicle.name = normalizedVehicleName(match.captured(2));
        vehicle.weight = match.captured(3).toDouble();
        setVehicle(&vehicleTables[match.captured(1).toLower()], vehicle);
    }

    const QRegularExpression tableLinkExpression(
                QStringLiteral("VehicleZoneDistribution\\.([A-Za-z0-9_]+)\\.vehicles\\s*=\\s*([A-Za-z0-9_]+)"));
    QRegularExpressionMatchIterator tableLinkMatches =
            tableLinkExpression.globalMatch(zoneText);
    while (tableLinkMatches.hasNext()) {
        const QRegularExpressionMatch match = tableLinkMatches.next();
        const QString zoneName = match.captured(1).toLower();
        const QString tableName = match.captured(2).toLower();
        if (vehicleTables.contains(tableName))
            mZoneDefinitions[zoneName].vehicles = vehicleTables.value(tableName);
        rememberZone(zoneName);
    }

    const QRegularExpression propertyExpression(
                QStringLiteral("VehicleZoneDistribution\\.([A-Za-z0-9_]+)\\.(chanceToSpawnNormal|chanceToSpawnBurnt|spawnRate|chanceOfOverCar|chanceToSpawnSpecial|randomAngle|specialCar|burntCar|forceSpawn)\\s*=\\s*([^;\\r\\n]+)"));
    QRegularExpressionMatchIterator propertyMatches =
            propertyExpression.globalMatch(zoneText);
    while (propertyMatches.hasNext()) {
        const QRegularExpressionMatch match = propertyMatches.next();
        const QString zoneName = match.captured(1).toLower();
        const QString property = match.captured(2);
        const QString valueText = match.captured(3).trimmed();
        ZoneDefinition &definition = mZoneDefinitions[zoneName];
        if (property == QLatin1String("chanceToSpawnNormal"))
            definition.chanceToSpawnNormal = valueText.toInt();
        else if (property == QLatin1String("chanceToSpawnBurnt"))
            definition.chanceToSpawnBurnt = valueText.toInt();
        else if (property == QLatin1String("spawnRate"))
            definition.spawnRate = valueText.toInt();
        else if (property == QLatin1String("chanceOfOverCar"))
            definition.chanceOfOverCar = valueText.toInt();
        else if (property == QLatin1String("chanceToSpawnSpecial"))
            definition.chanceToSpawnSpecial = valueText.toInt();
        else if (property == QLatin1String("randomAngle"))
            definition.randomAngle = valueText.compare(
                        QLatin1String("true"), Qt::CaseInsensitive) == 0;
        else if (property == QLatin1String("specialCar"))
            definition.specialCar = valueText.compare(
                        QLatin1String("true"), Qt::CaseInsensitive) == 0;
        else if (property == QLatin1String("burntCar"))
            definition.burntCar = valueText.compare(
                        QLatin1String("true"), Qt::CaseInsensitive) == 0;
        else if (property == QLatin1String("forceSpawn"))
            definition.forceSpawn = valueText.compare(
                        QLatin1String("true"), Qt::CaseInsensitive) == 0;
        rememberZone(zoneName);
    }

    QVector<QPair<QString, QString>> aliases;
    const QRegularExpression aliasExpression(
                QStringLiteral("VehicleZoneDistribution\\.([A-Za-z0-9_]+)\\s*=\\s*VehicleZoneDistribution\\.([A-Za-z0-9_]+)"));
    QRegularExpressionMatchIterator aliasMatches =
            aliasExpression.globalMatch(zoneText);
    while (aliasMatches.hasNext()) {
        const QRegularExpressionMatch match = aliasMatches.next();
        aliases.append(qMakePair(match.captured(1).toLower(),
                                 match.captured(2).toLower()));
    }
    for (const QPair<QString, QString> &alias : qAsConst(aliases)) {
        if (!mZoneDefinitions.contains(alias.second))
            continue;
        mZoneDefinitions.insert(alias.first,
                                mZoneDefinitions.value(alias.second));
        rememberZone(alias.first);
    }
    for (const QString &zoneName : qAsConst(zoneOrder)) {
        if (mZoneDefinitions.value(zoneName).specialCar)
            mSpecialZoneNames.append(zoneName);
    }

    auto hashFileInfo = [&catalogHash](const QString &path) {
        const QFileInfo info(path);
        if (!info.isFile())
            return;
        catalogHash.addData(info.absoluteFilePath().toLower().toUtf8());
        catalogHash.addData(QByteArray::number(info.size()));
        catalogHash.addData(QByteArray::number(
                                info.lastModified().toMSecsSinceEpoch()));
    };
    for (const ModelDefinition &model : qAsConst(mModels)) {
        QString relative = model.mesh;
        const int geometrySeparator = relative.indexOf(QLatin1Char('|'));
        if (geometrySeparator >= 0)
            relative = relative.left(geometrySeparator);
        QString fbxRelative = relative;
        if (!fbxRelative.endsWith(QLatin1String(".fbx"),
                                  Qt::CaseInsensitive)) {
            fbxRelative += QLatin1String(".fbx");
        }
        const QString fbxPath = QDir(media).filePath(
                    QStringLiteral("models_X/") + fbxRelative);
        if (QFileInfo::exists(fbxPath)) {
            hashFileInfo(fbxPath);
        } else {
            QString legacyRelative = relative;
            if (!legacyRelative.endsWith(QLatin1String(".txt"),
                                         Qt::CaseInsensitive)) {
                legacyRelative += QLatin1String(".txt");
            }
            hashFileInfo(QDir(media).filePath(
                             QStringLiteral("models/")
                             + legacyRelative));
        }
    }
    QSet<QString> textureNames;
    for (const VehicleDefinition &vehicle : qAsConst(mVehicles))
        textureNames.insert(vehicle.texture);
    for (const ModelDefinition &model : qAsConst(mModels))
        textureNames.insert(model.texture);
    textureNames.remove(QString());
    for (QString textureName : qAsConst(textureNames)) {
        if (!textureName.endsWith(QLatin1String(".png"),
                                  Qt::CaseInsensitive)) {
            textureName += QLatin1String(".png");
        }
        hashFileInfo(QDir(media).filePath(
                         QStringLiteral("textures/") + textureName));
    }
    mCatalogFingerprint = QString::fromLatin1(
                catalogHash.result().toHex().left(24));

    mGameDirectory = gameDirectory;
    mMediaDirectory = media;
    mCatalogReady = mVehicles.contains(QLatin1String("base.carnormal"))
            && mModels.contains(QLatin1String("vehicles_carnormal"))
            && !mZoneDefinitions.value(
                QLatin1String("parkingstall")).vehicles.isEmpty();
    return mCatalogReady;
}

QString VehicleMeshPreview::chooseVehicle(const QString &distribution,
                                          quint32 seed) const
{
    QVector<ZoneVehicle> vehicles = mZoneDefinitions.value(
                distribution).vehicles;
    if (vehicles.isEmpty())
        vehicles = mZoneDefinitions.value(
                    QLatin1String("parkingstall")).vehicles;
    if (vehicles.isEmpty())
        return QStringLiteral("Base.CarNormal");

    qreal total = 0.0;
    for (const ZoneVehicle &vehicle : qAsConst(vehicles)) {
        if (mVehicles.contains(vehicle.name.toLower()))
            total += vehicle.weight;
    }
    if (total <= 0.0)
        return QStringLiteral("Base.CarNormal");
    const quint32 hash = mixedSeed(qHash(distribution) ^ seed);
    const qreal target = (hash % 1000000) / 1000000.0 * total;
    qreal cumulative = 0.0;
    for (const ZoneVehicle &vehicle : qAsConst(vehicles)) {
        if (!mVehicles.contains(vehicle.name.toLower()))
            continue;
        cumulative += vehicle.weight;
        if (target <= cumulative)
            return vehicle.name;
    }
    return QStringLiteral("Base.CarNormal");
}

QString VehicleMeshPreview::atlasDirectory() const
{
    const QString root = mCacheRootOverride.isEmpty()
            ? PortableSettings::path(
                  QStringLiteral("cache/vehicle-preview-atlas"))
            : mCacheRootOverride;
    return QDir(root).filePath(mCatalogFingerprint);
}

VehicleMeshPreview::AtlasCache &VehicleMeshPreview::atlasForQuality(
        int quality)
{
    AtlasCache &atlas = mAtlases[quality];
    if (!atlas.loaded) {
        loadAtlas(quality, &atlas);
        atlas.loaded = true;
    }
    return atlas;
}

bool VehicleMeshPreview::loadAtlas(int quality, AtlasCache *atlas)
{
    const QDir directory(atlasDirectory());
    QFile manifest(directory.filePath(
                       QStringLiteral("atlas-%1.json").arg(quality)));
    if (!manifest.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument document = QJsonDocument::fromJson(
                manifest.readAll());
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema")).toInt() != 4
            || root.value(QStringLiteral("quality")).toInt() != quality
            || root.value(QStringLiteral("fingerprint")).toString()
            != mCatalogFingerprint) {
        return false;
    }

    QVector<AtlasPage> pages;
    const QJsonArray pageValues = root.value(
                QStringLiteral("pages")).toArray();
    for (int index = 0; index < pageValues.size(); ++index) {
        const QJsonObject value = pageValues.at(index).toObject();
        AtlasPage page;
        page.image = QImageReader(directory.filePath(
                                      value.value(QStringLiteral("file"))
                                      .toString())).read();
        if (page.image.isNull())
            return false;
        page.cursorX = value.value(QStringLiteral("cursorX")).toInt(2);
        page.cursorY = value.value(QStringLiteral("cursorY")).toInt(2);
        page.rowHeight = value.value(QStringLiteral("rowHeight")).toInt();
        pages.append(page);
    }

    QHash<QString, AtlasEntry> entries;
    const QJsonArray entryValues = root.value(
                QStringLiteral("entries")).toArray();
    for (const QJsonValue &entryValue : entryValues) {
        const QJsonObject value = entryValue.toObject();
        AtlasEntry entry;
        entry.page = value.value(QStringLiteral("page")).toInt(-1);
        entry.rect = QRect(value.value(QStringLiteral("x")).toInt(),
                           value.value(QStringLiteral("y")).toInt(),
                           value.value(QStringLiteral("width")).toInt(),
                           value.value(QStringLiteral("height")).toInt());
        entry.anchor = QPointF(value.value(QStringLiteral("anchorX"))
                               .toDouble(),
                               value.value(QStringLiteral("anchorY"))
                               .toDouble());
        entry.vehicleName = value.value(
                    QStringLiteral("vehicleName")).toString();
        entry.rasterScale = value.value(
                    QStringLiteral("rasterScale")).toDouble(1.0);
        if (entry.page < 0 || entry.page >= pages.size()
                || !entry.rect.isValid()
                || !pages.at(entry.page).image.rect()
                .contains(entry.rect)) {
            return false;
        }
        entries.insert(value.value(QStringLiteral("key")).toString(),
                       entry);
    }
    atlas->pages = pages;
    atlas->entries = entries;
    qInfo().noquote() << "Vehicle preview atlas loaded:"
                      << entries.size() << "sprites on"
                      << pages.size() << "page(s) at quality"
                      << quality / 100.0;
    return true;
}

void VehicleMeshPreview::saveAtlas(int quality, AtlasCache *atlas,
                                   bool force)
{
    if (!atlas->loaded || !atlas->dirty
            || (!force && atlas->unsavedEntries < 16)) {
        return;
    }
    const QString directoryPath = atlasDirectory();
    if (mCatalogFingerprint.isEmpty()
            || !QDir().mkpath(directoryPath)) {
        return;
    }
    const QDir directory(directoryPath);
    QJsonArray pageValues;
    for (int index = 0; index < atlas->pages.size(); ++index) {
        AtlasPage &page = atlas->pages[index];
        const QString fileName = QStringLiteral("atlas-%1-%2.png")
                .arg(quality).arg(index);
        if (page.dirty) {
            QSaveFile imageFile(directory.filePath(fileName));
            if (!imageFile.open(QIODevice::WriteOnly)
                    || !page.image.save(&imageFile, "PNG")
                    || !imageFile.commit()) {
                return;
            }
        }
        QJsonObject value;
        value.insert(QStringLiteral("file"), fileName);
        value.insert(QStringLiteral("cursorX"), page.cursorX);
        value.insert(QStringLiteral("cursorY"), page.cursorY);
        value.insert(QStringLiteral("rowHeight"), page.rowHeight);
        pageValues.append(value);
    }

    QJsonArray entryValues;
    QStringList keys = atlas->entries.keys();
    std::sort(keys.begin(), keys.end(),
              [](const QString &left, const QString &right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    for (const QString &key : qAsConst(keys)) {
        const AtlasEntry entry = atlas->entries.value(key);
        QJsonObject value;
        value.insert(QStringLiteral("key"), key);
        value.insert(QStringLiteral("page"), entry.page);
        value.insert(QStringLiteral("x"), entry.rect.x());
        value.insert(QStringLiteral("y"), entry.rect.y());
        value.insert(QStringLiteral("width"), entry.rect.width());
        value.insert(QStringLiteral("height"), entry.rect.height());
        value.insert(QStringLiteral("anchorX"), entry.anchor.x());
        value.insert(QStringLiteral("anchorY"), entry.anchor.y());
        value.insert(QStringLiteral("vehicleName"), entry.vehicleName);
        value.insert(QStringLiteral("rasterScale"), entry.rasterScale);
        entryValues.append(value);
    }
    QJsonObject root;
    root.insert(QStringLiteral("schema"), 4);
    root.insert(QStringLiteral("quality"), quality);
    root.insert(QStringLiteral("fingerprint"), mCatalogFingerprint);
    root.insert(QStringLiteral("pages"), pageValues);
    root.insert(QStringLiteral("entries"), entryValues);
    QSaveFile manifest(directory.filePath(
                           QStringLiteral("atlas-%1.json").arg(quality)));
    if (!manifest.open(QIODevice::WriteOnly))
        return;
    manifest.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!manifest.commit())
        return;
    for (AtlasPage &page : atlas->pages)
        page.dirty = false;
    atlas->dirty = false;
    atlas->unsavedEntries = 0;
}

void VehicleMeshPreview::saveAtlases(bool force)
{
    const QList<int> qualities = mAtlases.keys();
    for (int quality : qualities)
        saveAtlas(quality, &mAtlases[quality], force);
}

VehicleMeshPreview::RenderedVehicle VehicleMeshPreview::atlasEntry(
        const AtlasCache &atlas, const AtlasEntry &entry) const
{
    RenderedVehicle result;
    if (entry.page < 0 || entry.page >= atlas.pages.size())
        return result;
    result.image = atlas.pages.at(entry.page).image.copy(entry.rect);
    result.anchor = entry.anchor;
    result.vehicleName = entry.vehicleName;
    result.rasterScale = entry.rasterScale;
    return result;
}

VehicleMeshPreview::RenderedVehicle VehicleMeshPreview::addToAtlas(
        int quality, const QString &key,
        const RenderedVehicle &rendered)
{
    if (!rendered.isValid())
        return rendered;
    AtlasCache &atlas = atlasForQuality(quality);
    const int padding = 2;
    int pageIndex = -1;
    QRect rect;
    for (int index = 0; index < atlas.pages.size(); ++index) {
        AtlasPage &page = atlas.pages[index];
        int x = page.cursorX;
        int y = page.cursorY;
        int rowHeight = page.rowHeight;
        if (x + rendered.image.width() + padding > page.image.width()) {
            x = padding;
            y += rowHeight + padding;
            rowHeight = 0;
        }
        if (y + rendered.image.height() + padding > page.image.height())
            continue;
        page.cursorX = x + rendered.image.width() + padding;
        page.cursorY = y;
        page.rowHeight = qMax(rowHeight, rendered.image.height());
        pageIndex = index;
        rect = QRect(x, y, rendered.image.width(),
                     rendered.image.height());
        break;
    }
    if (pageIndex < 0) {
        int pageSize = 2048;
        const int required = qMax(rendered.image.width(),
                                  rendered.image.height()) + padding * 2;
        while (pageSize < required)
            pageSize *= 2;
        AtlasPage page;
        page.image = QImage(pageSize, pageSize,
                            QImage::Format_ARGB32_Premultiplied);
        page.image.fill(Qt::transparent);
        pageIndex = atlas.pages.size();
        rect = QRect(padding, padding, rendered.image.width(),
                     rendered.image.height());
        page.cursorX = rect.right() + 1 + padding;
        page.cursorY = padding;
        page.rowHeight = rect.height();
        atlas.pages.append(page);
    }
    AtlasPage &page = atlas.pages[pageIndex];
    QPainter painter(&page.image);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(rect.topLeft(), rendered.image);
    painter.end();
    page.dirty = true;

    AtlasEntry entry;
    entry.page = pageIndex;
    entry.rect = rect;
    entry.anchor = rendered.anchor;
    entry.vehicleName = rendered.vehicleName;
    entry.rasterScale = rendered.rasterScale;
    atlas.entries.insert(key, entry);
    atlas.dirty = true;
    ++atlas.unsavedEntries;
    saveAtlas(quality, &atlas, false);
    return rendered;
}

QString VehicleMeshPreview::atlasStatus(const QString &gameDirectory,
                                        qreal rasterScale)
{
    if (!ensureCatalog(gameDirectory))
        return QStringLiteral("Game assets unavailable");
    const int quality = qRound(qBound(0.25, rasterScale, 4.0) * 100.0);
    AtlasCache &atlas = atlasForQuality(quality);
    qint64 bytes = 0;
    const QDir directory(atlasDirectory());
    const QFileInfoList files = directory.entryInfoList(
                QStringList()
                << QStringLiteral("atlas-%1.json").arg(quality)
                << QStringLiteral("atlas-%1-*.png").arg(quality),
                QDir::Files);
    for (const QFileInfo &file : files)
        bytes += file.size();
    return QStringLiteral("%1 sprites, %2 page(s), %3 MiB on disk")
            .arg(atlas.entries.size())
            .arg(atlas.pages.size())
            .arg(bytes / 1048576.0, 0, 'f', 1);
}

bool VehicleMeshPreview::rebuildAtlas(
        const QString &gameDirectory, qreal rasterScale,
        const std::function<bool(int, int, const QString &)> &progress,
        QString *summary, QString *error)
{
    if (!ensureCatalog(gameDirectory)) {
        if (error)
            *error = QStringLiteral("Project Zomboid vehicle scripts or assets were not found");
        return false;
    }
    rasterScale = qBound(0.25, rasterScale, 4.0);
    const int quality = qRound(rasterScale * 100.0);
    saveAtlases();
    mAtlases.remove(quality);
    QMutableHashIterator<QString, RenderedVehicle> rendered(mRendered);
    const QString qualitySuffix = QLatin1Char('|')
            + QString::number(quality);
    while (rendered.hasNext()) {
        rendered.next();
        if (rendered.key().endsWith(qualitySuffix))
            rendered.remove();
    }
    QDir directory(atlasDirectory());
    const QStringList cacheFiles = directory.entryList(
                QStringList()
                << QStringLiteral("atlas-%1.json").arg(quality)
                << QStringLiteral("atlas-%1-*.png").arg(quality),
                QDir::Files);
    for (const QString &fileName : cacheFiles)
        directory.remove(fileName);

    QSet<QString> vehicleSet;
    for (const ZoneDefinition &definition : qAsConst(mZoneDefinitions)) {
        for (const ZoneVehicle &vehicle : definition.vehicles) {
            if (mVehicles.contains(vehicle.name.toLower()))
                vehicleSet.insert(vehicle.name);
        }
    }
    vehicleSet.insert(QStringLiteral("Base.CarNormal"));
    QStringList vehicles = vehicleSet.values();
    std::sort(vehicles.begin(), vehicles.end(),
              [](const QString &left, const QString &right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    QVector<int> directions;
    for (int direction = 0; direction < 16; ++direction)
        directions.append(direction);
    const int total = vehicles.size() * directions.size();
    int completed = 0;
    int failed = 0;
    bool cancelled = false;
    for (const QString &vehicleName : qAsConst(vehicles)) {
        for (int direction : qAsConst(directions)) {
            if (progress && !progress(completed, total, vehicleName)) {
                cancelled = true;
                break;
            }
            const RenderedVehicle result = previewVehicle(
                        vehicleName,
                        direction * M_PI * 2.0 / 16.0,
                        rasterScale);
            if (!result.isValid())
                ++failed;
            ++completed;
        }
        if (cancelled)
            break;
    }
    saveAtlases();
    AtlasCache &atlas = atlasForQuality(quality);
    if (summary) {
        *summary = cancelled
                ? QStringLiteral("Atlas reconstruction stopped after %1 of %2 previews. The partial cache was preserved.")
                  .arg(completed).arg(total)
                : QStringLiteral("Atlas rebuilt with %1 previews from %2 vehicles in sixteen directions on %3 page(s).")
                  .arg(atlas.entries.size()).arg(vehicles.size())
                  .arg(atlas.pages.size());
        if (failed > 0)
            *summary += QStringLiteral(" %1 preview(s) could not be rendered.")
                    .arg(failed);
    }
    QMutableHashIterator<QString, RenderedVehicle> completedRendered(
                mRendered);
    while (completedRendered.hasNext()) {
        completedRendered.next();
        if (completedRendered.key().endsWith(qualitySuffix))
            completedRendered.remove();
    }
    mAtlases.remove(quality);
    return true;
}

VehicleMeshPreview::SpawnSelection VehicleMeshPreview::selectSpawn(
        const QString &zoneName, qreal angleRadians,
        int zoneVariant, int placementVariant,
        bool applySpawnChance, bool polyline,
        qreal minimumAngleJitter, qreal maximumAngleJitter) const
{
    SpawnSelection selection;
    QString requested = zoneName.trimmed().toLower();
    if (requested.isEmpty())
        requested = QLatin1String("parkingstall");
    const bool randomTrafficJam = requested.startsWith(
                QLatin1String("rtrafficjam"));
    QString distribution = requested;
    if (randomTrafficJam)
        distribution.remove(0, 1);
    const bool trafficJam = distribution.startsWith(
                QLatin1String("trafficjam"));
    if (!mZoneDefinitions.contains(distribution))
        distribution = QLatin1String("parkingstall");

    const quint32 zoneSeed = mixedSeed(qHash(requested)
                                       ^ quint32(zoneVariant));
    const quint32 placementSeed = mixedSeed(zoneSeed
                                            ^ quint32(placementVariant));
    auto unit = [](quint32 seed) {
        return (mixedSeed(seed) % 1000000) / 1000000.0;
    };
    auto chance = [&unit](quint32 seed, int percentage) {
        return unit(seed) < qBound(0, percentage, 100) / 100.0;
    };

    if (applySpawnChance && randomTrafficJam
            && !chance(zoneSeed ^ 0x4d3c2b1aU, 10)) {
        return selection;
    }

    ZoneDefinition definition = mZoneDefinitions.value(distribution);
    if (chance(placementSeed ^ 0x11a2b3c4U,
               definition.chanceToSpawnBurnt)) {
        distribution = chance(placementSeed ^ 0x22b3c4d5U, 80)
                ? QLatin1String("normalburnt")
                : QLatin1String("specialburnt");
        definition = mZoneDefinitions.value(distribution);
    } else {
        if (definition.specialCar
                && chance(placementSeed ^ 0x33c4d5e6U,
                          definition.chanceToSpawnNormal)) {
            distribution = QLatin1String("parkingstall");
            definition = mZoneDefinitions.value(distribution);
        }
        if (!definition.burntCar && !definition.specialCar
                && !mSpecialZoneNames.isEmpty()
                && chance(placementSeed ^ 0x44d5e6f7U,
                          definition.chanceToSpawnSpecial)) {
            const int index = mixedSeed(placementSeed ^ 0x55e6f708U)
                    % mSpecialZoneNames.size();
            distribution = mSpecialZoneNames.at(index);
            definition = mZoneDefinitions.value(distribution);
        }
        if (definition.burntCar) {
            distribution = chance(placementSeed ^ 0x66f70819U, 80)
                    ? QLatin1String("normalburnt")
                    : QLatin1String("specialburnt");
            definition = mZoneDefinitions.value(distribution);
        }
    }

    if (applySpawnChance) {
        int spawnChance = 100;
        if (trafficJam)
            spawnChance = 80;
        else if (!polyline && !definition.forceSpawn)
            spawnChance = definition.spawnRate;
        if (!chance(placementSeed ^ 0x7708192aU, spawnChance))
            return selection;
    }

    selection.distributionName = distribution;
    selection.vehicleName = chooseVehicle(
                distribution, placementSeed ^ 0x88192a3bU);
    selection.angleRadians = angleRadians;
    if (definition.randomAngle) {
        selection.angleRadians = unit(placementSeed ^ 0x992a3b4cU)
                * M_PI * 2.0;
    } else if (!qFuzzyCompare(minimumAngleJitter,
                              maximumAngleJitter)) {
        selection.angleRadians += minimumAngleJitter
                + unit(placementSeed ^ 0xaa3b4c5dU)
                * (maximumAngleJitter - minimumAngleJitter);
    }
    selection.spawn = !selection.vehicleName.isEmpty();
    return selection;
}

bool VehicleMeshPreview::loadMesh(const VehicleDefinition &vehicle,
                                  Mesh *mesh) const
{
    return loadModelMesh(vehicle.model, mesh);
}

bool VehicleMeshPreview::loadModelMesh(const QString &modelName,
                                       Mesh *mesh) const
{
    if (!mesh)
        return false;
    const ModelDefinition model = mModels.value(modelName.toLower());
    if (model.mesh.isEmpty())
        return false;
    QString relative = model.mesh;
    QString geometryName;
    const int geometrySeparator = relative.indexOf(QLatin1Char('|'));
    if (geometrySeparator >= 0) {
        geometryName = relative.mid(geometrySeparator + 1);
        relative = relative.left(geometrySeparator);
    }
    if (!relative.endsWith(QLatin1String(".fbx"), Qt::CaseInsensitive))
        relative += QLatin1String(".fbx");
    const QString path = QDir(mMediaDirectory).filePath(
                QStringLiteral("models_X/") + relative);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QString legacyRelative = model.mesh;
        if (!legacyRelative.endsWith(QLatin1String(".txt"),
                                     Qt::CaseInsensitive)) {
            legacyRelative += QLatin1String(".txt");
        }
        const QString legacyPath = QDir(mMediaDirectory).filePath(
                    QStringLiteral("models/") + legacyRelative);
        return loadLegacyMesh(legacyPath, model, mesh);
    }
    const QByteArray header = file.read(24);
    file.close();
    QVector<qreal> coordinates;
    QVector<int> polygonIndices;
    QVector<qreal> uvCoordinates;
    QVector<int> uvIndices;
    QVector3D modelScale(1.0f, 1.0f, 1.0f);
    if (header.startsWith("Kaydara FBX Binary")) {
        BinaryFbxReader reader;
        if (!reader.read(path, &coordinates, &polygonIndices,
                         &uvCoordinates, &uvIndices, &modelScale,
                         geometryName)) {
            return false;
        }
    } else {
        const QString text = readTextFile(path);
        if (text.isEmpty())
            return false;
        coordinates = numericArray(text, QStringLiteral("Vertices"));
        polygonIndices = integerArray(
                    text, QStringLiteral("PolygonVertexIndex"));
        const QRegularExpression uvExpression(
                    QStringLiteral("LayerElementUV\\s*:\\s*0\\s*\\{"));
        const QRegularExpressionMatch uvMatch = uvExpression.match(text);
        if (!uvMatch.hasMatch())
            return false;
        const int uvOpeningBrace = text.indexOf(
                    QLatin1Char('{'), uvMatch.capturedEnd() - 1);
        const QString uvBlock = blockAt(text, uvOpeningBrace);
        uvCoordinates = numericArray(uvBlock, QStringLiteral("UV"));
        uvIndices = integerArray(uvBlock, QStringLiteral("UVIndex"));
    }
    if (coordinates.size() < 9 || polygonIndices.size() < 3)
        return false;
    if (uvCoordinates.size() < 6
            || uvIndices.size() != polygonIndices.size()) {
        return false;
    }

    QVector<QVector3D> positions;
    positions.reserve(coordinates.size() / 3);
    const qreal maximum = std::numeric_limits<float>::max();
    mesh->minimum = QVector3D(maximum, maximum, maximum);
    mesh->maximum = QVector3D(-maximum, -maximum, -maximum);
    for (int index = 0; index + 2 < coordinates.size(); index += 3) {
        QVector3D position(coordinates.at(index),
                           coordinates.at(index + 1),
                           coordinates.at(index + 2));
        position.setX(position.x() * modelScale.x());
        position.setY(position.y() * modelScale.y());
        position.setZ(position.z() * modelScale.z());
        if (model.invertX)
            position.setX(-position.x());
        positions.append(position);
        mesh->minimum.setX(qMin(mesh->minimum.x(), position.x()));
        mesh->minimum.setY(qMin(mesh->minimum.y(), position.y()));
        mesh->minimum.setZ(qMin(mesh->minimum.z(), position.z()));
        mesh->maximum.setX(qMax(mesh->maximum.x(), position.x()));
        mesh->maximum.setY(qMax(mesh->maximum.y(), position.y()));
        mesh->maximum.setZ(qMax(mesh->maximum.z(), position.z()));
    }

    QVector<MeshVertex> polygon;
    for (int index = 0; index < polygonIndices.size(); ++index) {
        const int rawIndex = polygonIndices.at(index);
        const bool endOfPolygon = rawIndex < 0;
        const int vertexIndex = endOfPolygon ? -rawIndex - 1 : rawIndex;
        const int uvIndex = uvIndices.at(index);
        if (vertexIndex >= 0 && vertexIndex < positions.size()
                && uvIndex >= 0 && uvIndex * 2 + 1 < uvCoordinates.size()) {
            MeshVertex vertex;
            vertex.position = positions.at(vertexIndex);
            vertex.uv = QPointF(uvCoordinates.at(uvIndex * 2),
                                1.0 - uvCoordinates.at(uvIndex * 2 + 1));
            polygon.append(vertex);
        }
        if (!endOfPolygon)
            continue;
        for (int triangleIndex = 1;
             triangleIndex + 1 < polygon.size(); ++triangleIndex) {
            MeshTriangle triangle;
            triangle.vertices[0] = polygon.at(0);
            triangle.vertices[1] = polygon.at(triangleIndex);
            triangle.vertices[2] = polygon.at(triangleIndex + 1);
            mesh->triangles.append(triangle);
        }
        polygon.clear();
    }
    return !mesh->triangles.isEmpty();
}

bool VehicleMeshPreview::loadLegacyMesh(const QString &path,
                                        const ModelDefinition &model,
                                        Mesh *mesh) const
{
    if (!mesh)
        return false;
    const QString text = readTextFile(path);
    if (text.isEmpty())
        return false;
    const QStringList lines = text.split(QRegularExpression(
            QStringLiteral("\\r?\\n")));
    int vertexCount = 0;
    int vertexBuffer = -1;
    int faceCount = 0;
    int faceData = -1;
    for (int index = 0; index < lines.size(); ++index) {
        const QString line = lines.at(index).trimmed();
        if (line == QLatin1String("# Vertex Count:")
                && index + 1 < lines.size()) {
            vertexCount = lines.at(index + 1).trimmed().toInt();
        } else if (line == QLatin1String("# Vertex Buffer:")) {
            vertexBuffer = index + 1;
        } else if (line == QLatin1String("# Number of Faces:")
                   && index + 1 < lines.size()) {
            faceCount = lines.at(index + 1).trimmed().toInt();
        } else if (line == QLatin1String("# Face Data:")) {
            faceData = index + 1;
        }
    }
    if (vertexCount < 3 || vertexBuffer < 0
            || faceCount < 1 || faceData < 0) {
        return false;
    }

    QVector<MeshVertex> vertices;
    vertices.reserve(vertexCount);
    int lineIndex = vertexBuffer;
    for (int vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
        while (lineIndex < lines.size()
               && (lines.at(lineIndex).trimmed().isEmpty()
                   || lines.at(lineIndex).trimmed().startsWith(
                       QLatin1Char('#')))) {
            ++lineIndex;
        }
        if (lineIndex + 2 >= lines.size())
            return false;
        const QStringList positionValues = lines.at(lineIndex++)
                .split(QLatin1Char(','), Qt::SkipEmptyParts);
        ++lineIndex;
        const QStringList uvValues = lines.at(lineIndex++)
                .split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (positionValues.size() < 3 || uvValues.size() < 2)
            return false;
        bool positionOk[3] = { false, false, false };
        bool uvOk[2] = { false, false };
        QVector3D position(
                    positionValues.at(0).trimmed().toDouble(&positionOk[0]),
                    positionValues.at(1).trimmed().toDouble(&positionOk[1]),
                    positionValues.at(2).trimmed().toDouble(&positionOk[2]));
        const QPointF uv(
                    uvValues.at(0).trimmed().toDouble(&uvOk[0]),
                    1.0 - uvValues.at(1).trimmed().toDouble(&uvOk[1]));
        if (!positionOk[0] || !positionOk[1] || !positionOk[2]
                || !uvOk[0] || !uvOk[1]) {
            return false;
        }
        if (model.invertX)
            position.setX(-position.x());
        MeshVertex vertex;
        vertex.position = position;
        vertex.uv = uv;
        vertices.append(vertex);
    }

    const qreal maximum = std::numeric_limits<float>::max();
    mesh->minimum = QVector3D(maximum, maximum, maximum);
    mesh->maximum = QVector3D(-maximum, -maximum, -maximum);
    for (const MeshVertex &vertex : qAsConst(vertices)) {
        mesh->minimum.setX(qMin(mesh->minimum.x(), vertex.position.x()));
        mesh->minimum.setY(qMin(mesh->minimum.y(), vertex.position.y()));
        mesh->minimum.setZ(qMin(mesh->minimum.z(), vertex.position.z()));
        mesh->maximum.setX(qMax(mesh->maximum.x(), vertex.position.x()));
        mesh->maximum.setY(qMax(mesh->maximum.y(), vertex.position.y()));
        mesh->maximum.setZ(qMax(mesh->maximum.z(), vertex.position.z()));
    }

    lineIndex = faceData;
    for (int faceIndex = 0; faceIndex < faceCount; ++faceIndex) {
        while (lineIndex < lines.size()
               && (lines.at(lineIndex).trimmed().isEmpty()
                   || lines.at(lineIndex).trimmed().startsWith(
                       QLatin1Char('#')))) {
            ++lineIndex;
        }
        if (lineIndex >= lines.size())
            return false;
        const QStringList indices = lines.at(lineIndex++)
                .split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (indices.size() < 3)
            return false;
        bool indexOk[3] = { false, false, false };
        const int first = indices.at(0).trimmed().toInt(&indexOk[0]);
        const int second = indices.at(1).trimmed().toInt(&indexOk[1]);
        const int third = indices.at(2).trimmed().toInt(&indexOk[2]);
        if (!indexOk[0] || !indexOk[1] || !indexOk[2]
                || first < 0 || first >= vertices.size()
                || second < 0 || second >= vertices.size()
                || third < 0 || third >= vertices.size()) {
            return false;
        }
        MeshTriangle triangle;
        triangle.vertices[0] = vertices.at(first);
        triangle.vertices[1] = vertices.at(second);
        triangle.vertices[2] = vertices.at(third);
        mesh->triangles.append(triangle);
    }
    return !mesh->triangles.isEmpty();
}

VehicleMeshPreview::RenderedVehicle VehicleMeshPreview::render(
        const VehicleDefinition &vehicle, qreal angleRadians,
        qreal rasterScale)
{
    rasterScale = qBound(0.25, rasterScale, 4.0);
    const QString meshKey = vehicle.model.toLower();
    if (!mMeshes.contains(meshKey)) {
        Mesh mesh;
        if (!loadMesh(vehicle, &mesh))
            return RenderedVehicle();
        mMeshes.insert(meshKey, mesh);
    }
    const Mesh &mesh = mMeshes[meshKey];

    QString textureName = vehicle.texture;
    if (textureName.isEmpty())
        textureName = QStringLiteral("Vehicles/vehicle_carnormalshell");
    QString texturePath = QDir(mMediaDirectory).filePath(
                QStringLiteral("textures/") + textureName);
    if (!texturePath.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
        texturePath += QLatin1String(".png");
    QImage texture = QImageReader(texturePath).read();
    if (texture.isNull()) {
        texture = QImage(32, 32, QImage::Format_ARGB32_Premultiplied);
        texture.fill(QColor(92, 105, 96));
    }
    QVector<QImage> textures;
    textures.append(texture);

    const QVector3D rawSize = mesh.maximum - mesh.minimum;
    const ModelDefinition bodyModel = mModels.value(meshKey);
    const qreal vehicleScale = qMax(0.001, vehicle.modelScale);
    const qreal bodyScale = qMax(0.000001,
                                  bodyModel.scale * vehicleScale);
    const QVector3D modelOffset = vehicle.modelOffset * vehicleScale;
    const qreal width = vehicle.extents.x() > 0.0
            ? vehicle.extents.x() * vehicleScale
            : rawSize.x() * bodyScale;
    const qreal length = vehicle.extents.z() > 0.0
            ? vehicle.extents.z() * vehicleScale
            : rawSize.y() * bodyScale;
    qreal assembledMinimumZ = mesh.minimum.z() * bodyScale
            + modelOffset.y();
    for (const WheelDefinition &wheel : vehicle.wheels) {
        assembledMinimumZ = qMin(assembledMinimumZ,
                                 wheel.offset.y() * vehicleScale
                                 + modelOffset.y()
                                 - wheel.radius * vehicleScale);
    }
    const qreal verticalShift = -assembledMinimumZ;
    const qreal cosine = qCos(angleRadians);
    const qreal sine = qSin(angleRadians);

    QVector<ProjectedTriangle> triangles;
    triangles.reserve(mesh.triangles.size() + vehicle.wheels.size() * 48);
    qreal minimumX = std::numeric_limits<qreal>::max();
    qreal minimumY = std::numeric_limits<qreal>::max();
    qreal maximumX = -std::numeric_limits<qreal>::max();
    qreal maximumY = -std::numeric_limits<qreal>::max();
    for (const MeshTriangle &meshTriangle : mesh.triangles) {
        ProjectedTriangle triangle;
        triangle.textureIndex = 0;
        for (int index = 0; index < 3; ++index) {
            const QVector3D raw = meshTriangle.vertices[index].position;
            const qreal localX = raw.x() * bodyScale
                    + modelOffset.x();
            const qreal localY = raw.y() * bodyScale
                    + modelOffset.z();
            const qreal localZ = raw.z() * bodyScale
                    + modelOffset.y() + verticalShift;
            const qreal worldX = localX * cosine + localY * sine;
            const qreal worldY = localX * sine - localY * cosine;
            triangle.worldPoints[index] = QVector3D(worldX, worldY, localZ);
            triangle.points[index] = QPointF(
                        (worldX - worldY) * 32.0 * rasterScale,
                        ((worldX + worldY) * 16.0
                         - localZ * 32.0) * rasterScale);
            triangle.texturePoints[index] = QPointF(
                        meshTriangle.vertices[index].uv.x()
                        * texture.width(),
                        meshTriangle.vertices[index].uv.y()
                        * texture.height());
            triangle.depth += worldX + worldY + localZ * 0.05;
            minimumX = qMin(minimumX, triangle.points[index].x());
            minimumY = qMin(minimumY, triangle.points[index].y());
            maximumX = qMax(maximumX, triangle.points[index].x());
            maximumY = qMax(maximumY, triangle.points[index].y());
        }
        triangle.depth /= 3.0;
        triangles.append(triangle);
    }

    const QString wheelModelName = QStringLiteral("Vehicles_Wheel");
    const QString wheelMeshKey = wheelModelName.toLower();
    if (!vehicle.wheels.isEmpty()) {
        if (!mMeshes.contains(wheelMeshKey)) {
            Mesh wheelMesh;
            if (loadModelMesh(wheelModelName, &wheelMesh))
                mMeshes.insert(wheelMeshKey, wheelMesh);
        }
        if (mMeshes.contains(wheelMeshKey)) {
            const ModelDefinition wheelModel = mModels.value(wheelMeshKey);
            QString wheelTextureName = wheelModel.texture;
            if (wheelTextureName.isEmpty())
                wheelTextureName = QStringLiteral("Vehicles/vehicle_wheel");
            QString wheelTexturePath = QDir(mMediaDirectory).filePath(
                        QStringLiteral("textures/") + wheelTextureName);
            if (!wheelTexturePath.endsWith(
                        QLatin1String(".png"), Qt::CaseInsensitive)) {
                wheelTexturePath += QLatin1String(".png");
            }
            QImage wheelTexture = QImageReader(wheelTexturePath).read();
            if (wheelTexture.isNull()) {
                wheelTexture = QImage(
                            16, 16, QImage::Format_ARGB32_Premultiplied);
                wheelTexture.fill(QColor(42, 42, 42));
            }
            const int wheelTextureIndex = textures.size();
            textures.append(wheelTexture);
            const Mesh &wheelMesh = mMeshes[wheelMeshKey];
            const QVector3D wheelRawSize = wheelMesh.maximum
                    - wheelMesh.minimum;
            const qreal wheelCenterX = (wheelMesh.minimum.x()
                    + wheelMesh.maximum.x()) / 2.0;
            const qreal wheelCenterY = (wheelMesh.minimum.y()
                    + wheelMesh.maximum.y()) / 2.0;
            const qreal wheelCenterZ = (wheelMesh.minimum.z()
                    + wheelMesh.maximum.z()) / 2.0;
            for (const WheelDefinition &wheel : vehicle.wheels) {
                const QVector3D wheelOffset = wheel.offset
                        * vehicleScale + modelOffset;
                const qreal diameter = wheel.radius * 2.0
                        * vehicleScale;
                const qreal wheelWidth = wheel.width * vehicleScale;
                for (const MeshTriangle &meshTriangle
                     : wheelMesh.triangles) {
                    ProjectedTriangle triangle;
                    triangle.textureIndex = wheelTextureIndex;
                    for (int index = 0; index < 3; ++index) {
                        const QVector3D raw =
                                meshTriangle.vertices[index].position;
                        const qreal localX = wheelOffset.x()
                                + (wheelRawSize.x() == 0.0 ? 0.0
                                   : (raw.x() - wheelCenterX)
                                   / wheelRawSize.x() * wheelWidth);
                        const qreal localY = wheelOffset.z()
                                + (wheelRawSize.z() == 0.0 ? 0.0
                                   : (raw.z() - wheelCenterZ)
                                   / wheelRawSize.z() * diameter);
                        const qreal localZ = wheelOffset.y()
                                + verticalShift
                                + (wheelRawSize.y() == 0.0 ? 0.0
                                   : (raw.y() - wheelCenterY)
                                   / wheelRawSize.y() * diameter);
                        const qreal worldX = localX * cosine
                                + localY * sine;
                        const qreal worldY = localX * sine
                                - localY * cosine;
                        triangle.worldPoints[index] = QVector3D(
                                    worldX, worldY, localZ);
                        triangle.points[index] = QPointF(
                                    (worldX - worldY) * 32.0 * rasterScale,
                                    ((worldX + worldY) * 16.0
                                     - localZ * 32.0) * rasterScale);
                        triangle.texturePoints[index] = QPointF(
                                    meshTriangle.vertices[index].uv.x()
                                    * wheelTexture.width(),
                                    meshTriangle.vertices[index].uv.y()
                                    * wheelTexture.height());
                        triangle.depth += worldX + worldY
                                + localZ * 0.05;
                        minimumX = qMin(minimumX,
                                        triangle.points[index].x());
                        minimumY = qMin(minimumY,
                                        triangle.points[index].y());
                        maximumX = qMax(maximumX,
                                        triangle.points[index].x());
                        maximumY = qMax(maximumY,
                                        triangle.points[index].y());
                    }
                    triangle.depth /= 3.0;
                    triangles.append(triangle);
                }
            }
        }
    }
    if (triangles.isEmpty())
        return RenderedVehicle();

    const qreal margin = 5.0 * rasterScale;
    minimumX -= margin;
    minimumY -= margin;
    maximumX += margin;
    maximumY += margin;
    const QSize imageSize(qMax(1, int(qCeil(maximumX - minimumX))),
                          qMax(1, int(qCeil(maximumY - minimumY))));
    QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.translate(-minimumX, -minimumY);

    QPolygonF shadow;
    const QPointF footprint[] = {
        QPointF(modelOffset.x() - width / 2.0,
                modelOffset.z() - length / 2.0),
        QPointF(modelOffset.x() + width / 2.0,
                modelOffset.z() - length / 2.0),
        QPointF(modelOffset.x() + width / 2.0,
                modelOffset.z() + length / 2.0),
        QPointF(modelOffset.x() - width / 2.0,
                modelOffset.z() + length / 2.0)
    };
    for (const QPointF &point : footprint) {
        const qreal worldX = point.x() * cosine + point.y() * sine;
        const qreal worldY = point.x() * sine - point.y() * cosine;
        shadow.append(QPointF((worldX - worldY) * 32.0 * rasterScale,
                              (worldX + worldY) * 16.0 * rasterScale));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 55));
    painter.drawPolygon(shadow);

    std::sort(triangles.begin(), triangles.end(),
              [](const ProjectedTriangle &left,
                 const ProjectedTriangle &right) {
        return left.depth < right.depth;
    });
    const QVector3D light = QVector3D(-0.35f, -0.45f, 1.0f).normalized();
    for (const ProjectedTriangle &triangle : qAsConst(triangles)) {
        if (triangle.textureIndex < 0
                || triangle.textureIndex >= textures.size()) {
            continue;
        }
        const QImage &triangleTexture = textures.at(
                    triangle.textureIndex);
        QPolygonF polygon;
        polygon << triangle.points[0]
                << triangle.points[1]
                << triangle.points[2];
        QPainterPath clip;
        clip.addPolygon(polygon);
        QTransform transform;
        if (!imageTransform(triangle.texturePoints,
                            triangle.points, &transform)) {
            continue;
        }
        painter.save();
        painter.setClipPath(clip);
        painter.setTransform(transform, true);
        painter.drawImage(QPointF(0.0, 0.0), triangleTexture);
        painter.restore();

        const QVector3D edge1 = triangle.worldPoints[1]
                - triangle.worldPoints[0];
        const QVector3D edge2 = triangle.worldPoints[2]
                - triangle.worldPoints[0];
        QVector3D normal = QVector3D::crossProduct(edge1, edge2).normalized();
        const qreal brightness = 0.55
                + 0.45 * qAbs(QVector3D::dotProduct(normal, light));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0,
                                int((1.0 - brightness) * 150.0)));
        painter.drawPolygon(polygon);
    }
    painter.end();

    RenderedVehicle result;
    result.image = image;
    result.anchor = QPointF(-minimumX, -minimumY);
    result.vehicleName = vehicle.name;
    result.rasterScale = rasterScale;
    return result;
}

VehicleMeshPreview::RenderedVehicle VehicleMeshPreview::preview(
        const QString &gameDirectory, const QString &zoneName,
        qreal angleRadians, int variant, qreal rasterScale)
{
    if (!ensureCatalog(gameDirectory))
        return RenderedVehicle();
    const SpawnSelection selection = selectSpawn(
                zoneName, angleRadians, variant, variant,
                false, false, 0.0, 0.0);
    if (!selection.spawn)
        return RenderedVehicle();
    return previewVehicle(selection.vehicleName,
                          selection.angleRadians, rasterScale);
}

VehicleMeshPreview::RenderedVehicle VehicleMeshPreview::previewForPlacement(
        const QString &gameDirectory, const QString &zoneName,
        qreal angleRadians, int zoneVariant, int placementVariant,
        qreal rasterScale, bool polyline,
        qreal minimumAngleJitter, qreal maximumAngleJitter)
{
    if (!ensureCatalog(gameDirectory))
        return RenderedVehicle();
    const SpawnSelection selection = selectSpawn(
                zoneName, angleRadians, zoneVariant, placementVariant,
                true, polyline,
                minimumAngleJitter, maximumAngleJitter);
    if (!selection.spawn)
        return RenderedVehicle();
    return previewVehicle(selection.vehicleName,
                          selection.angleRadians, rasterScale);
}

VehicleMeshPreview::RenderedVehicle VehicleMeshPreview::previewVehicle(
        const QString &vehicleName, qreal angleRadians,
        qreal rasterScale)
{
    const VehicleDefinition fallback = mVehicles.value(
                QLatin1String("base.carnormal"));
    const VehicleDefinition vehicle = mVehicles.value(
                vehicleName.toLower(), fallback);
    qreal normalized = std::fmod(angleRadians, M_PI * 2.0);
    if (normalized < 0.0)
        normalized += M_PI * 2.0;
    const int direction = qRound(normalized / (M_PI * 2.0) * 16.0) % 16;
    rasterScale = qBound(0.25, rasterScale, 4.0);
    const int quality = qRound(rasterScale * 100.0);
    const QString key = vehicle.name.toLower()
            + QLatin1Char('|') + QString::number(direction)
            + QLatin1Char('|') + QString::number(quality);
    if (!mRendered.contains(key)) {
        AtlasCache &atlas = atlasForQuality(quality);
        if (atlas.entries.contains(key)) {
            mRendered.insert(key, atlasEntry(
                                 atlas, atlas.entries.value(key)));
            ++mAtlasDiskHits;
        } else {
            const int nearbyDirection = ((direction + 1) / 2 * 2) % 16;
            const QString nearbyKey = vehicle.name.toLower()
                    + QLatin1Char('|')
                    + QString::number(nearbyDirection)
                    + QLatin1Char('|') + QString::number(quality);
            if ((direction % 2) != 0
                    && atlas.entries.contains(nearbyKey)) {
                mRendered.insert(key, atlasEntry(
                                     atlas,
                                     atlas.entries.value(nearbyKey)));
                ++mAtlasDiskHits;
            } else {
                const RenderedVehicle rendered = render(
                            vehicle, direction * M_PI * 2.0 / 16.0,
                            rasterScale);
                mRendered.insert(key, addToAtlas(
                                     quality, key, rendered));
            }
        }
    }
    RenderedVehicle result = mRendered.value(key);
    if (result.isValid() || vehicle.name.compare(
                fallback.name, Qt::CaseInsensitive) == 0) {
        return result;
    }
    const QString fallbackKey = fallback.name.toLower()
            + QLatin1Char('|') + QString::number(direction)
            + QLatin1Char('|') + QString::number(quality);
    if (!mRendered.contains(fallbackKey)) {
        AtlasCache &atlas = atlasForQuality(quality);
        if (atlas.entries.contains(fallbackKey)) {
            mRendered.insert(fallbackKey, atlasEntry(
                                 atlas, atlas.entries.value(fallbackKey)));
            ++mAtlasDiskHits;
        } else {
            const RenderedVehicle rendered = render(
                        fallback,
                        direction * M_PI * 2.0 / 16.0,
                        rasterScale);
            mRendered.insert(fallbackKey, addToAtlas(
                                 quality, fallbackKey, rendered));
        }
    }
    result = mRendered.value(fallbackKey);
    if (result.isValid()) {
        result.vehicleName += QStringLiteral(" fallback for ")
                + vehicle.name;
    }
    return result;
}

bool VehicleMeshPreview::validate(const QString &gameDirectory,
                                  QString *summary, QString *error)
{
    QTemporaryDir atlasTemporaryDirectory;
    if (!atlasTemporaryDirectory.isValid()) {
        if (error)
            *error = QStringLiteral("A temporary vehicle atlas directory could not be created");
        return false;
    }
    VehicleMeshPreview preview;
    preview.mCacheRootOverride = atlasTemporaryDirectory.path();
    if (!preview.ensureCatalog(gameDirectory)) {
        if (error)
            *error = QStringLiteral("Project Zomboid vehicle scripts or assets were not found");
        return false;
    }
    const ZoneDefinition parkingDefinition = preview.mZoneDefinitions.value(
                QLatin1String("parkingstall"));
    const ZoneDefinition policeDefinition = preview.mZoneDefinitions.value(
                QLatin1String("police"));
    const ZoneDefinition trafficDefinition = preview.mZoneDefinitions.value(
                QLatin1String("trafficjamw"));
    const ZoneDefinition businessDefinition = preview.mZoneDefinitions.value(
                QLatin1String("business"));
    const ZoneDefinition businessAliasDefinition =
            preview.mZoneDefinitions.value(QLatin1String("business2"));
    if (parkingDefinition.vehicles.size() < 10
            || policeDefinition.vehicles.size() != 3
            || trafficDefinition.vehicles.size() < 10
            || businessDefinition.vehicles.size()
            != businessAliasDefinition.vehicles.size()
            || preview.mSpecialZoneNames.size() < 10) {
        if (error)
            *error = QStringLiteral("Vehicle zone categories, shared tables, or weighted aliases were not parsed completely");
        return false;
    }

    int policeSpecific = 0;
    int policeNormal = 0;
    int junkBurnt = 0;
    int trafficBurnt = 0;
    int parkingSpecial = 0;
    int parkingSpawned = 0;
    int trafficSpawned = 0;
    bool trailerRandomAngle = false;
    const int distributionSamples = 2000;
    for (int variant = 0; variant < distributionSamples; ++variant) {
        const SpawnSelection police = preview.selectSpawn(
                    QStringLiteral("police"), 0.0, 101, variant,
                    false, false, 0.0, 0.0);
        if (police.distributionName == QLatin1String("police"))
            ++policeSpecific;
        else if (police.distributionName == QLatin1String("parkingstall"))
            ++policeNormal;

        const SpawnSelection junk = preview.selectSpawn(
                    QStringLiteral("junkyard"), 0.0, 202, variant,
                    false, false, 0.0, 0.0);
        if (junk.distributionName == QLatin1String("normalburnt")
                || junk.distributionName == QLatin1String("specialburnt")) {
            ++junkBurnt;
        }

        const SpawnSelection trafficSelection = preview.selectSpawn(
                    QStringLiteral("trafficjamw"), 0.0, 303, variant,
                    false, false, 0.0, 0.0);
        if (trafficSelection.distributionName
                == QLatin1String("normalburnt")
                || trafficSelection.distributionName
                == QLatin1String("specialburnt")) {
            ++trafficBurnt;
        }

        const SpawnSelection parking = preview.selectSpawn(
                    QStringLiteral("parkingstall"), 0.0, 404, variant,
                    false, false, 0.0, 0.0);
        if (preview.mZoneDefinitions.value(
                    parking.distributionName).specialCar) {
            ++parkingSpecial;
        }
        if (preview.selectSpawn(
                    QStringLiteral("parkingstall"), 0.0, 505, variant,
                    true, false, 0.0, 0.0).spawn) {
            ++parkingSpawned;
        }
        if (preview.selectSpawn(
                    QStringLiteral("trafficjamw"), 0.0, 606, variant,
                    true, false, 0.0, 0.0).spawn) {
            ++trafficSpawned;
        }

        const SpawnSelection trailer = preview.selectSpawn(
                    QStringLiteral("trailerpark"), 0.0, 707, variant,
                    false, false, 0.0, 0.0);
        if (trailer.distributionName == QLatin1String("trailerpark")
                && qAbs(trailer.angleRadians) > 0.05) {
            trailerRandomAngle = true;
        }
    }
    auto withinPercent = [distributionSamples](int count,
                                                int minimum,
                                                int maximum) {
        return count * 100 >= minimum * distributionSamples
                && count * 100 <= maximum * distributionSamples;
    };
    if (!withinPercent(policeSpecific, 20, 40)
            || !withinPercent(policeNormal, 55, 75)
            || !withinPercent(junkBurnt, 30, 50)
            || !withinPercent(trafficBurnt, 72, 88)
            || !withinPercent(parkingSpecial, 2, 9)
            || !withinPercent(parkingSpawned, 12, 20)
            || !withinPercent(trafficSpawned, 75, 85)
            || !trailerRandomAngle) {
        if (error)
            *error = QStringLiteral("Deterministic vehicle previews did not preserve default category, density, or orientation probabilities");
        return false;
    }
    const RenderedVehicle north = preview.preview(
                gameDirectory, QStringLiteral("parkingstall"),
                directionAngle(QStringLiteral("N")), 0);
    const RenderedVehicle highQuality = preview.preview(
                gameDirectory, QStringLiteral("parkingstall"),
                directionAngle(QStringLiteral("N")), 0, 2.0);
    const RenderedVehicle east = preview.preview(
                gameDirectory, QStringLiteral("parkingstall"),
                directionAngle(QStringLiteral("E")), 1);
    const RenderedVehicle traffic = preview.preview(
                gameDirectory, QStringLiteral("trafficjamw"),
                directionAngle(QStringLiteral("W")), 0);
    const VehicleDefinition taxiVehicle = preview.mVehicles.value(
                QLatin1String("base.cartaxi"));
    const RenderedVehicle taxi = preview.render(
                taxiVehicle, 0.0, 2.5);
    const VehicleDefinition linkedVehicle = preview.mVehicles.value(
                QLatin1String("base.moderncar_martin"));
    const RenderedVehicle linked = preview.render(linkedVehicle, 0.0);
    if (!north.isValid() || !highQuality.isValid()
            || !east.isValid() || !traffic.isValid()
            || !taxi.isValid()) {
        if (error) {
            *error = QStringLiteral("FBX render states: parking north %1, parking east %2, traffic jam %3")
                    .arg(north.isValid() ? QStringLiteral("ready")
                                         : QStringLiteral("failed"))
                    .arg(east.isValid() ? QStringLiteral("ready")
                                        : QStringLiteral("failed"))
                    .arg(traffic.isValid() ? QStringLiteral("ready")
                                           : QStringLiteral("failed"));
        }
        return false;
    }
    const QSizeF taxiLogicalSize(
                taxi.image.width() / taxi.rasterScale,
                taxi.image.height() / taxi.rasterScale);
    if (qAbs(taxiVehicle.modelScale - 1.82) > 0.001
            || qAbs(taxiVehicle.modelOffset.y() - 0.2527) > 0.001
            || taxiLogicalSize.width() < 100.0
            || taxiLogicalSize.width() > 420.0
            || taxiLogicalSize.height() < 70.0
            || taxiLogicalSize.height() > 320.0) {
        if (error)
            *error = QStringLiteral("Vehicle model scale, offset, or game-scale raster dimensions were not preserved");
        return false;
    }
    const QStringList trailerNames = {
        QStringLiteral("base.trailer_horsebox"),
        QStringLiteral("base.trailer_livestock")
    };
    for (const QString &trailerName : trailerNames) {
        const VehicleDefinition trailer = preview.mVehicles.value(trailerName);
        Mesh trailerMesh;
        if (!preview.loadMesh(trailer, &trailerMesh)) {
            if (error)
                *error = QStringLiteral("Trailer FBX mesh could not be loaded: %1")
                        .arg(trailerName);
            return false;
        }
        const QVector3D size = (trailerMesh.maximum - trailerMesh.minimum)
                * float(preview.mModels.value(trailer.model.toLower()).scale
                        * trailer.modelScale);
        if (size.x() < 1.5f || size.x() > 3.0f
                || size.y() < 3.0f || size.y() > 6.0f) {
            if (error)
                *error = QStringLiteral("Trailer FBX node scaling produced an invalid body size: %1 (%2 x %3)")
                        .arg(trailerName).arg(size.x()).arg(size.y());
            return false;
        }
    }
    if (highQuality.image.width() <= north.image.width()
            || highQuality.image.height() <= north.image.height()) {
        if (error)
            *error = QStringLiteral("The higher vehicle render quality did not increase the calculated image resolution");
        return false;
    }
    const QSizeF normalLogicalSize(
                north.image.width() / north.rasterScale,
                north.image.height() / north.rasterScale);
    const QSizeF highLogicalSize(
                highQuality.image.width() / highQuality.rasterScale,
                highQuality.image.height() / highQuality.rasterScale);
    const QPointF normalLogicalAnchor = north.anchor / north.rasterScale;
    const QPointF highLogicalAnchor = highQuality.anchor
            / highQuality.rasterScale;
    if (qAbs(normalLogicalSize.width() - highLogicalSize.width()) > 2.0
            || qAbs(normalLogicalSize.height()
                    - highLogicalSize.height()) > 2.0
            || QLineF(normalLogicalAnchor, highLogicalAnchor).length() > 2.0) {
        if (error)
            *error = QStringLiteral("Vehicle render quality changed the logical display size or anchor");
        return false;
    }
    if (!linked.isValid()) {
        if (error)
            *error = QStringLiteral("The named body mesh in a multi-geometry vehicle could not be resolved");
        return false;
    }
    if (!preview.mMeshes.contains(QLatin1String("vehicles_wheel"))) {
        if (error)
            *error = QStringLiteral("The linked vehicle wheel mesh was not loaded");
        return false;
    }
    const Mesh linkedMesh = preview.mMeshes.value(
                linkedVehicle.model.toLower());
    const ModelDefinition linkedModel = preview.mModels.value(
                linkedVehicle.model.toLower());
    const qreal linkedVehicleScale = qMax(
                0.001, linkedVehicle.modelScale);
    const qreal linkedBodyScale = qMax(
                0.000001, linkedModel.scale * linkedVehicleScale);
    const qreal linkedModelOffsetY = linkedVehicle.modelOffset.y()
            * linkedVehicleScale;
    qreal linkedMinimumZ = linkedMesh.minimum.z() * linkedBodyScale
            + linkedModelOffsetY;
    for (const WheelDefinition &wheel : linkedVehicle.wheels) {
        linkedMinimumZ = qMin(linkedMinimumZ,
                              wheel.offset.y() * linkedVehicleScale
                              + linkedModelOffsetY
                              - wheel.radius * linkedVehicleScale);
    }
    const qreal linkedVerticalShift = -linkedMinimumZ;
    const qreal linkedMaximumZ = linkedMesh.maximum.z()
            * linkedBodyScale + linkedModelOffsetY
            + linkedVerticalShift;
    for (const WheelDefinition &wheel : linkedVehicle.wheels) {
        const qreal radius = wheel.radius * linkedVehicleScale;
        const qreal centerHeight = wheel.offset.y()
                * linkedVehicleScale
                + linkedModelOffsetY
                + linkedVerticalShift;
        if (centerHeight < radius - 0.001
                || centerHeight > linkedMaximumZ + radius) {
            if (error)
                *error = QStringLiteral("Vehicle wheel height was not aligned with the assembled body and ground plane");
            return false;
        }
    }
    if (north.vehicleName.contains(QLatin1String("fallback"),
                                   Qt::CaseInsensitive)
            || east.vehicleName.contains(QLatin1String("fallback"),
                                         Qt::CaseInsensitive)) {
        if (error) {
            *error = QStringLiteral("Representative vehicle distributions did not resolve their expected FBX assets");
        }
        return false;
    }
    QSet<QString> parkingModels;
    for (int variant = 0; variant < 16; ++variant) {
        const RenderedVehicle candidate = preview.preview(
                    gameDirectory, QStringLiteral("parkingstall"),
                    directionAngle(QStringLiteral("N")), variant);
        if (candidate.isValid()
                && !candidate.vehicleName.contains(
                    QLatin1String("fallback"), Qt::CaseInsensitive)) {
            parkingModels.insert(candidate.vehicleName);
        }
    }
    if (parkingModels.size() < 3) {
        if (error)
            *error = QStringLiteral("Deterministic parking previews did not produce sufficient model variety");
        return false;
    }
    int opaquePixels = 0;
    for (int y = 0; y < north.image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(
                    north.image.constScanLine(y));
        for (int x = 0; x < north.image.width(); ++x) {
            if (qAlpha(line[x]) > 0)
                ++opaquePixels;
        }
    }
    if (opaquePixels < 100) {
        if (error)
            *error = QStringLiteral("The rendered FBX preview contains too few visible pixels");
        return false;
    }
    const RenderedVehicle diagonal = preview.previewVehicle(
                north.vehicleName, M_PI / 4.0, 1.0);
    if (!diagonal.isValid()) {
        if (error)
            *error = QStringLiteral("A standard diagonal vehicle atlas direction could not be rendered");
        return false;
    }
    preview.saveAtlases();
    int atlasPages = 0;
    int atlasEntries = 0;
    for (const AtlasCache &atlas : qAsConst(preview.mAtlases)) {
        atlasPages += atlas.pages.size();
        atlasEntries += atlas.entries.size();
    }
    VehicleMeshPreview reloadedPreview;
    reloadedPreview.mCacheRootOverride = atlasTemporaryDirectory.path();
    const RenderedVehicle reloadedNorth = reloadedPreview.preview(
                gameDirectory, QStringLiteral("parkingstall"),
                directionAngle(QStringLiteral("N")), 0);
    const int diskHitsBeforeIntermediateAngle =
            reloadedPreview.mAtlasDiskHits;
    const RenderedVehicle reloadedIntermediate =
            reloadedPreview.previewVehicle(
                north.vehicleName, M_PI / 8.0, 1.0);
    if (!reloadedNorth.isValid()
            || reloadedPreview.mAtlasDiskHits < 1
            || !reloadedIntermediate.isValid()
            || reloadedPreview.mAtlasDiskHits
            != diskHitsBeforeIntermediateAngle + 1
            || reloadedNorth.image.size() != north.image.size()
            || QLineF(reloadedNorth.anchor, north.anchor).length() > 0.01
            || atlasPages < 1 || atlasEntries < 3) {
        if (error)
            *error = QStringLiteral("The persistent vehicle preview atlas could not be generated and reloaded");
        return false;
    }
    QString rebuildSummary;
    QString rebuildError;
    if (!reloadedPreview.rebuildAtlas(
                gameDirectory, 1.0,
                [](int current, int, const QString &) {
        return current < 12;
    }, &rebuildSummary, &rebuildError)
            || !rebuildSummary.contains(
                QLatin1String("stopped after 12"),
                Qt::CaseInsensitive)
            || !reloadedPreview.atlasStatus(
                gameDirectory, 1.0).startsWith(
                QLatin1String("12 sprites"))) {
        if (error)
            *error = rebuildError.isEmpty()
                    ? QStringLiteral("The vehicle atlas rebuild and cancellation workflow failed")
                    : rebuildError;
        return false;
    }
    if (summary) {
        *summary = QStringLiteral("%1 vehicle definitions, %2 model resources, %3 zone categories with %4 weighted special entries, %5 meshes cached including linked wheels, %6 parking models sampled, %7 visible pixels, police %8% specific and %9% normal, junk %10% burnt, traffic %11% burnt and %12% occupied, parking %13% occupied, quality 1x %14x%15 and 2x %16x%17 with stable logical size, game-scale taxi %18x%19, %20 atlas entries on %21 page(s) with a verified disk-cache reload")
                .arg(preview.mVehicles.size() / 2)
                .arg(preview.mModels.size())
                .arg(preview.mZoneDefinitions.size())
                .arg(preview.mSpecialZoneNames.size())
                .arg(preview.mMeshes.size())
                .arg(parkingModels.size())
                .arg(opaquePixels)
                .arg(policeSpecific * 100 / distributionSamples)
                .arg(policeNormal * 100 / distributionSamples)
                .arg(junkBurnt * 100 / distributionSamples)
                .arg(trafficBurnt * 100 / distributionSamples)
                .arg(trafficSpawned * 100 / distributionSamples)
                .arg(parkingSpawned * 100 / distributionSamples)
                .arg(north.image.width())
                .arg(north.image.height())
                .arg(highQuality.image.width())
                .arg(highQuality.image.height())
                .arg(qRound(taxiLogicalSize.width()))
                .arg(qRound(taxiLogicalSize.height()))
                .arg(atlasEntries)
                .arg(atlasPages);
    }
    return true;
}

QImage VehicleMeshPreview::validationImage(const QString &gameDirectory,
                                           QString *error)
{
    VehicleMeshPreview preview;
    if (!preview.ensureCatalog(gameDirectory)) {
        if (error)
            *error = QStringLiteral("Project Zomboid vehicle scripts or assets were not found");
        return QImage();
    }
    QVector<RenderedVehicle> vehicles;
    vehicles.append(preview.render(preview.mVehicles.value(
                                       QLatin1String("base.cartaxi")),
                                   0.0, 2.5));
    vehicles.append(preview.preview(gameDirectory,
                                    QStringLiteral("parkingstall"),
                                    M_PI_2, 1));
    vehicles.append(preview.preview(gameDirectory,
                                    QStringLiteral("trafficjamw"),
                                    M_PI * 1.5, 0));
    vehicles.append(preview.render(preview.mVehicles.value(
                                       QLatin1String("base.moderncar_martin")),
                                   0.0));
    vehicles.append(preview.render(preview.mVehicles.value(
                                       QLatin1String("base.trailer_horsebox")),
                                   0.0));
    vehicles.append(preview.render(preview.mVehicles.value(
                                       QLatin1String("base.trailer_livestock")),
                                   0.0));
    for (const RenderedVehicle &vehicle : qAsConst(vehicles)) {
        if (!vehicle.isValid()) {
            if (error)
                *error = QStringLiteral("A validation vehicle could not be rendered");
            return QImage();
        }
    }
    QImage image(1440, 300, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(42, 46, 50));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QVector<QPointF> anchors = {
        QPointF(120.0, 210.0),
        QPointF(360.0, 210.0),
        QPointF(600.0, 210.0),
        QPointF(840.0, 210.0),
        QPointF(1080.0, 210.0),
        QPointF(1320.0, 210.0)
    };
    for (int index = 0; index < vehicles.size(); ++index) {
        const RenderedVehicle &vehicle = vehicles.at(index);
        const QPointF anchor = anchors.at(index);
        painter.setPen(QPen(QColor(98, 126, 148), 1.0));
        painter.drawLine(anchor + QPointF(-80.0, -40.0),
                         anchor + QPointF(0.0, 0.0));
        painter.drawLine(anchor + QPointF(80.0, -40.0),
                         anchor + QPointF(0.0, 0.0));
        const qreal imageScale = 1.0 / qMax(
                    0.01, vehicle.rasterScale);
        painter.drawImage(QRectF(
                              anchor - vehicle.anchor * imageScale,
                              QSizeF(vehicle.image.width() * imageScale,
                                     vehicle.image.height() * imageScale)),
                          vehicle.image);
        painter.setPen(Qt::white);
        painter.drawText(QRectF(anchor.x() - 110.0, 250.0,
                                220.0, 30.0),
                         Qt::AlignHCenter | Qt::AlignTop,
                         vehicle.vehicleName);
    }
    painter.end();
    return image;
}
