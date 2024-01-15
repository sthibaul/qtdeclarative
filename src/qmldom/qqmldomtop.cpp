// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qqmldomitem_p.h"
#include "qqmldomtop_p.h"
#include "qqmldomexternalitems_p.h"
#include "qqmldommock_p.h"
#include "qqmldomelements_p.h"
#include "qqmldomastcreator_p.h"
#include "qqmldommoduleindex_p.h"
#include "qqmldomtypesreader_p.h"
#include "qqmldom_utils_p.h"

#include <QtQml/private/qqmljslexer_p.h>
#include <QtQml/private/qqmljsparser_p.h>
#include <QtQml/private/qqmljsengine_p.h>
#include <QtQml/private/qqmljsastvisitor_p.h>
#include <QtQml/private/qqmljsast_p.h>

#include <QtCore/QBasicMutex>
#include <QtCore/QCborArray>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QPair>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#if QT_FEATURE_thread
#    include <QtCore/QThread>
#endif

#include <memory>

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

namespace QQmlJS {
namespace Dom {

using std::shared_ptr;


/*!
 \internal
 \brief QQml::Dom::DomTop::loadFile
 \param filePath
 the file path to load
 \param logicalPath
 the path from the
 \param callback
 a callback called with an canonical path, the old value, and the current value.
  \param loadOptions are
 if force is true the file is always read
 */

Path DomTop::canonicalPath(const DomItem &) const
{
    return canonicalPath();
}

DomItem DomTop::containingObject(const DomItem &) const
{
    return DomItem();
}

bool DomTop::iterateDirectSubpaths(const DomItem &self, DirectVisitor visitor) const
{
    static QHash<QString, QString> knownFields;
    static QBasicMutex m;
    auto toField = [](const QString &f) mutable -> QStringView {
        QMutexLocker l(&m);
        if (!knownFields.contains(f))
            knownFields[f] = f;
        return knownFields[f];
    };
    bool cont = true;
    auto objs = m_extraOwningItems;
    auto itO = objs.cbegin();
    auto endO = objs.cend();
    while (itO != endO) {
        cont = cont && self.dvItemField(visitor, toField(itO.key()), [&self, &itO]() {
            return std::visit([&self](auto &&el) { return self.copy(el); }, *itO);
        });
        ++itO;
    }
    return cont;
}

void DomTop::clearExtraOwningItems()
{
    QMutexLocker l(mutex());
    m_extraOwningItems.clear();
}

QMap<QString, OwnerT> DomTop::extraOwningItems() const
{
    QMutexLocker l(mutex());
    QMap<QString, OwnerT> res = m_extraOwningItems;
    return res;
}

/*!
\class QQmlJS::Dom::DomUniverse

\brief Represents a set of parsed/loaded modules libraries and a plugins

This can be used to share parsing and updates between several Dom models, and kickstart a model
without reparsing everything.

The universe is peculiar, because stepping into it from an environment looses the connection with
the environment.

This implementation is a placeholder, a later patch will introduce it.
 */

ErrorGroups DomUniverse::myErrors()
{
    static ErrorGroups groups = {{ DomItem::domErrorGroup, NewErrorGroup("Universe") }};
    return groups;
}

DomUniverse::DomUniverse(const QString &universeName) : m_name(universeName) { }

std::shared_ptr<DomUniverse> DomUniverse::guaranteeUniverse(
        const std::shared_ptr<DomUniverse> &univ)
{
    const auto next = [] {
        Q_CONSTINIT static std::atomic<int> counter(0);
        return counter.fetch_add(1, std::memory_order_relaxed) + 1;
    };
    if (univ)
        return univ;

    return std::make_shared<DomUniverse>(
            QLatin1String("universe") + QString::number(next()));
}

DomItem DomUniverse::create(const QString &universeName)
{
    auto res = std::make_shared<DomUniverse>(universeName);
    return DomItem(res);
}

Path DomUniverse::canonicalPath() const
{
    return Path::Root(u"universe");
}

bool DomUniverse::iterateDirectSubpaths(const DomItem &self, DirectVisitor visitor) const
{
    bool cont = true;
    cont = cont && DomTop::iterateDirectSubpaths(self, visitor);
    cont = cont && self.dvValueField(visitor, Fields::name, name());
    cont = cont && self.dvItemField(visitor, Fields::globalScopeWithName, [this, &self]() {
        return self.subMapItem(Map(
                Path::Field(Fields::globalScopeWithName),
                [this](const DomItem &map, const QString &key) { return map.copy(globalScopeWithName(key)); },
                [this](const DomItem &) { return globalScopeNames(); }, QLatin1String("GlobalScope")));
    });
    cont = cont && self.dvItemField(visitor, Fields::qmlDirectoryWithPath, [this, &self]() {
        return self.subMapItem(Map(
                Path::Field(Fields::qmlDirectoryWithPath),
                [this](const DomItem &map, const QString &key) { return map.copy(qmlDirectoryWithPath(key)); },
                [this](const DomItem &) { return qmlDirectoryPaths(); }, QLatin1String("QmlDirectory")));
    });
    cont = cont && self.dvItemField(visitor, Fields::qmldirFileWithPath, [this, &self]() {
        return self.subMapItem(Map(
                Path::Field(Fields::qmldirFileWithPath),
                [this](const DomItem &map, const QString &key) { return map.copy(qmldirFileWithPath(key)); },
                [this](const DomItem &) { return qmldirFilePaths(); }, QLatin1String("QmldirFile")));
    });
    cont = cont && self.dvItemField(visitor, Fields::qmlFileWithPath, [this, &self]() {
        return self.subMapItem(Map(
                Path::Field(Fields::qmlFileWithPath),
                [this](const DomItem &map, const QString &key) { return map.copy(qmlFileWithPath(key)); },
                [this](const DomItem &) { return qmlFilePaths(); }, QLatin1String("QmlFile")));
    });
    cont = cont && self.dvItemField(visitor, Fields::jsFileWithPath, [this, &self]() {
        return self.subMapItem(Map(
                Path::Field(Fields::jsFileWithPath),
                [this](const DomItem &map, const QString &key) { return map.copy(jsFileWithPath(key)); },
                [this](const DomItem &) { return jsFilePaths(); }, QLatin1String("JsFile")));
    });
    cont = cont && self.dvItemField(visitor, Fields::jsFileWithPath, [this, &self]() {
        return self.subMapItem(Map(
                Path::Field(Fields::qmltypesFileWithPath),
                [this](const DomItem &map, const QString &key) { return map.copy(qmltypesFileWithPath(key)); },
                [this](const DomItem &) { return qmltypesFilePaths(); }, QLatin1String("QmltypesFile")));
    });
    return cont;
}

std::shared_ptr<OwningItem> DomUniverse::doCopy(const DomItem &) const
{
    QRegularExpression r(QRegularExpression::anchoredPattern(QLatin1String(R"(.*Copy([0-9]*)$)")));
    auto m = r.match(m_name);
    QString newName;
    if (m.hasMatch())
        newName = QStringLiteral(u"%1Copy%2").arg(m_name).arg(m.captured(1).toInt() + 1);
    else
        newName = m_name + QLatin1String("Copy");
    auto res = std::make_shared<DomUniverse>(newName);
    return res;
}

static DomType fileTypeForPath(const DomItem &self, const QString &canonicalFilePath)
{
    if (canonicalFilePath.endsWith(u".qml", Qt::CaseInsensitive)
        || canonicalFilePath.endsWith(u".qmlannotation", Qt::CaseInsensitive)) {
        return DomType::QmlFile;
    } else if (canonicalFilePath.endsWith(u".qmltypes")) {
        return DomType::QmltypesFile;
    } else if (QStringView(u"qmldir").compare(QFileInfo(canonicalFilePath).fileName(),
                                              Qt::CaseInsensitive)
               == 0) {
        return DomType::QmldirFile;
    } else if (QFileInfo(canonicalFilePath).isDir()) {
        return DomType::QmlDirectory;
    } else if (canonicalFilePath.endsWith(u".js", Qt::CaseInsensitive)) {
        return DomType::JsFile;
    }
    else {
        self.addError(DomUniverse::myErrors()
                              .error(QCoreApplication::translate("Dom::fileTypeForPath",
                                                                 "Could not detect type of file %1")
                                             .arg(canonicalFilePath))
                              .handle());
    }
    return DomType::Empty;
}

void DomUniverse::loadFile(const FileToLoad &file, Callback callback, LoadOptions,
                           std::optional<DomType> fileType)
{
    DomItem selfItem(shared_from_this());
    const auto &canonicalPath = file.canonicalPath();
    DomType fType = (bool(fileType) ? (*fileType) : fileTypeForPath(selfItem, canonicalPath));
    switch (fType) {
    case DomType::QmlFile:
    case DomType::QmltypesFile:
    case DomType::QmldirFile:
    case DomType::QmlDirectory:
    case DomType::JsFile: {
        const auto &valueChange = parse(file, fType);
        // execute callback
        if (callback) {
            Path p;
            if (fType == DomType::QmlFile)
                p = Paths::qmlFileInfoPath(canonicalPath);
            else if (fType == DomType::QmltypesFile)
                p = Paths::qmltypesFileInfoPath(canonicalPath);
            else if (fType == DomType::QmldirFile)
                p = Paths::qmldirFileInfoPath(canonicalPath);
            else if (fType == DomType::QmlDirectory)
                p = Paths::qmlDirectoryInfoPath(canonicalPath);
            else if (fType == DomType::JsFile)
                p = Paths::jsFileInfoPath(canonicalPath);
            else
                Q_ASSERT(false);
            callback(p, valueChange.formerItem, valueChange.currentItem);
        }
        return;
    }
    default:
        selfItem.addError(
                myErrors()
                        .error(tr("Ignoring request to load file %1 of unexpected type %2, "
                                  "calling callback immediately")
                                       .arg(file.canonicalPath(), domTypeToString(fType)))
                        .handle());
        Q_ASSERT(false && "loading non supported file type");
        callback(Path(), DomItem::empty, DomItem::empty);
        return;
    }
}

template<typename T>
QPair<std::shared_ptr<ExternalItemPair<T>>, std::shared_ptr<ExternalItemPair<T>>>
updateEntry(const DomItem &univ, const std::shared_ptr<T> &newItem,
            QMap<QString, std::shared_ptr<ExternalItemPair<T>>> &map, QBasicMutex *mutex)
{
    std::shared_ptr<ExternalItemPair<T>> oldValue;
    std::shared_ptr<ExternalItemPair<T>> newValue;
    QString canonicalPath = newItem->canonicalFilePath();
    QDateTime now = QDateTime::currentDateTimeUtc();
    {
        QMutexLocker l(mutex);
        auto it = map.find(canonicalPath);
        if (it != map.cend() && (*it) && (*it)->current) {
            oldValue = *it;
            QString oldCode = oldValue->current->code();
            QString newCode = newItem->code();
            if (!oldCode.isNull() && !newCode.isNull() && oldCode == newCode) {
                newValue = oldValue;
                if (newValue->current->lastDataUpdateAt() < newItem->lastDataUpdateAt())
                    newValue->current->refreshedDataAt(newItem->lastDataUpdateAt());
            } else if (oldValue->current->lastDataUpdateAt() > newItem->lastDataUpdateAt()) {
                newValue = oldValue;
            } else {
                DomItem oldValueObj = univ.copy(oldValue);
                newValue = oldValue->makeCopy(oldValueObj);
                newValue->current = newItem;
                newValue->currentExposedAt = now;
                if (newItem->isValid()) {
                    newValue->valid = newItem;
                    newValue->validExposedAt = now;
                }
                it = map.insert(it, canonicalPath, newValue);
            }
        } else {
            newValue = std::make_shared<ExternalItemPair<T>>(
                    (newItem->isValid() ? newItem : std::shared_ptr<T>()), newItem, now, now);
            map.insert(canonicalPath, newValue);
        }
    }
    return qMakePair(oldValue, newValue);
}

DomUniverse::ValueChange DomUniverse::parse(const FileToLoad &file, DomType fType)
{
    QString canonicalPath = file.canonicalPath();
    ContentWithDate codeWithDate;
    codeWithDate.content = file.content() ? file.content()->data : QString();
    codeWithDate.date = file.content() ? file.content()->date
                                       : QDateTime::fromMSecsSinceEpoch(0, QTimeZone::UTC);

    DomItem oldValue; // old ExternalItemPair (might be empty, or equal to newValue)
    DomItem newValue; // current ExternalItemPair
    DomItem univ = DomItem(shared_from_this());

    if (codeWithDate.content.isEmpty()) {
        // When the code is empty, Universe attempts to read it from the File.
        // However if it already has the most recent version of that File it just returns it
        const auto &curValueItem = getItemIfMostRecent(univ, fType, canonicalPath);
        if (curValueItem.has_value()) {
            oldValue = newValue = curValueItem.value();
            return { oldValue, newValue };
        }
        auto readResult = readFileContent(canonicalPath);
        if (std::holds_alternative<ErrorMessage>(readResult)) {
            newValue.addError(std::move(std::get<ErrorMessage>(readResult)));
            return { oldValue, newValue }; // read failed, nothing to parse
        } else {
            codeWithDate = std::get<ContentWithDate>(readResult);
        }
    }

    const auto &curValueItem = getItemIfHasSameCode(univ, fType, canonicalPath, codeWithDate);
    if (curValueItem.has_value()) {
        oldValue = newValue = curValueItem.value();
        return { oldValue, newValue };
    }

    // Value doesn't exist or considered outdated / expired.
    // Hence we do the actual parsing
    if (fType == DomType::QmlFile) {
        auto qmlFile = parseQmlFile(codeWithDate.content, file, codeWithDate.date);
        auto change = updateEntry<QmlFile>(univ, qmlFile, m_qmlFileWithPath, mutex());
        oldValue = univ.copy(change.first);
        newValue = univ.copy(change.second);
    } else if (fType == DomType::QmltypesFile) {
        auto qmltypesFile = std::make_shared<QmltypesFile>(canonicalPath, codeWithDate.content,
                                                           codeWithDate.date);
        QmltypesReader reader(univ.copy(qmltypesFile));
        reader.parse();
        auto change =
                updateEntry<QmltypesFile>(univ, qmltypesFile, m_qmltypesFileWithPath, mutex());
        oldValue = univ.copy(change.first);
        newValue = univ.copy(change.second);
    } else if (fType == DomType::QmldirFile) {
        shared_ptr<QmldirFile> qmldirFile =
                QmldirFile::fromPathAndCode(canonicalPath, codeWithDate.content);
        auto change = updateEntry<QmldirFile>(univ, qmldirFile, m_qmldirFileWithPath, mutex());
        oldValue = univ.copy(change.first);
        newValue = univ.copy(change.second);
    } else if (fType == DomType::QmlDirectory) {
        auto qmlDirectory = std::make_shared<QmlDirectory>(
                canonicalPath, codeWithDate.content.split(QLatin1Char('\n')), codeWithDate.date);
        auto change =
                updateEntry<QmlDirectory>(univ, qmlDirectory, m_qmlDirectoryWithPath, mutex());
        oldValue = univ.copy(change.first);
        newValue = univ.copy(change.second);
    } else if (fType == DomType::JsFile) {
        auto jsFile = parseJsFile(codeWithDate.content, file, codeWithDate.date);
        auto change = updateEntry<JsFile>(univ, jsFile, m_jsFileWithPath, mutex());
        oldValue = univ.copy(change.first);
        newValue = univ.copy(change.second);
    } else {
        Q_ASSERT(false);
    }
    return { oldValue, newValue };
}

void DomUniverse::removePath(const QString &path)
{
    QMutexLocker l(mutex());
    const auto toDelete = [path](const auto &it) {
        QString p = it.key();
        return p.startsWith(path) && (p.size() == path.size() || p.at(path.size()) == u'/');
    };
    m_qmlDirectoryWithPath.removeIf(toDelete);
    m_qmldirFileWithPath.removeIf(toDelete);
    m_qmlFileWithPath.removeIf(toDelete);
    m_jsFileWithPath.removeIf(toDelete);
    m_qmltypesFileWithPath.removeIf(toDelete);
}

DomUniverse::ReadResult DomUniverse::readFileContent(const QString &canonicalPath) const
{
    if (canonicalPath.isEmpty()) {
        return myErrors().error(tr("Non existing path %1").arg(canonicalPath));
    }
    QFile file(canonicalPath);
    QFileInfo fileInfo(canonicalPath);
    if (fileInfo.isDir()) {
        return ContentWithDate{ QDir(canonicalPath)
                                        .entryList(QDir::NoDotAndDotDot | QDir::Files, QDir::Name)
                                        .join(QLatin1Char('\n')),
                                QDateTime::currentDateTimeUtc() };
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return myErrors().error(
                tr("Error opening path %1: %2 %3")
                        .arg(canonicalPath, QString::number(file.error()), file.errorString()));
    }
    auto content = QString::fromUtf8(file.readAll());
    file.close();
    return ContentWithDate{ std::move(content), QDateTime::currentDateTimeUtc() };
}

std::shared_ptr<QmlFile> DomUniverse::parseQmlFile(const QString &code, const FileToLoad &file,
                                                   const QDateTime &contentDate)
{
    auto qmlFile = std::make_shared<QmlFile>(file.canonicalPath(), code, contentDate, 0,
                                             file.options().testFlag(WithRecovery)
                                                     ? QmlFile::EnableParserRecovery
                                                     : QmlFile::DisableParserRecovery);
    std::shared_ptr<DomEnvironment> envPtr;
    if (auto ptr = file.environment().lock())
        envPtr = std::move(ptr);
    else
        envPtr = std::make_shared<DomEnvironment>(
                QStringList(), DomEnvironment::Option::NoDependencies, shared_from_this());
    envPtr->addQmlFile(qmlFile);
    DomItem env(envPtr);
    if (qmlFile->isValid()) {
        createDom(MutableDomItem(env.copy(qmlFile)), file.options());
    } else {
        QString errs;
        DomItem qmlFileObj = env.copy(qmlFile);
        qmlFile->iterateErrors(qmlFileObj, [&errs](const DomItem &, const ErrorMessage &m) {
            errs += m.toString();
            errs += u"\n";
            return true;
        });
        qCWarning(domLog).noquote().nospace()
                << "Parsed invalid file " << file.canonicalPath() << errs;
    }
    return qmlFile;
}

std::shared_ptr<JsFile> DomUniverse::parseJsFile(const QString &code, const FileToLoad &file,
                                                 const QDateTime &contentDate)
{
    // WATCH OUT!
    // DOM construction for plain JS files is not yet supported
    // Only parsing of the file
    // and adding ExternalItem to the Environment will happen here
    auto jsFile = std::make_shared<JsFile>(file.canonicalPath(), code, contentDate);
    std::shared_ptr<DomEnvironment> envPtr;
    if (auto ptr = file.environment().lock())
        envPtr = std::move(ptr);
    else
        envPtr = std::make_shared<DomEnvironment>(
                QStringList(), DomEnvironment::Option::NoDependencies, shared_from_this());
    envPtr->addJsFile(jsFile);
    DomItem env(envPtr);
    if (!jsFile->isValid()) {
        QString errs;
        DomItem qmlFileObj = env.copy(jsFile);
        jsFile->iterateErrors(qmlFileObj, [&errs](const DomItem &, const ErrorMessage &m) {
            errs += m.toString();
            errs += u"\n";
            return true;
        });
        qCWarning(domLog).noquote().nospace()
                << "Parsed invalid file " << file.canonicalPath() << errs;
    }
    return jsFile;
}

/*!
    \internal
    Queries the corresponding path map attempting to get the value
    *WARNING* Usage of this function should be protected by the read lock
 */
std::shared_ptr<ExternalItemPairBase> DomUniverse::getPathValueOrNull(DomType fType,
                                                                      const QString &path) const
{
    switch (fType) {
    case DomType::QmlFile:
        return m_qmlFileWithPath.value(path);
    case DomType::QmltypesFile:
        return m_qmltypesFileWithPath.value(path);
    case DomType::QmldirFile:
        return m_qmldirFileWithPath.value(path);
    case DomType::QmlDirectory:
        return m_qmlDirectoryWithPath.value(path);
    case DomType::JsFile:
        return m_jsFileWithPath.value(path);
    default:
        Q_ASSERT(false);
    }
    return nullptr;
}

std::optional<DomItem> DomUniverse::getItemIfMostRecent(const DomItem &univ, DomType fType,
                                                        const QString &canonicalPath) const
{
    QFileInfo fInfo(canonicalPath);
    std::shared_ptr<ExternalItemPairBase> value = nullptr;
    {
        // Mutex is to sync access to the Value and Value->CurrentItem, which can be modified
        // through updateEnty method and currentItem->refreshedDataAt
        QMutexLocker l(mutex());
        value = getPathValueOrNull(fType, canonicalPath);
        if (valueHasMostRecentItem(value.get(), fInfo.lastModified()) || canonicalPath.isEmpty()) {
            return univ.copy(value);
        };
    }
    return std::nullopt;
}

std::optional<DomItem> DomUniverse::getItemIfHasSameCode(const DomItem &univ, DomType fType,
                                                         const QString &canonicalPath,
                                                         const ContentWithDate &codeWithDate) const
{
    std::shared_ptr<ExternalItemPairBase> value = nullptr;
    DomItem valueItem;
    {
        // Mutex is to sync access to the Value and Value->CurrentItem, which can be modified
        // through updateEnty method and currentItem->refreshedDataAt
        QMutexLocker l(mutex());
        auto value = getPathValueOrNull(fType, canonicalPath);
        if (valueHasSameContent(value.get(), codeWithDate.content)) {
            valueItem = univ.copy(value);
            if (value->currentItem()->lastDataUpdateAt() < codeWithDate.date)
                value->currentItem()->refreshedDataAt(codeWithDate.date);
        }
    }
    if (valueItem) {
        return valueItem;
    }
    return std::nullopt;
}

/*!
    \internal
    Checks if value has current Item and if it was not modified since last seen
    *WARNING* Usage of this function should be protected by the read lock
 */
bool DomUniverse::valueHasMostRecentItem(const ExternalItemPairBase *value,
                                         const QDateTime &lastModified)
{
    if (!value || !value->currentItem()) {
        return false;
    }
    return lastModified < value->currentItem()->lastDataUpdateAt();
}

/*!
    \internal
    Checks if value has current Item and if it has same content
    *WARNING* Usage of this function should be protected by the read lock
 */
bool DomUniverse::valueHasSameContent(const ExternalItemPairBase *value, const QString &content)
{
    if (!value || !value->currentItem()) {
        return false;
    }
    QString curContent = value->currentItem()->code();
    return !curContent.isNull() && curContent == content;
}

std::shared_ptr<OwningItem> LoadInfo::doCopy(const DomItem &self) const
{
    auto res = std::make_shared<LoadInfo>(*this);
    if (res->status() != Status::Done) {
        res->addErrorLocal(DomEnvironment::myErrors().warning(
                u"This is a copy of a LoadInfo still in progress, artificially ending it, if you "
                u"use this you will *not* resume loading"));
        DomEnvironment::myErrors()
                .warning([&self](const Sink &sink) {
                    sink(u"Copying an in progress LoadInfo, which is most likely an error (");
                    self.dump(sink);
                    sink(u")");
                })
                .handle();
        QMutexLocker l(res->mutex());
        res->m_status = Status::Done;
        res->m_toDo.clear();
        res->m_inProgress.clear();
        res->m_endCallbacks.clear();
    }
    return res;
}

Path LoadInfo::canonicalPath(const DomItem &) const
{
    return Path::Root(PathRoot::Env).field(Fields::loadInfo).key(elementCanonicalPath().toString());
}

bool LoadInfo::iterateDirectSubpaths(const DomItem &self, DirectVisitor visitor) const
{
    bool cont = OwningItem::iterateDirectSubpaths(self, visitor);
    cont = cont && self.dvValueField(visitor, Fields::status, int(status()));
    cont = cont && self.dvValueField(visitor, Fields::nLoaded, nLoaded());
    cont = cont
            && self.dvValueField(visitor, Fields::elementCanonicalPath,
                                 elementCanonicalPath().toString());
    cont = cont && self.dvValueField(visitor, Fields::nNotdone, nNotDone());
    cont = cont && self.dvValueField(visitor, Fields::nCallbacks, nCallbacks());
    return cont;
}

void LoadInfo::addEndCallback(const DomItem &self,
                              std::function<void(Path, const DomItem &, const DomItem &)> callback)
{
    if (!callback)
        return;
    {
        QMutexLocker l(mutex());
        switch (m_status) {
        case Status::NotStarted:
        case Status::Starting:
        case Status::InProgress:
        case Status::CallingCallbacks:
            m_endCallbacks.append(callback);
            return;
        case Status::Done:
            break;
        }
    }
    Path p = elementCanonicalPath();
    DomItem el = self.path(p);
    callback(p, el, el);
}

void LoadInfo::advanceLoad(const DomItem &self)
{
    Status myStatus;
    Dependency dep;
    bool depValid = false;
    {
        QMutexLocker l(mutex());
        myStatus = m_status;
        switch (myStatus) {
        case Status::NotStarted:
            m_status = Status::Starting;
            break;
        case Status::Starting:
        case Status::InProgress:
            if (!m_toDo.isEmpty()) {
                dep = m_toDo.dequeue();
                m_inProgress.append(dep);
                depValid = true;
            }
            break;
        case Status::CallingCallbacks:
        case Status::Done:
            break;
        }
    }
    switch (myStatus) {
    case Status::NotStarted:
        refreshedDataAt(QDateTime::currentDateTimeUtc());
        doAddDependencies(self);
        refreshedDataAt(QDateTime::currentDateTimeUtc());
        {
            QMutexLocker l(mutex());
            Q_ASSERT(m_status == Status::Starting);
            if (m_toDo.isEmpty() && m_inProgress.isEmpty())
                myStatus = m_status = Status::CallingCallbacks;
            else
                myStatus = m_status = Status::InProgress;
        }
        if (myStatus == Status::CallingCallbacks)
            execEnd(self);
        break;
    case Status::Starting:
    case Status::InProgress:
        if (depValid) {
            refreshedDataAt(QDateTime::currentDateTimeUtc());
            auto envPtr = self.environment().ownerAs<DomEnvironment>();
            Q_ASSERT(envPtr && "missing environment");
            if (!dep.uri.isEmpty()) {
                envPtr->loadModuleDependency(
                        dep.uri, dep.version,
                        [this, copiedSelf = self, dep](Path, const DomItem &, const DomItem &) {
                            // Need to explicitly copy self here since we might store this and
                            // call it later.
                            finishedLoadingDep(copiedSelf, dep);
                        },
                        self.errorHandler());
                Q_ASSERT(dep.filePath.isEmpty() && "dependency with both uri and file");
            } else if (!dep.filePath.isEmpty()) {
                envPtr->loadFile(
                        FileToLoad::fromFileSystem(envPtr, dep.filePath),
                        [this, copiedSelf = self, dep](Path, const DomItem &, const DomItem &) {
                            // Need to explicitly copy self here since we might store this and
                            // call it later.
                            finishedLoadingDep(copiedSelf, dep);
                        },
                        LoadOption::DefaultLoad, dep.fileType, self.errorHandler());
            } else {
                Q_ASSERT(false && "dependency without uri and filePath");
            }
        } else {
            addErrorLocal(DomEnvironment::myErrors().error(
                    tr("advanceLoad called but found no work, which should never happen")));
        }
        break;
    case Status::CallingCallbacks:
    case Status::Done:
        addErrorLocal(DomEnvironment::myErrors().error(tr(
                "advanceLoad called after work should have been done, which should never happen")));
        break;
    }
}

void LoadInfo::finishedLoadingDep(const DomItem &self, const Dependency &d)
{
    bool didRemove = false;
    bool unexpectedState = false;
    bool doEnd = false;
    {
        QMutexLocker l(mutex());
        didRemove = m_inProgress.removeOne(d);
        switch (m_status) {
        case Status::NotStarted:
        case Status::CallingCallbacks:
        case Status::Done:
            unexpectedState = true;
            break;
        case Status::Starting:
            break;
        case Status::InProgress:
            if (m_toDo.isEmpty() && m_inProgress.isEmpty()) {
                m_status = Status::CallingCallbacks;
                doEnd = true;
            }
            break;
        }
    }
    if (!didRemove) {
        addErrorLocal(DomEnvironment::myErrors().error([&self](const Sink &sink) {
            sink(u"LoadInfo::finishedLoadingDep did not find its dependency in those inProgress "
                 u"()");
            self.dump(sink);
            sink(u")");
        }));
        Q_ASSERT(false
                 && "LoadInfo::finishedLoadingDep did not find its dependency in those inProgress");
    }
    if (unexpectedState) {
        addErrorLocal(DomEnvironment::myErrors().error([&self](const Sink &sink) {
            sink(u"LoadInfo::finishedLoadingDep found an unexpected state (");
            self.dump(sink);
            sink(u")");
        }));
        Q_ASSERT(false && "LoadInfo::finishedLoadingDep did find an unexpected state");
    }
    if (doEnd)
        execEnd(self);
}

void LoadInfo::execEnd(const DomItem &self)
{
    QList<std::function<void(Path, const DomItem &, const DomItem &)>> endCallbacks;
    bool unexpectedState = false;
    {
        QMutexLocker l(mutex());
        unexpectedState = m_status != Status::CallingCallbacks;
        endCallbacks = m_endCallbacks;
        m_endCallbacks.clear();
    }
    Q_ASSERT(!unexpectedState && "LoadInfo::execEnd found an unexpected state");
    Path p = elementCanonicalPath();
    DomItem el = self.path(p);
    {
        auto cleanup = qScopeGuard([this, p, &el] {
            QList<std::function<void(Path, const DomItem &, const DomItem &)>> otherCallbacks;
            bool unexpectedState2 = false;
            {
                QMutexLocker l(mutex());
                unexpectedState2 = m_status != Status::CallingCallbacks;
                m_status = Status::Done;
                otherCallbacks = m_endCallbacks;
                m_endCallbacks.clear();
            }
            Q_ASSERT(!unexpectedState2 && "LoadInfo::execEnd found an unexpected state");
            for (auto const &cb : otherCallbacks) {
                if (cb)
                    cb(p, el, el);
            }
        });
        for (auto const &cb : endCallbacks) {
            if (cb)
                cb(p, el, el);
        }
    }
}

void LoadInfo::doAddDependencies(const DomItem &self)
{
    if (!elementCanonicalPath()) {
        DomEnvironment::myErrors()
                .error(tr("Uninitialized LoadInfo %1").arg(self.canonicalPath().toString()))
                .handle(nullptr);
        Q_ASSERT(false);
        return;
    }
    // sychronous add of all dependencies
    DomItem el = self.path(elementCanonicalPath());
    if (el.internalKind() == DomType::ExternalItemInfo) {
        DomItem currentFile = el.field(Fields::currentItem);
        DomItem currentImports = currentFile.field(Fields::imports);
        QString currentFilePath = currentFile.canonicalFilePath();
        int iEnd = currentImports.indexes();
        for (int i = 0; i < iEnd; ++i) {
            DomItem import = currentImports.index(i);
            if (const Import *importPtr = import.as<Import>()) {
                if (importPtr->uri.isDirectory()) {
                    QString path = importPtr->uri.absoluteLocalPath(currentFilePath);
                    if (!path.isEmpty()) {
                        addDependency(self,
                                      Dependency { QString(), importPtr->version, path,
                                                   DomType::QmlDirectory });
                    } else {
                        self.addError(DomEnvironment::myErrors().error(
                                tr("Ignoring dependencies for non resolved path import %1")
                                        .arg(importPtr->uri.toString())));
                    }
                } else {
                    addDependency(self,
                                  Dependency { importPtr->uri.moduleUri(), importPtr->version,
                                               QString(), DomType::ModuleIndex });
                }
            }
        }
        DomItem currentQmltypesFiles = currentFile.field(Fields::qmltypesFiles);
        int qEnd = currentQmltypesFiles.indexes();
        for (int i = 0; i < qEnd; ++i) {
            DomItem qmltypesRef = currentQmltypesFiles.index(i);
            if (const Reference *ref = qmltypesRef.as<Reference>()) {
                Path canonicalPath = ref->referredObjectPath[2];
                if (canonicalPath && !canonicalPath.headName().isEmpty())
                    addDependency(self,
                                  Dependency { QString(), Version(), canonicalPath.headName(),
                                               DomType::QmltypesFile });
            }
        }
        DomItem currentQmlFiles = currentFile.field(Fields::qmlFiles);
        currentQmlFiles.visitKeys([this, &self](const QString &, const DomItem &els) {
            return els.visitIndexes([this, &self](const DomItem &el) {
                if (const Reference *ref = el.as<Reference>()) {
                    Path canonicalPath = ref->referredObjectPath[2];
                    if (canonicalPath && !canonicalPath.headName().isEmpty())
                        addDependency(self,
                                      Dependency { QString(), Version(), canonicalPath.headName(),
                                                   DomType::QmlFile });
                }
                return true;
            });
        });
    } else if (shared_ptr<ModuleIndex> elPtr = el.ownerAs<ModuleIndex>()) {
        const auto qmldirs = elPtr->qmldirsToLoad(el);
        for (const Path &qmldirPath : qmldirs) {
            Path canonicalPath = qmldirPath[2];
            if (canonicalPath && !canonicalPath.headName().isEmpty())
                addDependency(self,
                              Dependency { QString(), Version(), canonicalPath.headName(),
                                           DomType::QmldirFile });
        }
        QString uri = elPtr->uri();
        addEndCallback(self, [uri, qmldirs](Path, const DomItem &, const DomItem &newV) {
            for (const Path &p : qmldirs) {
                DomItem qmldir = newV.path(p);
                if (std::shared_ptr<QmldirFile> qmldirFilePtr = qmldir.ownerAs<QmldirFile>()) {
                    qmldirFilePtr->ensureInModuleIndex(qmldir, uri);
                }
            }
        });
    } else if (!el) {
        self.addError(DomEnvironment::myErrors().error(
                tr("Ignoring dependencies for empty (invalid) type %1")
                        .arg(domTypeToString(el.internalKind()))));
    } else {
        self.addError(
                DomEnvironment::myErrors().error(tr("dependencies of %1 (%2) not yet implemented")
                                                         .arg(domTypeToString(el.internalKind()),
                                                              elementCanonicalPath().toString())));
    }
}

void LoadInfo::addDependency(const DomItem &self, const Dependency &dep)
{
    bool unexpectedState = false;
    {
        QMutexLocker l(mutex());
        unexpectedState = m_status != Status::Starting;
        m_toDo.enqueue(dep);
    }
    Q_ASSERT(!unexpectedState && "LoadInfo::addDependency found an unexpected state");
    DomItem env = self.environment();
    env.ownerAs<DomEnvironment>()->addWorkForLoadInfo(elementCanonicalPath());
}

/*!
\class QQmlJS::Dom::DomEnvironment

\brief Represents a consistent set of types organized in modules, it is the top level of the DOM
 */

template<typename T>
DomTop::Callback envCallbackForFile(
        const DomItem &self,
        QMap<QString, std::shared_ptr<ExternalItemInfo<T>>> DomEnvironment::*map,
        std::shared_ptr<ExternalItemInfo<T>> (DomEnvironment::*lookupF)(
                const DomItem &, const QString &, EnvLookup) const,
        DomTop::Callback loadCallback, DomTop::Callback allDirectDepsCallback,
        DomTop::Callback endCallback)
{
    const std::shared_ptr<DomEnvironment> ePtr = self.ownerAs<DomEnvironment>();
    return [selfPtr = std::weak_ptr<DomEnvironment>(ePtr),
            basePtr = ePtr->base(),
            map, lookupF,
            loadCallback = std::move(loadCallback),
            allDirectDepsCallback = std::move(allDirectDepsCallback),
            endCallback = std::move(endCallback)](
                   Path, const DomItem &, const DomItem &newItem) {
        shared_ptr<DomEnvironment> envPtr = selfPtr.lock();
        if (!envPtr)
            return;
        DomItem env = DomItem(envPtr);
        shared_ptr<ExternalItemInfo<T>> oldValue;
        shared_ptr<ExternalItemInfo<T>> newValue;
        shared_ptr<T> newItemPtr;
        if (envPtr->options() & DomEnvironment::Option::KeepValid)
            newItemPtr = newItem.field(Fields::validItem).ownerAs<T>();
        if (!newItemPtr)
            newItemPtr = newItem.field(Fields::currentItem).ownerAs<T>();
        Q_ASSERT(newItemPtr && "callbackForQmlFile reached without current qmlFile");
        {
            QMutexLocker l(envPtr->mutex());
            oldValue = ((*envPtr).*map).value(newItem.canonicalFilePath());
        }
        if (oldValue) {
            // we do not change locally loaded files (avoid loading a file more than once)
            newValue = oldValue;
        } else {
            if (basePtr) {
                DomItem baseObj(basePtr);
                oldValue = ((*basePtr).*lookupF)(baseObj, newItem.canonicalFilePath(),
                                                 EnvLookup::BaseOnly);
            }
            if (oldValue) {
                DomItem oldValueObj = env.copy(oldValue);
                newValue = oldValue->makeCopy(oldValueObj);
                if (newValue->current != newItemPtr) {
                    newValue->current = newItemPtr;
                    newValue->setCurrentExposedAt(QDateTime::currentDateTimeUtc());
                }
            } else {
                newValue = std::make_shared<ExternalItemInfo<T>>(
                        newItemPtr, QDateTime::currentDateTimeUtc());
            }
            {
                QMutexLocker l(envPtr->mutex());
                auto value = ((*envPtr).*map).value(newItem.canonicalFilePath());
                if (value) {
                    oldValue = newValue = value;
                } else {
                    ((*envPtr).*map).insert(newItem.canonicalFilePath(), newValue);
                }
            }
        }
        Path p = env.copy(newValue).canonicalPath();
        {
            auto depLoad = qScopeGuard([p, &env, envPtr, allDirectDepsCallback, endCallback] {
                if (!(envPtr->options() & DomEnvironment::Option::NoDependencies)) {
                    auto loadInfo = std::make_shared<LoadInfo>(p);
                    if (!p)
                        Q_ASSERT(false);
                    DomItem loadInfoObj = env.copy(loadInfo);
                    loadInfo->addEndCallback(loadInfoObj, allDirectDepsCallback);
                    envPtr->addLoadInfo(env, loadInfo);
                }
                if (endCallback)
                    envPtr->addAllLoadedCallback(env,
                                                 [p, endCallback](Path, const DomItem &, const DomItem &env) {
                                                     DomItem el = env.path(p);
                                                     endCallback(p, el, el);
                                                 });
            });
            if (loadCallback) {
                DomItem oldValueObj = env.copy(oldValue);
                DomItem newValueObj = env.copy(newValue);
                loadCallback(p, oldValueObj, newValueObj);
            }
            if ((envPtr->options() & DomEnvironment::Option::NoDependencies)
                && allDirectDepsCallback) {
                DomItem oldValueObj = env.copy(oldValue);
                DomItem newValueObj = env.copy(newValue);
                env.addError(DomEnvironment::myErrors().warning(
                        QLatin1String("calling allDirectDepsCallback immediately for load with "
                                      "NoDependencies of %1")
                                .arg(newItem.canonicalFilePath())));
                allDirectDepsCallback(p, oldValueObj, newValueObj);
            }
        }
    };
}

ErrorGroups DomEnvironment::myErrors() {
    static ErrorGroups res = {{NewErrorGroup("Dom")}};
    return res;
}

DomType DomEnvironment::kind() const
{
    return kindValue;
}

Path DomEnvironment::canonicalPath() const
{
    return Path::Root(u"env");
}

bool DomEnvironment::iterateDirectSubpaths(const DomItem &self, DirectVisitor visitor) const
{
    bool cont = true;
    cont = cont && DomTop::iterateDirectSubpaths(self, visitor);
    DomItem univ = universe();
    cont = cont && self.dvItemField(visitor, Fields::universe, [this]() { return universe(); });
    cont = cont && self.dvValueField(visitor, Fields::options, int(options()));
    cont = cont && self.dvItemField(visitor, Fields::base, [this]() { return base(); });
    cont = cont
            && self.dvValueLazyField(visitor, Fields::loadPaths, [this]() { return loadPaths(); });
    cont = cont && self.dvValueField(visitor, Fields::globalScopeName, globalScopeName());
    cont = cont && self.dvItemField(visitor, Fields::globalScopeWithName, [this, &self]() {
        return self.subMapItem(Map(
                Path::Field(Fields::globalScopeWithName),
                [&self, this](const DomItem &map, const QString &key) {
                    return map.copy(globalScopeWithName(self, key));
                },
                [&self, this](const DomItem &) { return globalScopeNames(self); },
                QLatin1String("GlobalScope")));
    });
    cont = cont && self.dvItemField(visitor, Fields::qmlDirectoryWithPath, [this, &self]() {
        return self.subMapItem(Map(
                Path::Field(Fields::qmlDirectoryWithPath),
                [&self, this](const DomItem &map, const QString &key) {
                    return map.copy(qmlDirectoryWithPath(self, key));
                },
                [&self, this](const DomItem &) { return qmlDirectoryPaths(self); },
                QLatin1String("QmlDirectory")));
    });
    cont = cont && self.dvItemField(visitor, Fields::qmldirFileWithPath, [this, &self]() {
        return self.subMapItem(Map(
                Path::Field(Fields::qmldirFileWithPath),
                [&self, this](const DomItem &map, const QString &key) {
                    return map.copy(qmldirFileWithPath(self, key));
                },
                [&self, this](const DomItem &) { return qmldirFilePaths(self); },
                QLatin1String("QmldirFile")));
    });
    cont = cont && self.dvItemField(visitor, Fields::qmldirWithPath, [this, &self]() {
        return self.subMapItem(Map(
                Path::Field(Fields::qmldirWithPath),
                [&self, this](const DomItem &map, const QString &key) {
                    return map.copy(qmlDirWithPath(self, key));
                },
                [&self, this](const DomItem &) { return qmlDirPaths(self); }, QLatin1String("Qmldir")));
    });
    cont = cont && self.dvItemField(visitor, Fields::qmlFileWithPath, [this, &self]() {
        return self.subMapItem(Map(
                Path::Field(Fields::qmlFileWithPath),
                [&self, this](const DomItem &map, const QString &key) {
                    return map.copy(qmlFileWithPath(self, key));
                },
                [&self, this](const DomItem &) { return qmlFilePaths(self); }, QLatin1String("QmlFile")));
    });
    cont = cont && self.dvItemField(visitor, Fields::jsFileWithPath, [this, &self]() {
        return self.subMapItem(Map(
                Path::Field(Fields::jsFileWithPath),
                [this](const DomItem &map, const QString &key) {
                    DomItem mapOw(map.owner());
                    return map.copy(jsFileWithPath(mapOw, key));
                },
                [this](const DomItem &map) {
                    DomItem mapOw = map.owner();
                    return jsFilePaths(mapOw);
                },
                QLatin1String("JsFile")));
    });
    cont = cont && self.dvItemField(visitor, Fields::qmltypesFileWithPath, [this, &self]() {
        return self.subMapItem(Map(
                Path::Field(Fields::qmltypesFileWithPath),
                [this](const DomItem &map, const QString &key) {
                    DomItem mapOw = map.owner();
                    return map.copy(qmltypesFileWithPath(mapOw, key));
                },
                [this](const DomItem &map) {
                    DomItem mapOw = map.owner();
                    return qmltypesFilePaths(mapOw);
                },
                QLatin1String("QmltypesFile")));
    });
    cont = cont && self.dvItemField(visitor, Fields::moduleIndexWithUri, [this, &self]() {
        return self.subMapItem(Map(
                Path::Field(Fields::moduleIndexWithUri),
                [this](const DomItem &map, const QString &key) {
                    return map.subMapItem(Map(
                            map.pathFromOwner().key(key),
                            [this, key](const DomItem &submap, const QString &subKey) {
                                bool ok;
                                int i = subKey.toInt(&ok);
                                if (!ok) {
                                    if (subKey.isEmpty())
                                        i = Version::Undefined;
                                    else if (subKey.compare(u"Latest", Qt::CaseInsensitive) == 0)
                                        i = Version::Latest;
                                    else
                                        return DomItem();
                                }
                                DomItem subMapOw = submap.owner();
                                std::shared_ptr<ModuleIndex> mIndex =
                                        moduleIndexWithUri(subMapOw, key, i);
                                return submap.copy(mIndex);
                            },
                            [this, key](const DomItem &subMap) {
                                QSet<QString> res;
                                DomItem subMapOw = subMap.owner();
                                for (int mVersion :
                                     moduleIndexMajorVersions(subMapOw, key, EnvLookup::Normal))
                                    if (mVersion == Version::Undefined)
                                        res.insert(QString());
                                    else
                                        res.insert(QString::number(mVersion));
                                if (!res.isEmpty())
                                    res.insert(QLatin1String("Latest"));
                                return res;
                            },
                            QLatin1String("ModuleIndex")));
                },
                [this](const DomItem &map) {
                    DomItem mapOw = map.owner();
                    return moduleIndexUris(mapOw);
                },
                QLatin1String("Map<ModuleIndex>")));
    });
    bool loadedLoadInfo = false;
    QQueue<Path> loadsWithWork;
    QQueue<Path> inProgress;
    int nAllLoadedCallbacks;
    auto ensureInfo = [&]() {
        if (!loadedLoadInfo) {
            QMutexLocker l(mutex());
            loadedLoadInfo = true;
            loadsWithWork = m_loadsWithWork;
            inProgress = m_inProgress;
            nAllLoadedCallbacks = m_allLoadedCallback.size();
        }
    };
    cont = cont
            && self.dvItemField(
                    visitor, Fields::loadsWithWork, [&ensureInfo, &self, &loadsWithWork]() {
                        ensureInfo();
                        return self.subListItem(List(
                                Path::Field(Fields::loadsWithWork),
                                [loadsWithWork](const DomItem &list, index_type i) {
                                    if (i >= 0 && i < loadsWithWork.size())
                                        return list.subDataItem(PathEls::Index(i),
                                                                loadsWithWork.at(i).toString());
                                    else
                                        return DomItem();
                                },
                                [loadsWithWork](const DomItem &) {
                                    return index_type(loadsWithWork.size());
                                },
                                nullptr, QLatin1String("Path")));
                    });
    cont = cont
            && self.dvItemField(visitor, Fields::inProgress, [&self, &ensureInfo, &inProgress]() {
                   ensureInfo();
                   return self.subListItem(List(
                           Path::Field(Fields::inProgress),
                           [inProgress](const DomItem &list, index_type i) {
                               if (i >= 0 && i < inProgress.size())
                                   return list.subDataItem(PathEls::Index(i),
                                                           inProgress.at(i).toString());
                               else
                                   return DomItem();
                           },
                           [inProgress](const DomItem &) { return index_type(inProgress.size()); },
                           nullptr, QLatin1String("Path")));
               });
    cont = cont && self.dvItemField(visitor, Fields::loadInfo, [&self, this]() {
        return self.subMapItem(Map(
                Path::Field(Fields::loadInfo),
                [this](const DomItem &map, const QString &pStr) {
                    bool hasErrors = false;
                    Path p = Path::fromString(pStr, [&hasErrors](const ErrorMessage &m) {
                        switch (m.level) {
                        case ErrorLevel::Debug:
                        case ErrorLevel::Info:
                            break;
                        case ErrorLevel::Warning:
                        case ErrorLevel::Error:
                        case ErrorLevel::Fatal:
                            hasErrors = true;
                            break;
                        }
                    });
                    if (!hasErrors)
                        return map.copy(loadInfo(p));
                    return DomItem();
                },
                [this](const DomItem &) {
                    QSet<QString> res;
                    const auto infoPaths = loadInfoPaths();
                    for (const Path &p : infoPaths)
                        res.insert(p.toString());
                    return res;
                },
                QLatin1String("LoadInfo")));
    });
    cont = cont && self.dvWrapField(visitor, Fields::imports, m_implicitImports);
    cont = cont
            && self.dvValueLazyField(visitor, Fields::nAllLoadedCallbacks,
                                     [&nAllLoadedCallbacks, &ensureInfo]() {
                                         ensureInfo();
                                         return nAllLoadedCallbacks;
                                     });
    return cont;
}

DomItem DomEnvironment::field(const DomItem &self, QStringView name) const
{
    return DomTop::field(self, name);
}

std::shared_ptr<DomEnvironment> DomEnvironment::makeCopy(const DomItem &self) const
{
    return std::static_pointer_cast<DomEnvironment>(doCopy(self));
}

std::shared_ptr<OwningItem> DomEnvironment::doCopy(const DomItem &) const
{
    shared_ptr<DomEnvironment> res;
    if (m_base)
        res = std::make_shared<DomEnvironment>(m_base, m_loadPaths, m_options);
    else
        res = std::make_shared<DomEnvironment>(
                m_loadPaths, m_options, m_universe);
    return res;
}

void DomEnvironment::loadFile(const FileToLoad &file, const Callback &callback,
                              LoadOptions loadOptions, std::optional<DomType> fileType,
                              const ErrorHandler &h)
{
    if (options() & DomEnvironment::Option::NoDependencies)
        loadFile(file, callback, DomTop::Callback(), DomTop::Callback(), loadOptions, fileType, h);
    else {
        // When the file is required to be loaded with dependencies, those dependencies
        // will be added to the "pending" queue through envCallbackForFile
        // then those should not be forgotten to be loaded.
        loadFile(file, DomTop::Callback(), DomTop::Callback(), callback, loadOptions, fileType, h);
    }
}

// TODO(QTBUG-119550) refactor this
void DomEnvironment::loadFile(const FileToLoad &file, Callback loadCallback,
                              Callback directDepsCallback, Callback endCallback,
                              LoadOptions loadOptions, std::optional<DomType> fileType,
                              const ErrorHandler &h)
{
    DomItem self(shared_from_this());
    if (file.canonicalPath().isEmpty()) {
        if (!file.content() || file.content()->data.isNull()) {
            // file's content inavailable and no path to retrieve it
            myErrors()
                    .error(tr("Non existing path to load: '%1'").arg(file.logicalPath()))
                    .handle(h);
            if (loadCallback)
                loadCallback(Path(), DomItem::empty, DomItem::empty);
            if (directDepsCallback)
                directDepsCallback(Path(), DomItem::empty, DomItem::empty);
            if (endCallback)
                addAllLoadedCallback(self, [endCallback](Path, const DomItem &, const DomItem &) {
                    endCallback(Path(), DomItem::empty, DomItem::empty);
                });
            return;
        } else {
            // fallback: path invalid but file's content is already available.
            file.canonicalPath() = file.logicalPath();
        }
    }

    shared_ptr<ExternalItemInfoBase> oldValue, newValue;
    const DomType fType =
            (bool(fileType) ? (*fileType) : fileTypeForPath(self, file.canonicalPath()));
    switch (fType) {
    case DomType::QmlDirectory: {
        {
            QMutexLocker l(mutex());
            auto it = m_qmlDirectoryWithPath.find(file.canonicalPath());
            if (it != m_qmlDirectoryWithPath.end())
                oldValue = newValue = *it;
        }
        if (!newValue && (options() & Option::NoReload) && m_base) {
            if (auto v = m_base->qmlDirectoryWithPath(self, file.canonicalPath(),
                                                      EnvLookup::Normal)) {
                oldValue = v;
                QDateTime now = QDateTime::currentDateTimeUtc();
                auto newV = std::make_shared<ExternalItemInfo<QmlDirectory>>(
                        v->current, now, v->revision(), v->lastDataUpdateAt());
                newValue = newV;
                QMutexLocker l(mutex());
                auto it = m_qmlDirectoryWithPath.find(file.canonicalPath());
                if (it != m_qmlDirectoryWithPath.end())
                    oldValue = newValue = *it;
                else
                    m_qmlDirectoryWithPath.insert(file.canonicalPath(), newV);
            }
        }
        if (!newValue) {
            universe()->loadFile(
                    file,
                    callbackForQmlDirectory(self, loadCallback, directDepsCallback, endCallback),
                    loadOptions, fType);
            return;
        }
    } break;
    case DomType::QmlFile: {
        {
            QMutexLocker l(mutex());
            auto it = m_qmlFileWithPath.find(file.canonicalPath());
            if (it != m_qmlFileWithPath.end())
                oldValue = newValue = *it;
        }
        if (!newValue && (options() & Option::NoReload) && m_base) {
            if (auto v = m_base->qmlFileWithPath(self, file.canonicalPath(), EnvLookup::Normal)) {
                oldValue = v;
                QDateTime now = QDateTime::currentDateTimeUtc();
                auto newV = std::make_shared<ExternalItemInfo<QmlFile>>(
                        v->current, now, v->revision(), v->lastDataUpdateAt());
                newValue = newV;
                QMutexLocker l(mutex());
                auto it = m_qmlFileWithPath.find(file.canonicalPath());
                if (it != m_qmlFileWithPath.end())
                    oldValue = newValue = *it;
                else
                    m_qmlFileWithPath.insert(file.canonicalPath(), newV);
            }
        }
        if (!newValue) {
            universe()->loadFile(
                    file, callbackForQmlFile(self, loadCallback, directDepsCallback, endCallback),
                    loadOptions, fType);
            return;
        }
    } break;
    case DomType::QmltypesFile: {
        {
            QMutexLocker l(mutex());
            auto it = m_qmltypesFileWithPath.find(file.canonicalPath());
            if (it != m_qmltypesFileWithPath.end())
                oldValue = newValue = *it;
        }
        if (!newValue && (options() & Option::NoReload) && m_base) {
            if (auto v = m_base->qmltypesFileWithPath(self, file.canonicalPath(),
                                                      EnvLookup::Normal)) {
                oldValue = v;
                QDateTime now = QDateTime::currentDateTimeUtc();
                auto newV = std::make_shared<ExternalItemInfo<QmltypesFile>>(
                        v->current, now, v->revision(), v->lastDataUpdateAt());
                newValue = newV;
                QMutexLocker l(mutex());
                auto it = m_qmltypesFileWithPath.find(file.canonicalPath());
                if (it != m_qmltypesFileWithPath.end())
                    oldValue = newValue = *it;
                else
                    m_qmltypesFileWithPath.insert(file.canonicalPath(), newV);
            }
        }
        if (!newValue) {
            universe()->loadFile(
                    file,
                    callbackForQmltypesFile(self, loadCallback, directDepsCallback, endCallback),
                    loadOptions, fType);
            return;
        }
    } break;
    case DomType::QmldirFile: {
        {
            QMutexLocker l(mutex());
            auto it = m_qmldirFileWithPath.find(file.canonicalPath());
            if (it != m_qmldirFileWithPath.end())
                oldValue = newValue = *it;
        }
        if (!newValue && (options() & Option::NoReload) && m_base) {
            if (auto v =
                        m_base->qmldirFileWithPath(self, file.canonicalPath(), EnvLookup::Normal)) {
                oldValue = v;
                QDateTime now = QDateTime::currentDateTimeUtc();
                auto newV = std::make_shared<ExternalItemInfo<QmldirFile>>(
                        v->current, now, v->revision(), v->lastDataUpdateAt());
                newValue = newV;
                QMutexLocker l(mutex());
                auto it = m_qmldirFileWithPath.find(file.canonicalPath());
                if (it != m_qmldirFileWithPath.end())
                    oldValue = newValue = *it;
                else
                    m_qmldirFileWithPath.insert(file.canonicalPath(), newV);
            }
        }
        if (!newValue) {
            universe()->loadFile(
                    file,
                    callbackForQmldirFile(self, loadCallback, directDepsCallback, endCallback),
                    loadOptions, fType);
            return;
        }
    } break;
    case DomType::JsFile: {
        universe()->loadFile(file,
                             callbackForJSFile(self, loadCallback, directDepsCallback, endCallback),
                             loadOptions, fType);
        return;
    } break;
    default: {
        myErrors().error(tr("Unexpected file to load: '%1'").arg(file.canonicalPath())).handle(h);
        if (loadCallback)
            loadCallback(self.canonicalPath(), DomItem::empty, DomItem::empty);
        if (directDepsCallback)
            directDepsCallback(self.canonicalPath(), DomItem::empty, DomItem::empty);
        if (endCallback)
            endCallback(self.canonicalPath(), DomItem::empty, DomItem::empty);
        return;
    } break;
    }
    Path p = self.copy(newValue).canonicalPath();
    std::shared_ptr<LoadInfo> lInfo = loadInfo(p);
    if (lInfo) {
        if (loadCallback) {
            DomItem oldValueObj = self.copy(oldValue);
            DomItem newValueObj = self.copy(newValue);
            loadCallback(p, oldValueObj, newValueObj);
        }
        if (directDepsCallback) {
            DomItem lInfoObj = self.copy(lInfo);
            lInfo->addEndCallback(lInfoObj, directDepsCallback);
        }
    } else {
        self.addError(myErrors().error(tr("missing load info in ")));
        if (loadCallback)
            loadCallback(self.canonicalPath(), DomItem::empty, DomItem::empty);
        if (directDepsCallback)
            directDepsCallback(self.canonicalPath(), DomItem::empty, DomItem::empty);
    }
    if (endCallback)
        addAllLoadedCallback(self, [p, endCallback](Path, const DomItem &, const DomItem &env) {
            DomItem el = env.path(p);
            endCallback(p, el, el);
        });
}

void DomEnvironment::loadModuleDependency(
        const QString &uri, Version version,
        const std::function<void(const Path &, const DomItem &, const DomItem &)> &callback,
        const ErrorHandler &errorHandler)
{
    DomItem envItem(shared_from_this());
    if (options() & DomEnvironment::Option::NoDependencies)
        loadModuleDependency(envItem, uri, version, callback, nullptr, errorHandler);
    else
        loadModuleDependency(envItem, uri, version, nullptr, callback, errorHandler);
}

void DomEnvironment::loadModuleDependency(const DomItem &self, const QString &uri, Version v,
                                          Callback loadCallback, Callback endCallback,
                                          const ErrorHandler &errorHandler)
{
    Q_ASSERT(!uri.contains(u'/'));
    Path p = Paths::moduleIndexPath(uri, v.majorVersion);
    if (v.majorVersion == Version::Latest) {
        // load both the latest .<version> directory, and the common one
        QStringList subPathComponents = uri.split(QLatin1Char('.'));
        int maxV = -1;
        bool commonV = false;
        QString lastComponent = subPathComponents.last();
        subPathComponents.removeLast();
        QString subPathV = subPathComponents.join(u'/');
        QRegularExpression vRe(QRegularExpression::anchoredPattern(
                QRegularExpression::escape(lastComponent) + QStringLiteral(u"\\.([0-9]*)")));
        const auto lPaths = loadPaths();
        qCDebug(QQmlJSDomImporting) << "DomEnvironment::loadModuleDependency: Searching module with"
                                       " uri"
                                    << uri;
        for (const QString &path : lPaths) {
            QDir dir(path + (subPathV.isEmpty() ? QStringLiteral(u"") : QStringLiteral(u"/"))
                     + subPathV);
            const auto eList = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &dirNow : eList) {
                auto m = vRe.match(dirNow);
                if (m.hasMatch()) {
                    int majorV = m.captured(1).toInt();
                    if (majorV > maxV) {
                        QFileInfo fInfo(dir.canonicalPath() + QChar(u'/') + dirNow
                                        + QStringLiteral(u"/qmldir"));
                        if (fInfo.isFile()) {
                            qCDebug(QQmlJSDomImporting)
                                    << "Found qmldir in " << fInfo.canonicalFilePath();
                            maxV = majorV;
                        }
                    }
                }
                if (!commonV && dirNow == lastComponent) {
                    QFileInfo fInfo(dir.canonicalPath() + QChar(u'/') + dirNow
                                    + QStringLiteral(u"/qmldir"));
                    if (fInfo.isFile()) {
                        qCDebug(QQmlJSDomImporting)
                                << "Found qmldir in " << fInfo.canonicalFilePath();
                        commonV = true;
                    }
                }
            }
        }

        // This decrements _separately_ for each copy of the lambda. So, what we get here is not a
        // limit on the total number of calls but a limit on the number of calls per caller
        // location. It gets even funnier if the callback is first called and then copied further.
        // TODO: Is this the intended behavior?
        int toLoad = (commonV ? 1 : 0) + ((maxV >= 0) ? 1 : 0);
        const auto loadCallback2 = loadCallback
                ? [p, loadCallback, toLoad](Path, const DomItem &, const DomItem &elV) mutable {
                      if (--toLoad == 0) {
                          DomItem el = elV.path(p);
                          loadCallback(p, el, el);
                      }
                  }
                : Callback();

        if (maxV >= 0)
            loadModuleDependency(self, uri, Version(maxV, v.minorVersion), loadCallback2, nullptr);
        if (commonV)
            loadModuleDependency(self, uri, Version(Version::Undefined, v.minorVersion),
                                 loadCallback2, nullptr);
        else if (maxV < 0) {
            if (uri != u"QML") {
                const QString loadPaths = lPaths.join(u", "_s);
                qCDebug(QQmlJSDomImporting)
                        << "DomEnvironment::loadModuleDependency: qmldir at" << (uri + u"/qmldir"_s)
                        << "was not found in " << loadPaths;
                addErrorLocal(
                        myErrors()
                                .warning(tr("Failed to find main qmldir file for %1 %2 in %3.")
                                                 .arg(uri, v.stringValue(), loadPaths))
                                .handle());
            }
            if (loadCallback)
                loadCallback(p, DomItem::empty, DomItem::empty);
        }
    } else {
        std::shared_ptr<ModuleIndex> mIndex = moduleIndexWithUri(
                self, uri, v.majorVersion, EnvLookup::Normal, Changeable::Writable, errorHandler);
        std::shared_ptr<LoadInfo> lInfo = loadInfo(p);
        if (lInfo) {
            DomItem lInfoObj = self.copy(lInfo);
            lInfo->addEndCallback(lInfoObj, loadCallback);
        } else {
            addErrorLocal(
                    myErrors().warning(tr("Missing loadInfo for %1").arg(p.toString())).handle());
            if (loadCallback)
                loadCallback(p, DomItem::empty, DomItem::empty);
        }
    }
    if (endCallback) {
        addAllLoadedCallback(self, [p = std::move(p), endCallback = std::move(endCallback)](
                                           Path, const DomItem &, const DomItem &env) {
            DomItem el = env.path(p);
            endCallback(p, el, el);
        });
    }
}

void DomEnvironment::loadBuiltins(const Callback &callback, const ErrorHandler &h)
{
    QString builtinsName = QLatin1String("builtins.qmltypes");
    const auto lPaths = loadPaths();
    for (const QString &path : lPaths) {
        QDir dir(path);
        QFileInfo fInfo(dir.filePath(builtinsName));
        if (fInfo.isFile()) {
            loadFile(FileToLoad::fromFileSystem(shared_from_this(), fInfo.canonicalFilePath()),
                     callback, LoadOption::DefaultLoad);
            return;
        }
    }
    myErrors().error(tr("Could not find builtins.qmltypes file")).handle(h);
}

void DomEnvironment::removePath(const QString &path)
{
    QMutexLocker l(mutex());
    auto toDelete = [path](auto it) {
        QString p = it.key();
        return p.startsWith(path) && (p.size() == path.size() || p.at(path.size()) == u'/');
    };
    m_qmlDirectoryWithPath.removeIf(toDelete);
    m_qmldirFileWithPath.removeIf(toDelete);
    m_qmlFileWithPath.removeIf(toDelete);
    m_jsFileWithPath.removeIf(toDelete);
    m_qmltypesFileWithPath.removeIf(toDelete);
}

shared_ptr<DomUniverse> DomEnvironment::universe() const {
    if (m_universe)
        return m_universe;
    else if (m_base)
        return m_base->universe();
    else
        return {};
}

template<typename T>
QSet<QString> DomEnvironment::getStrings(function_ref<QSet<QString>()> getBase,
                                         const QMap<QString, T> &selfMap, EnvLookup options) const
{
    QSet<QString> res;
    if (options != EnvLookup::NoBase && m_base) {
        if (m_base)
            res = getBase();
    }
    if (options != EnvLookup::BaseOnly) {
        QMap<QString, T> map;
        {
            QMutexLocker l(mutex());
            map = selfMap;
        }
        auto it = map.keyBegin();
        auto end = map.keyEnd();
        while (it != end) {
            res += *it;
            ++it;
        }
    }
    return res;
}

QSet<QString> DomEnvironment::moduleIndexUris(const DomItem &, EnvLookup lookup) const
{
    DomItem baseObj = DomItem(m_base);
    return this->getStrings<QMap<int, std::shared_ptr<ModuleIndex>>>(
            [this, &baseObj] { return m_base->moduleIndexUris(baseObj, EnvLookup::Normal); },
            m_moduleIndexWithUri, lookup);
}

QSet<int> DomEnvironment::moduleIndexMajorVersions(const DomItem &, const QString &uri, EnvLookup lookup) const
{
    QSet<int> res;
    if (lookup != EnvLookup::NoBase && m_base) {
        DomItem baseObj(m_base);
        res = m_base->moduleIndexMajorVersions(baseObj, uri, EnvLookup::Normal);
    }
    if (lookup != EnvLookup::BaseOnly) {
        QMap<int, std::shared_ptr<ModuleIndex>> map;
        {
            QMutexLocker l(mutex());
            map = m_moduleIndexWithUri.value(uri);
        }
        auto it = map.keyBegin();
        auto end = map.keyEnd();
        while (it != end) {
            res += *it;
            ++it;
        }
    }
    return res;
}

std::shared_ptr<ModuleIndex> DomEnvironment::lookupModuleInEnv(const QString &uri, int majorVersion) const
{
    QMutexLocker l(mutex());
    auto it = m_moduleIndexWithUri.find(uri);
    if (it == m_moduleIndexWithUri.end())
        return {}; // we haven't seen the module yet
    if (it->empty())
        return {}; // module contains nothing
    if (majorVersion == Version::Latest)
        return it->last(); // map is ordered by version, so last == Latest
    else
        return it->value(majorVersion); // null shared_ptr is fine if no match
}

DomEnvironment::ModuleLookupResult DomEnvironment::moduleIndexWithUriHelper(const DomItem &self, const QString &uri, int majorVersion, EnvLookup options) const
{
    std::shared_ptr<ModuleIndex> res;
    if (options != EnvLookup::BaseOnly)
        res = lookupModuleInEnv(uri, majorVersion);
    // if there is no base, or if we should not consider it
    // then the only result we can end up with is the module we looked up above
    if (options == EnvLookup::NoBase || !m_base)
        return {std::move(res), ModuleLookupResult::FromGlobal };
    const std::shared_ptr existingMod =
            m_base->moduleIndexWithUri(self, uri, majorVersion, options, Changeable::ReadOnly);
    if (!res)  // the only module we can find at all is the one in base (might be null, too, though)
        return { std::move(existingMod), ModuleLookupResult::FromBase };
    if (!existingMod) // on the other hand, if there was nothing in base, we can only return what was in the larger env
        return {std::move(res), ModuleLookupResult::FromGlobal };

    // if we have  both res and existingMod, res and existingMod should be the same
    // _unless_ we looked for the latest version. Then one might have a higher version than the other
    // and we have to check it

    if (majorVersion == Version::Latest) {
        if (res->majorVersion() >= existingMod->majorVersion())
            return { std::move(res), ModuleLookupResult::FromGlobal };
        else
            return { std::move(existingMod), ModuleLookupResult::FromBase };
    } else {
        // doesn't really matter which we return, but the other overload benefits from using the
        // version from m_moduleIndexWithUri
        return { std::move(res), ModuleLookupResult::FromGlobal };
    }
}

std::shared_ptr<ModuleIndex> DomEnvironment::moduleIndexWithUri(
        const DomItem &self, const QString &uri, int majorVersion, EnvLookup options,
        Changeable changeable, const ErrorHandler &errorHandler)
{
    // sanity checks
    Q_ASSERT((changeable == Changeable::ReadOnly
              || (majorVersion >= 0 || majorVersion == Version::Undefined))
             && "A writeable moduleIndexWithUri call should have a version (not with "
                "Version::Latest)");
    if (changeable == Changeable::Writable && (m_options & Option::Exported))
        myErrors().error(tr("A mutable module was requested in a multithreaded environment")).handle(errorHandler);


    // use the overload which does not care about changing m_moduleIndexWithUri to find a candidate
    auto [candidate, origin] = moduleIndexWithUriHelper(self, uri, majorVersion, options);

    // A ModuleIndex from m_moduleIndexWithUri can always be returned
    if (candidate && origin == ModuleLookupResult::FromGlobal)
        return candidate;

    // If we don't want to modify anything, return the candidate that we have found (if any)
    if (changeable == Changeable::ReadOnly)
        return candidate;

    // Else we want to create a modifyable version
    std::shared_ptr<ModuleIndex> newModulePtr = [&, candidate = candidate](){
        // which is a completely new module in case we don't have candidate
        if (!candidate)
            return std::make_shared<ModuleIndex>(uri, majorVersion);
        // or a copy of the candidate otherwise
        DomItem existingModObj = self.copy(candidate);
        return candidate->makeCopy(existingModObj);
    }();

    DomItem newModule = self.copy(newModulePtr);
    Path p = newModule.canonicalPath();
    {
        QMutexLocker l(mutex());
        auto &modsNow = m_moduleIndexWithUri[uri];
        // As we do not hold the lock for the whole operation, some other thread
        // might have created the module already
        if (auto it = modsNow.find(majorVersion); it != modsNow.end())
            return *it;
        modsNow.insert(majorVersion, newModulePtr);
    }
    if (p) {
        auto lInfo = std::make_shared<LoadInfo>(p);
        addLoadInfo(self, lInfo);
    } else {
        myErrors()
                .error(tr("Could not get path for newly created ModuleIndex %1 %2")
                               .arg(uri)
                               .arg(majorVersion))
                .handle(errorHandler);
    }

    return newModulePtr;
}

std::shared_ptr<ModuleIndex> DomEnvironment::moduleIndexWithUri(const DomItem &self, const QString &uri,
                                                                int majorVersion,
                                                                EnvLookup options) const
{
    return moduleIndexWithUriHelper(self, uri, majorVersion, options).module;
}



std::shared_ptr<ExternalItemInfo<QmlDirectory>>
DomEnvironment::qmlDirectoryWithPath(const DomItem &self, const QString &path, EnvLookup options) const
{
    if (options != EnvLookup::BaseOnly) {
        QMutexLocker l(mutex());
        if (m_qmlDirectoryWithPath.contains(path))
            return m_qmlDirectoryWithPath.value(path);
    }
    if (options != EnvLookup::NoBase && m_base) {
        return m_base->qmlDirectoryWithPath(self, path, options);
    }
    return {};
}

QSet<QString> DomEnvironment::qmlDirectoryPaths(const DomItem &, EnvLookup options) const
{
    return getStrings<std::shared_ptr<ExternalItemInfo<QmlDirectory>>>(
            [this] {
                DomItem baseObj(m_base);
                return m_base->qmlDirectoryPaths(baseObj, EnvLookup::Normal);
            },
            m_qmlDirectoryWithPath, options);
}

std::shared_ptr<ExternalItemInfo<QmldirFile>>
DomEnvironment::qmldirFileWithPath(const DomItem &self, const QString &path, EnvLookup options) const
{
    if (options != EnvLookup::BaseOnly) {
        QMutexLocker l(mutex());
        auto it = m_qmldirFileWithPath.find(path);
        if (it != m_qmldirFileWithPath.end())
            return *it;
    }
    if (options != EnvLookup::NoBase && m_base)
        return m_base->qmldirFileWithPath(self, path, options);
    return {};
}

QSet<QString> DomEnvironment::qmldirFilePaths(const DomItem &, EnvLookup lOptions) const
{
    return getStrings<std::shared_ptr<ExternalItemInfo<QmldirFile>>>(
            [this] {
                DomItem baseObj(m_base);
                return m_base->qmldirFilePaths(baseObj, EnvLookup::Normal);
            },
            m_qmldirFileWithPath, lOptions);
}

std::shared_ptr<ExternalItemInfoBase> DomEnvironment::qmlDirWithPath(const DomItem &self, const QString &path,
                                                                     EnvLookup options) const
{
    if (auto qmldirFile = qmldirFileWithPath(self, path + QLatin1String("/qmldir"), options))
        return qmldirFile;
    return qmlDirectoryWithPath(self, path, options);
}

QSet<QString> DomEnvironment::qmlDirPaths(const DomItem &self, EnvLookup options) const
{
    QSet<QString> res = qmlDirectoryPaths(self, options);
    const auto qmldirFiles = qmldirFilePaths(self, options);
    for (const QString &p : qmldirFiles) {
        if (p.endsWith(u"/qmldir")) {
            res.insert(p.left(p.size() - 7));
        } else {
            myErrors()
                    .warning(tr("Unexpected path not ending with qmldir in qmldirFilePaths: %1")
                                     .arg(p))
                    .handle();
        }
    }
    return res;
}

std::shared_ptr<ExternalItemInfo<QmlFile>>
DomEnvironment::qmlFileWithPath(const DomItem &self, const QString &path, EnvLookup options) const
{
    if (options != EnvLookup::BaseOnly) {
        QMutexLocker l(mutex());
        auto it = m_qmlFileWithPath.find(path);
        if (it != m_qmlFileWithPath.end())
            return *it;
    }
    if (options != EnvLookup::NoBase && m_base)
        return m_base->qmlFileWithPath(self, path, options);
    return {};
}

QSet<QString> DomEnvironment::qmlFilePaths(const DomItem &, EnvLookup lookup) const
{
    return getStrings<std::shared_ptr<ExternalItemInfo<QmlFile>>>(
            [this] {
                DomItem baseObj(m_base);
                return m_base->qmlFilePaths(baseObj, EnvLookup::Normal);
            },
            m_qmlFileWithPath, lookup);
}

std::shared_ptr<ExternalItemInfo<JsFile>>
DomEnvironment::jsFileWithPath(const DomItem &self, const QString &path, EnvLookup options) const
{
    if (options != EnvLookup::BaseOnly) {
        QMutexLocker l(mutex());
        if (m_jsFileWithPath.contains(path))
            return m_jsFileWithPath.value(path);
    }
    if (options != EnvLookup::NoBase && m_base)
        return m_base->jsFileWithPath(self, path, EnvLookup::Normal);
    return {};
}

QSet<QString> DomEnvironment::jsFilePaths(const DomItem &, EnvLookup lookup) const
{
    return getStrings<std::shared_ptr<ExternalItemInfo<JsFile>>>(
            [this] {
                DomItem baseObj(m_base);
                return m_base->jsFilePaths(baseObj, EnvLookup::Normal);
            },
            m_jsFileWithPath, lookup);
}

std::shared_ptr<ExternalItemInfo<QmltypesFile>>
DomEnvironment::qmltypesFileWithPath(const DomItem &self, const QString &path, EnvLookup options) const
{
    if (options != EnvLookup::BaseOnly) {
        QMutexLocker l(mutex());
        if (m_qmltypesFileWithPath.contains(path))
            return m_qmltypesFileWithPath.value(path);
    }
    if (options != EnvLookup::NoBase && m_base)
        return m_base->qmltypesFileWithPath(self, path, EnvLookup::Normal);
    return {};
}

QSet<QString> DomEnvironment::qmltypesFilePaths(const DomItem &, EnvLookup lookup) const
{
    return getStrings<std::shared_ptr<ExternalItemInfo<QmltypesFile>>>(
            [this] {
                DomItem baseObj(m_base);
                return m_base->qmltypesFilePaths(baseObj, EnvLookup::Normal);
            },
            m_qmltypesFileWithPath, lookup);
}

std::shared_ptr<ExternalItemInfo<GlobalScope>>
DomEnvironment::globalScopeWithName(const DomItem &self, const QString &name, EnvLookup lookupOptions) const
{
    if (lookupOptions != EnvLookup::BaseOnly) {
        QMutexLocker l(mutex());
        auto id = m_globalScopeWithName.find(name);
        if (id != m_globalScopeWithName.end())
            return *id;
    }
    if (lookupOptions != EnvLookup::NoBase && m_base)
        return m_base->globalScopeWithName(self, name, lookupOptions);
    return {};
}

std::shared_ptr<ExternalItemInfo<GlobalScope>>
DomEnvironment::ensureGlobalScopeWithName(const DomItem &self, const QString &name, EnvLookup lookupOptions)
{
    if (auto current = globalScopeWithName(self, name, lookupOptions))
        return current;
    if (auto u = universe()) {
        if (auto newVal = u->ensureGlobalScopeWithName(name)) {
            if (auto current = newVal->current) {
                DomItem currentObj = DomItem(u).copy(current);
                auto newScope = current->makeCopy(currentObj);
                auto newCopy = std::make_shared<ExternalItemInfo<GlobalScope>>(
                        newScope);
                QMutexLocker l(mutex());
                if (auto oldVal = m_globalScopeWithName.value(name))
                    return oldVal;
                m_globalScopeWithName.insert(name, newCopy);
                return newCopy;
            }
        }
    }
    Q_ASSERT_X(false, "DomEnvironment::ensureGlobalScopeWithName", "could not ensure globalScope");
    return {};
}

QSet<QString> DomEnvironment::globalScopeNames(const DomItem &, EnvLookup lookupOptions) const
{
    QSet<QString> res;
    if (lookupOptions != EnvLookup::NoBase && m_base) {
        if (m_base) {
            DomItem baseObj(m_base);
            res = m_base->globalScopeNames(baseObj, EnvLookup::Normal);
        }
    }
    if (lookupOptions != EnvLookup::BaseOnly) {
        QMap<QString, std::shared_ptr<ExternalItemInfo<GlobalScope>>> map;
        {
            QMutexLocker l(mutex());
            map = m_globalScopeWithName;
        }
        auto it = map.keyBegin();
        auto end = map.keyEnd();
        while (it != end) {
            res += *it;
            ++it;
        }
    }
    return res;
}

void DomEnvironment::addLoadInfo(const DomItem &self, const std::shared_ptr<LoadInfo> &loadInfo)
{
    if (!loadInfo)
        return;
    Path p = loadInfo->elementCanonicalPath();
    bool addWork = loadInfo->status() != LoadInfo::Status::Done;
    std::shared_ptr<LoadInfo> oldVal;
    {
        QMutexLocker l(mutex());
        oldVal = m_loadInfos.value(p);
        m_loadInfos.insert(p, loadInfo);
        if (addWork)
            m_loadsWithWork.enqueue(p);
    }
    if (oldVal && oldVal->status() != LoadInfo::Status::Done) {
        self.addError(myErrors()
                              .error(tr("addLoadinfo replaces unfinished load info for %1")
                                             .arg(p.toString()))
                              .handle());
    }
}

std::shared_ptr<LoadInfo> DomEnvironment::loadInfo(const Path &path) const
{
    QMutexLocker l(mutex());
    return m_loadInfos.value(path);
}

QHash<Path, std::shared_ptr<LoadInfo>> DomEnvironment::loadInfos() const
{
    QMutexLocker l(mutex());
    return m_loadInfos;
}

QList<Path> DomEnvironment::loadInfoPaths() const
{
    auto lInfos = loadInfos();
    return lInfos.keys();
}

DomItem::Callback DomEnvironment::callbackForQmlDirectory(const DomItem &self, Callback loadCallback,
                                                          Callback allDirectDepsCallback,
                                                          Callback endCallback)
{
    return envCallbackForFile<QmlDirectory>(self, &DomEnvironment::m_qmlDirectoryWithPath,
                                            &DomEnvironment::qmlDirectoryWithPath, loadCallback,
                                            allDirectDepsCallback, endCallback);
}

DomItem::Callback DomEnvironment::callbackForQmlFile(const DomItem &self, Callback loadCallback,
                                                     Callback allDirectDepsCallback,
                                                     Callback endCallback)
{
    return envCallbackForFile<QmlFile>(self, &DomEnvironment::m_qmlFileWithPath,
                                       &DomEnvironment::qmlFileWithPath, loadCallback,
                                       allDirectDepsCallback, endCallback);
}

DomTop::Callback DomEnvironment::callbackForQmltypesFile(const DomItem &self,
                                                         DomTop::Callback loadCallback,
                                                         Callback allDirectDepsCallback,
                                                         DomTop::Callback endCallback)
{
    return envCallbackForFile<QmltypesFile>(
            self, &DomEnvironment::m_qmltypesFileWithPath, &DomEnvironment::qmltypesFileWithPath,
            [loadCallback](const Path &p, const DomItem &oldV, const DomItem &newV) {
                DomItem newFile = newV.field(Fields::currentItem);
                if (std::shared_ptr<QmltypesFile> newFilePtr = newFile.ownerAs<QmltypesFile>())
                    newFilePtr->ensureInModuleIndex(newFile);
                if (loadCallback)
                    loadCallback(p, oldV, newV);
            },
            allDirectDepsCallback, endCallback);
}

DomTop::Callback DomEnvironment::callbackForQmldirFile(const DomItem &self, DomTop::Callback loadCallback,
                                                       Callback allDirectDepsCallback,
                                                       DomTop::Callback endCallback)
{
    return envCallbackForFile<QmldirFile>(self, &DomEnvironment::m_qmldirFileWithPath,
                                          &DomEnvironment::qmldirFileWithPath, loadCallback,
                                          allDirectDepsCallback, endCallback);
}

DomItem::Callback DomEnvironment::callbackForJSFile(const DomItem &self, Callback loadCallback,
                                                     Callback allDirectDepsCallback,
                                                     Callback endCallback)
{
    return envCallbackForFile<JsFile>(self, &DomEnvironment::m_jsFileWithPath,
                                       &DomEnvironment::jsFileWithPath, loadCallback,
                                       allDirectDepsCallback, endCallback);
}

DomEnvironment::DomEnvironment(
        const QStringList &loadPaths, Options options, const shared_ptr<DomUniverse> &universe)
    : m_options(options),
      m_universe(DomUniverse::guaranteeUniverse(universe)),
      m_loadPaths(loadPaths),
      m_implicitImports(defaultImplicitImports())
{}

std::shared_ptr<DomEnvironment> DomEnvironment::create(const QStringList &loadPaths,
                                                       Options options, const DomItem &universe)
{
    std::shared_ptr<DomUniverse> universePtr = universe.ownerAs<DomUniverse>();
    return std::make_shared<DomEnvironment>(loadPaths, options, universePtr);
}

DomEnvironment::DomEnvironment(
        const shared_ptr<DomEnvironment> &parent, const QStringList &loadPaths, Options options)
    : m_options(options),
      m_base(parent),
      m_loadPaths(loadPaths),
      m_implicitImports(defaultImplicitImports())
{}

template<typename T>
std::shared_ptr<ExternalItemInfo<T>>
addExternalItem(const std::shared_ptr<T> &file, const QString &key,
                QMap<QString, std::shared_ptr<ExternalItemInfo<T>>> &map, AddOption option,
                QBasicMutex *mutex)
{
    if (!file)
        return {};
    auto eInfo = std::make_shared<ExternalItemInfo<T>>(
            file, QDateTime::currentDateTimeUtc());
    {
        QMutexLocker l(mutex);
        auto it = map.find(key);
        if (it != map.end()) {
            switch (option) {
            case AddOption::KeepExisting:
                eInfo = *it;
                break;
            case AddOption::Overwrite:
                map.insert(key, eInfo);
                break;
            }
        } else {
            map.insert(key, eInfo);
        }
    }
    return eInfo;
}

std::shared_ptr<ExternalItemInfo<QmlFile>> DomEnvironment::addQmlFile(
        const std::shared_ptr<QmlFile> &file, AddOption options)
{
    return addExternalItem<QmlFile>(file, file->canonicalFilePath(), m_qmlFileWithPath, options,
                                    mutex());
}

std::shared_ptr<ExternalItemInfo<QmlDirectory>>
DomEnvironment::addQmlDirectory(const std::shared_ptr<QmlDirectory> &file, AddOption options)
{
    return addExternalItem<QmlDirectory>(file, file->canonicalFilePath(), m_qmlDirectoryWithPath,
                                         options, mutex());
}

std::shared_ptr<ExternalItemInfo<QmldirFile>>
DomEnvironment::addQmldirFile(const std::shared_ptr<QmldirFile> &file, AddOption options)
{
    return addExternalItem<QmldirFile>(file, file->canonicalFilePath(), m_qmldirFileWithPath,
                                       options, mutex());
}

std::shared_ptr<ExternalItemInfo<QmltypesFile>>
DomEnvironment::addQmltypesFile(const std::shared_ptr<QmltypesFile> &file, AddOption options)
{
    return addExternalItem<QmltypesFile>(file, file->canonicalFilePath(), m_qmltypesFileWithPath,
                                         options, mutex());
}

std::shared_ptr<ExternalItemInfo<JsFile>> DomEnvironment::addJsFile(
        const std::shared_ptr<JsFile> &file, AddOption options)
{
    return addExternalItem<JsFile>(file, file->canonicalFilePath(), m_jsFileWithPath, options,
                                   mutex());
}

std::shared_ptr<ExternalItemInfo<GlobalScope>>
DomEnvironment::addGlobalScope(const std::shared_ptr<GlobalScope> &scope, AddOption options)
{
    return addExternalItem<GlobalScope>(scope, scope->name(), m_globalScopeWithName, options,
                                        mutex());
}

bool DomEnvironment::commitToBase(
        const DomItem &self, const shared_ptr<DomEnvironment> &validEnvPtr)
{
    if (!base())
        return false;
    QMap<QString, QMap<int, std::shared_ptr<ModuleIndex>>> my_moduleIndexWithUri;
    QMap<QString, std::shared_ptr<ExternalItemInfo<GlobalScope>>> my_globalScopeWithName;
    QMap<QString, std::shared_ptr<ExternalItemInfo<QmlDirectory>>> my_qmlDirectoryWithPath;
    QMap<QString, std::shared_ptr<ExternalItemInfo<QmldirFile>>> my_qmldirFileWithPath;
    QMap<QString, std::shared_ptr<ExternalItemInfo<QmlFile>>> my_qmlFileWithPath;
    QMap<QString, std::shared_ptr<ExternalItemInfo<JsFile>>> my_jsFileWithPath;
    QMap<QString, std::shared_ptr<ExternalItemInfo<QmltypesFile>>> my_qmltypesFileWithPath;
    QHash<Path, std::shared_ptr<LoadInfo>> my_loadInfos;
    {
        QMutexLocker l(mutex());
        my_moduleIndexWithUri = m_moduleIndexWithUri;
        my_globalScopeWithName = m_globalScopeWithName;
        my_qmlDirectoryWithPath = m_qmlDirectoryWithPath;
        my_qmldirFileWithPath = m_qmldirFileWithPath;
        my_qmlFileWithPath = m_qmlFileWithPath;
        my_jsFileWithPath = m_jsFileWithPath;
        my_qmltypesFileWithPath = m_qmltypesFileWithPath;
        my_loadInfos = m_loadInfos;
    }
    {
        QMutexLocker lBase(base()->mutex()); // be more careful about makeCopy calls with lock?
        m_base->m_globalScopeWithName.insert(my_globalScopeWithName);
        m_base->m_qmlDirectoryWithPath.insert(my_qmlDirectoryWithPath);
        m_base->m_qmldirFileWithPath.insert(my_qmldirFileWithPath);
        m_base->m_qmlFileWithPath.insert(my_qmlFileWithPath);
        m_base->m_jsFileWithPath.insert(my_jsFileWithPath);
        m_base->m_qmltypesFileWithPath.insert(my_qmltypesFileWithPath);
        m_base->m_loadInfos.insert(my_loadInfos);
        {
            auto it = my_moduleIndexWithUri.cbegin();
            auto end = my_moduleIndexWithUri.cend();
            while (it != end) {
                QMap<int, shared_ptr<ModuleIndex>> &myVersions =
                        m_base->m_moduleIndexWithUri[it.key()];
                auto it2 = it.value().cbegin();
                auto end2 = it.value().cend();
                while (it2 != end2) {
                    auto oldV = myVersions.value(it2.key());
                    DomItem it2Obj = self.copy(it2.value());
                    auto newV = it2.value()->makeCopy(it2Obj);
                    newV->mergeWith(oldV);
                    myVersions.insert(it2.key(), newV);
                    ++it2;
                }
                ++it;
            }
        }
    }
    if (validEnvPtr) {
        QMutexLocker lValid(
                validEnvPtr->mutex()); // be more careful about makeCopy calls with lock?
        validEnvPtr->m_globalScopeWithName.insert(my_globalScopeWithName);
        validEnvPtr->m_qmlDirectoryWithPath.insert(my_qmlDirectoryWithPath);
        validEnvPtr->m_qmldirFileWithPath.insert(my_qmldirFileWithPath);
        for (auto it = my_qmlFileWithPath.cbegin(), end = my_qmlFileWithPath.cend(); it != end;
             ++it) {
            if (it.value() && it.value()->current && it.value()->current->isValid())
                validEnvPtr->m_qmlFileWithPath.insert(it.key(), it.value());
        }
        for (auto it = my_jsFileWithPath.cbegin(), end = my_jsFileWithPath.cend(); it != end;
             ++it) {
            if (it.value() && it.value()->current && it.value()->current->isValid())
                validEnvPtr->m_jsFileWithPath.insert(it.key(), it.value());
        }
        validEnvPtr->m_qmltypesFileWithPath.insert(my_qmltypesFileWithPath);
        validEnvPtr->m_loadInfos.insert(my_loadInfos);
        for (auto it = my_moduleIndexWithUri.cbegin(), end = my_moduleIndexWithUri.cend();
             it != end; ++it) {
            QMap<int, shared_ptr<ModuleIndex>> &myVersions =
                    validEnvPtr->m_moduleIndexWithUri[it.key()];
            for (auto it2 = it.value().cbegin(), end2 = it.value().cend(); it2 != end2; ++it2) {
                auto oldV = myVersions.value(it2.key());
                DomItem it2Obj = self.copy(it2.value());
                auto newV = it2.value()->makeCopy(it2Obj);
                newV->mergeWith(oldV);
                myVersions.insert(it2.key(), newV);
            }
        }
    }
    return true;
}

void DomEnvironment::loadPendingDependencies()
{
    DomItem self(shared_from_this());
    while (true) {
        Path elToDo;
        std::shared_ptr<LoadInfo> loadInfo;
        {
            QMutexLocker l(mutex());
            if (m_loadsWithWork.isEmpty())
                break;
            elToDo = m_loadsWithWork.dequeue();
            m_inProgress.append(elToDo);
            loadInfo = m_loadInfos.value(elToDo);
        }
        if (loadInfo) {
            auto cleanup = qScopeGuard([this, &elToDo, &self] {
                QList<Callback> endCallbacks;
                {
                    QMutexLocker l(mutex());
                    m_inProgress.removeOne(elToDo);
                    if (m_inProgress.isEmpty() && m_loadsWithWork.isEmpty()) {
                        endCallbacks = m_allLoadedCallback;
                        m_allLoadedCallback.clear();
                    }
                }
                for (const Callback &cb : std::as_const(endCallbacks))
                    cb(self.canonicalPath(), self, self);
            });
            DomItem loadInfoObj = self.copy(loadInfo);
            loadInfo->advanceLoad(loadInfoObj);
        } else {
            self.addError(myErrors().error(u"DomEnvironment::loadPendingDependencies could not "
                                           u"find loadInfo listed in m_loadsWithWork"));
            {
                QMutexLocker l(mutex());
                m_inProgress.removeOne(elToDo);
            }
            Q_ASSERT(false
                     && "DomEnvironment::loadPendingDependencies could not find loadInfo listed in "
                        "m_loadsWithWork");
        }
    }
}

bool DomEnvironment::finishLoadingDependencies(int waitMSec)
{
    bool hasPendingLoads = true;
    QDateTime endTime = QDateTime::currentDateTimeUtc().addMSecs(waitMSec);
    for (int i = 0; i < waitMSec / 10 + 2; ++i) {
        loadPendingDependencies();
        auto lInfos = loadInfos();
        auto it = lInfos.cbegin();
        auto end = lInfos.cend();
        hasPendingLoads = false;
        while (it != end) {
            if (*it && (*it)->status() != LoadInfo::Status::Done)
                hasPendingLoads = true;
        }
        if (!hasPendingLoads)
            break;
        auto missing = QDateTime::currentDateTimeUtc().msecsTo(endTime);
        if (missing < 0)
            break;
        if (missing > 100)
            missing = 100;
#if QT_FEATURE_thread
        QThread::msleep(missing);
#endif
    }
    return !hasPendingLoads;
}

void DomEnvironment::addWorkForLoadInfo(const Path &elementCanonicalPath)
{
    QMutexLocker l(mutex());
    m_loadsWithWork.enqueue(elementCanonicalPath);
}

DomEnvironment::Options DomEnvironment::options() const
{
    return m_options;
}

std::shared_ptr<DomEnvironment> DomEnvironment::base() const
{
    return m_base;
}

void DomEnvironment::setLoadPaths(const QStringList &v)
{
    QMutexLocker l(mutex());
    m_loadPaths = v;
}

QStringList DomEnvironment::loadPaths() const
{
    QMutexLocker l(mutex());
    return m_loadPaths;
}

QStringList DomEnvironment::qmldirFiles() const
{
    QMutexLocker l(mutex());
    return m_qmldirFileWithPath.keys();
}

QString DomEnvironment::globalScopeName() const
{
    return m_globalScopeName;
}

QList<Import> DomEnvironment::defaultImplicitImports()
{
    return QList<Import>({ Import::fromUriString(u"QML"_s, Version(1, 0)),
                           Import(QmlUri::fromUriString(u"QtQml"_s), Version(6, 0)) });
}

QList<Import> DomEnvironment::implicitImports() const
{
    return m_implicitImports;
}

void DomEnvironment::addAllLoadedCallback(const DomItem &self, DomTop::Callback c)
{
    if (c) {
        bool immediate = false;
        {
            QMutexLocker l(mutex());
            if (m_loadsWithWork.isEmpty() && m_inProgress.isEmpty())
                immediate = true;
            else
                m_allLoadedCallback.append(c);
        }
        if (immediate)
            c(Path(), self, self);
    }
}

void DomEnvironment::clearReferenceCache()
{
    m_referenceCache.clear();
}

QString ExternalItemInfoBase::canonicalFilePath(const DomItem &self) const
{
    shared_ptr<ExternalOwningItem> current = currentItem();
    DomItem currentObj = currentItem(self);
    return current->canonicalFilePath(currentObj);
}

bool ExternalItemInfoBase::iterateDirectSubpaths(const DomItem &self, DirectVisitor visitor) const
{
    if (!self.dvValueLazyField(visitor, Fields::currentRevision,
                               [this, &self]() { return currentRevision(self); }))
        return false;
    if (!self.dvValueLazyField(visitor, Fields::lastRevision,
                               [this, &self]() { return lastRevision(self); }))
        return false;
    if (!self.dvValueLazyField(visitor, Fields::lastValidRevision,
                               [this, &self]() { return lastValidRevision(self); }))
        return false;
    if (!visitor(PathEls::Field(Fields::currentItem),
                 [&self, this]() { return currentItem(self); }))
        return false;
    if (!self.dvValueLazyField(visitor, Fields::currentExposedAt,
                               [this]() { return currentExposedAt(); }))
        return false;
    return true;
}

int ExternalItemInfoBase::currentRevision(const DomItem &) const
{
    return currentItem()->revision();
}

int ExternalItemInfoBase::lastRevision(const DomItem &self) const
{
    Path p = currentItem()->canonicalPath();
    DomItem lastValue = self.universe()[p.mid(1, p.length() - 1)].field(u"revision");
    return static_cast<int>(lastValue.value().toInteger(0));
}

int ExternalItemInfoBase::lastValidRevision(const DomItem &self) const
{
    Path p = currentItem()->canonicalPath();
    DomItem lastValidValue = self.universe()[p.mid(1, p.length() - 2)].field(u"validItem").field(u"revision");
    return static_cast<int>(lastValidValue.value().toInteger(0));
}

QString ExternalItemPairBase::canonicalFilePath(const DomItem &) const
{
    shared_ptr<ExternalOwningItem> current = currentItem();
    return current->canonicalFilePath();
}

Path ExternalItemPairBase::canonicalPath(const DomItem &) const
{
    shared_ptr<ExternalOwningItem> current = currentItem();
    return current->canonicalPath().dropTail();
}

bool ExternalItemPairBase::iterateDirectSubpaths(const DomItem &self, DirectVisitor visitor) const
{
    if (!self.dvValueLazyField(visitor, Fields::currentIsValid,
                               [this]() { return currentIsValid(); }))
        return false;
    if (!visitor(PathEls::Field(Fields::validItem), [this, &self]() { return validItem(self); }))
        return false;
    if (!visitor(PathEls::Field(Fields::currentItem),
                 [this, &self]() { return currentItem(self); }))
        return false;
    if (!self.dvValueField(visitor, Fields::validExposedAt, validExposedAt))
        return false;
    if (!self.dvValueField(visitor, Fields::currentExposedAt, currentExposedAt))
        return false;
    return true;
}

bool ExternalItemPairBase::currentIsValid() const
{
    return currentItem() == validItem();
}

RefCacheEntry RefCacheEntry::forPath(const DomItem &el, const Path &canonicalPath)
{
    DomItem env = el.environment();
    std::shared_ptr<DomEnvironment> envPtr = env.ownerAs<DomEnvironment>();
    RefCacheEntry cached;
    if (envPtr) {
        QMutexLocker l(envPtr->mutex());
        cached = envPtr->m_referenceCache.value(canonicalPath, {});
    } else {
        qCWarning(domLog) << "No Env for reference" << canonicalPath << "from"
                          << el.internalKindStr() << el.canonicalPath();
        Q_ASSERT(false);
    }
    return cached;
}

bool RefCacheEntry::addForPath(const DomItem &el, const Path &canonicalPath, const RefCacheEntry &entry,
                               AddOption addOption)
{
    DomItem env = el.environment();
    std::shared_ptr<DomEnvironment> envPtr = env.ownerAs<DomEnvironment>();
    bool didSet = false;
    if (envPtr) {
        QMutexLocker l(envPtr->mutex());
        RefCacheEntry &cached = envPtr->m_referenceCache[canonicalPath];
        switch (cached.cached) {
        case RefCacheEntry::Cached::None:
            cached = entry;
            didSet = true;
            break;
        case RefCacheEntry::Cached::First:
            if (addOption == AddOption::Overwrite || entry.cached == RefCacheEntry::Cached::All) {
                cached = entry;
                didSet = true;
            }
            break;
        case RefCacheEntry::Cached::All:
            if (addOption == AddOption::Overwrite || entry.cached == RefCacheEntry::Cached::All) {
                cached = entry;
                didSet = true;
            }
        }
        if (cached.cached == RefCacheEntry::Cached::First && cached.canonicalPaths.isEmpty())
            cached.cached = RefCacheEntry::Cached::All;
    } else {
        Q_ASSERT(false);
    }
    return didSet;
}

} // end namespace Dom
} // end namespace QQmlJS

QT_END_NAMESPACE

#include "moc_qqmldomtop_p.cpp"
