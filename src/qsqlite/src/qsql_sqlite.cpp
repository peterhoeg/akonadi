/****************************************************************************
**
** Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the QtSql module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial Usage
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights.  These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qsql_sqlite.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QVariant>
#include <QSqlError>
#include <QSqlField>
#include <QSqlIndex>
#include <QSqlQuery>
#include <QStringList>
#include <QVector>

#include <QtSql/private/qsqldriver_p.h>
#include <QtSql/private/qsqlcachedresult_p.h>

#if defined Q_OS_WIN
# include <qt_windows.h>
#else
# include <unistd.h>
#endif

#include <sqlite3.h>

#include <QThread>
#include "sqlite_blocking.h"

Q_DECLARE_OPAQUE_POINTER(sqlite3 *)
Q_DECLARE_OPAQUE_POINTER(sqlite3_stmt *)

Q_DECLARE_METATYPE(sqlite3 *)
Q_DECLARE_METATYPE(sqlite3_stmt *)

QT_BEGIN_NAMESPACE

static QString _q_escapeIdentifier(const QString &identifier)
{
    QString res = identifier;
    if (!identifier.isEmpty() && identifier.at(0) != QString(QLatin1Char('"')) && identifier.right(1) != QString(QLatin1Char('"'))) {
        res.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        res.prepend(QLatin1Char('"')).append(QLatin1Char('"'));
        res.replace(QLatin1Char('.'), QStringLiteral("\".\""));
    }
    return res;
}

static QVariant::Type qGetColumnType(const QString &tpName)
{
    const QString typeName = tpName.toLower();

    if (typeName == QLatin1String("integer")
            || typeName == QLatin1String("int")) {
        return QVariant::Int;
    }
    if (typeName == QLatin1String("double")
            || typeName == QLatin1String("float")
            || typeName == QLatin1String("real")
            || typeName.startsWith(QLatin1String("numeric"))) {
        return QVariant::Double;
    }
    if (typeName == QLatin1String("blob")) {
        return QVariant::ByteArray;
    }
    if (typeName == QLatin1String("boolean")
            || typeName == QLatin1String("bool")) {
        return QVariant::Bool;
    }
    return QVariant::String;
}

static QSqlError qMakeError(sqlite3 *access, const QString &descr, QSqlError::ErrorType type,
                            int errorCode = -1)
{
    return QSqlError(descr,
                     QString(reinterpret_cast<const QChar *>(sqlite3_errmsg16(access))),
                     type, QString::number(errorCode));
}

class QSQLiteResultPrivate;

class QSQLiteResult : public QSqlCachedResult
{
    friend class QSQLiteDriver;
    friend class QSQLiteResultPrivate;
public:
    explicit QSQLiteResult(const QSQLiteDriver *db);
    ~QSQLiteResult();
    QVariant handle() const override;

protected:
    bool gotoNext(QSqlCachedResult::ValueCache &row, int idx) override;
    bool reset(const QString &query) override;
    bool prepare(const QString &query) override;
    bool exec() override;
    int size() override;
    int numRowsAffected() override;
    QVariant lastInsertId() const override;
    QSqlRecord record() const override;
    void detachFromResultSet() override;
    void virtual_hook(int id, void *data) override;

private:
    Q_DECLARE_PRIVATE(QSQLiteResult)
};

class QSQLiteDriverPrivate : public QSqlDriverPrivate
{
public:
    inline QSQLiteDriverPrivate() : access(nullptr)
    {
        dbmsType = QSqlDriver::SQLite;
    }
    sqlite3 *access;
    QList<QSQLiteResult *> results;
};

class QSQLiteResultPrivate : public QSqlCachedResultPrivate
{
public:
    QSQLiteResultPrivate(QSQLiteResult *res, const QSQLiteDriver *drv);

    void cleanup();
    bool fetchNext(QSqlCachedResult::ValueCache &values, int idx, bool initialFetch);
    // initializes the recordInfo and the cache
    void initColumns(bool emptyResultset);
    void finalize();

    sqlite3 *access;

    sqlite3_stmt *stmt;

    bool skippedStatus; // the status of the fetchNext() that's skipped
    bool skipRow; // skip the next fetchNext()?
    QSqlRecord rInf;
    QVector<QVariant> firstRow;

    Q_DECLARE_PUBLIC(QSQLiteResult)
};

QSQLiteResultPrivate::QSQLiteResultPrivate(QSQLiteResult *res, const QSQLiteDriver *drv)
    : QSqlCachedResultPrivate(res, drv)
    , access(nullptr)
    , stmt(nullptr)
    , skippedStatus(false)
    , skipRow(false)
{
}

void QSQLiteResultPrivate::cleanup()
{
    Q_Q(QSQLiteResult);
    finalize();
    rInf.clear();
    skippedStatus = false;
    skipRow = false;
    q->setAt(QSql::BeforeFirstRow);
    q->setActive(false);
    q->cleanup();
}

void QSQLiteResultPrivate::finalize()
{
    if (!stmt) {
        return;
    }

    sqlite3_finalize(stmt);
    stmt = nullptr;
}

void QSQLiteResultPrivate::initColumns(bool emptyResultset)
{
    Q_Q(QSQLiteResult);
    int nCols = sqlite3_column_count(stmt);
    if (nCols <= 0) {
        return;
    }

    q->init(nCols);

    for (int i = 0; i < nCols; ++i) {
        QString colName = QString::fromUtf16(
                              static_cast<const ushort *>(sqlite3_column_name16(stmt, i))
                          ).remove(QLatin1Char('"'));

        // must use typeName for resolving the type to match QSqliteDriver::record
        QString typeName = QString::fromUtf16(
                               static_cast<const ushort *>(sqlite3_column_decltype16(stmt, i)));

        // sqlite3_column_type is documented to have undefined behavior if the result set is empty
        int stp = emptyResultset ? -1 : sqlite3_column_type(stmt, i);

        QVariant::Type fieldType;

        if (typeName.isEmpty()) {
            fieldType = qGetColumnType(typeName);
        } else {
            // Get the proper type for the field based on stp value
            switch (stp) {
            case SQLITE_INTEGER:
                fieldType = QVariant::Int;
                break;
            case SQLITE_FLOAT:
                fieldType = QVariant::Double;
                break;
            case SQLITE_BLOB:
                fieldType = QVariant::ByteArray;
                break;
            case SQLITE_TEXT:
                fieldType = QVariant::String;
                break;
            case SQLITE_NULL:
            default:
                fieldType = QVariant::Invalid;
                break;
            }
        }

        QSqlField fld(colName, fieldType);
        fld.setSqlType(stp);
        rInf.append(fld);
    }
}

bool QSQLiteResultPrivate::fetchNext(QSqlCachedResult::ValueCache &values, int idx, bool initialFetch)
{
    Q_Q(QSQLiteResult);

    int res;
    int i;

    if (skipRow) {
        // already fetched
        Q_ASSERT(!initialFetch);
        skipRow = false;
        for (int i = 0; i < firstRow.count(); i++) {
            values[i] = firstRow[i];
        }
        return skippedStatus;
    }
    skipRow = initialFetch;

    if (initialFetch) {
        firstRow.clear();
        firstRow.resize(sqlite3_column_count(stmt));
    }

    if (!stmt) {
        q->setLastError(QSqlError(QCoreApplication::translate("QSQLiteResult", "Unable to fetch row"),
                                  QCoreApplication::translate("QSQLiteResult", "No query"), QSqlError::ConnectionError));
        q->setAt(QSql::AfterLastRow);
        return false;
    }
    res = sqlite3_blocking_step(stmt);

    switch (res) {
    case SQLITE_ROW:
        // check to see if should fill out columns
        if (rInf.isEmpty())
            // must be first call.
        {
            initColumns(false);
        }
        if (idx < 0 && !initialFetch) {
            return true;
        }
        for (i = 0; i < rInf.count(); ++i) {
            switch (sqlite3_column_type(stmt, i)) {
            case SQLITE_BLOB:
                values[i + idx] = QByteArray(static_cast<const char *>(
                                                 sqlite3_column_blob(stmt, i)),
                                             sqlite3_column_bytes(stmt, i));
                break;
            case SQLITE_INTEGER:
                values[i + idx] = sqlite3_column_int64(stmt, i);
                break;
            case SQLITE_FLOAT:
                switch (q->numericalPrecisionPolicy()) {
                case QSql::LowPrecisionInt32:
                    values[i + idx] = sqlite3_column_int(stmt, i);
                    break;
                case QSql::LowPrecisionInt64:
                    values[i + idx] = sqlite3_column_int64(stmt, i);
                    break;
                case QSql::LowPrecisionDouble:
                case QSql::HighPrecision:
                default:
                    values[i + idx] = sqlite3_column_double(stmt, i);
                    break;
                };
                break;
            case SQLITE_NULL:
                values[i + idx] = QVariant(QVariant::String);
                break;
            default:
                values[i + idx] = QString(reinterpret_cast<const QChar *>(
                                              sqlite3_column_text16(stmt, i)),
                                          sqlite3_column_bytes16(stmt, i) / sizeof(QChar));
                break;
            }
        }
        return true;
    case SQLITE_DONE:
        if (rInf.isEmpty())
            // must be first call.
        {
            initColumns(true);
        }
        q->setAt(QSql::AfterLastRow);
        sqlite3_reset(stmt);
        return false;
    case SQLITE_CONSTRAINT:
    case SQLITE_ERROR:
        // SQLITE_ERROR is a generic error code and we must call sqlite3_reset()
        // to get the specific error message.
        res = sqlite3_reset(stmt);
        q->setLastError(qMakeError(access, QCoreApplication::translate("QSQLiteResult",
                                   "Unable to fetch row"), QSqlError::ConnectionError, res));
        q->setAt(QSql::AfterLastRow);
        return false;
    case SQLITE_MISUSE:
    case SQLITE_BUSY:
    default:
        // something wrong, don't get col info, but still return false
        q->setLastError(qMakeError(access, QCoreApplication::translate("QSQLiteResult",
                                   "Unable to fetch row"), QSqlError::ConnectionError, res));
        sqlite3_reset(stmt);
        q->setAt(QSql::AfterLastRow);
        return false;
    }
    return false;
}

QSQLiteResult::QSQLiteResult(const QSQLiteDriver *db)
    : QSqlCachedResult(*new QSQLiteResultPrivate(this, db))
{
    Q_D(QSQLiteResult);
    d->access = db->d_func()->access;
    const_cast<QSQLiteDriverPrivate *>(db->d_func())->results.append(this);
}

QSQLiteResult::~QSQLiteResult()
{
    Q_D(QSQLiteResult);
    const QSqlDriver *sqlDriver = driver();
    if (sqlDriver) {
        const_cast<QSQLiteDriverPrivate *>(qobject_cast<const QSQLiteDriver *>(sqlDriver)->d_func())->results.removeOne(this);
    }
    d->cleanup();
}

void QSQLiteResult::virtual_hook(int id, void *data)
{
    QSqlCachedResult::virtual_hook(id, data);
}

bool QSQLiteResult::reset(const QString &query)
{
    if (!prepare(query)) {
        return false;
    }
    return exec();
}

bool QSQLiteResult::prepare(const QString &query)
{
    Q_D(QSQLiteResult);

    if (!driver() || !driver()->isOpen() || driver()->isOpenError()) {
        return false;
    }

    d->cleanup();

    setSelect(false);

    const void *pzTail = nullptr;

#if (SQLITE_VERSION_NUMBER >= 3003011)
//    int res = sqlite3_prepare16_v2(d->access, query.constData(), (query.size() + 1) * sizeof(QChar),
//                                   &d->stmt, 0);
    int res = sqlite3_blocking_prepare16_v2(d->access, query.constData(), (query.size() + 1) * sizeof(QChar),
                                            &d->stmt, &pzTail);
#else
    int res = sqlite3_prepare16(d->access, query.constData(), (query.size() + 1) * sizeof(QChar),
                                &d->stmt, &pzTail);
#endif

    if (res != SQLITE_OK) {
        setLastError(qMakeError(d->access, QCoreApplication::translate("QSQLiteResult",
                                "Unable to execute statement"), QSqlError::StatementError, res));
        d->finalize();
        return false;
    } else if (pzTail && !QString(reinterpret_cast<const QChar *>(pzTail)).trimmed().isEmpty()) {
        setLastError(qMakeError(d->access, QCoreApplication::translate("QSQLiteResult",
                                "Unable to execute multiple statements at a time"), QSqlError::StatementError, SQLITE_MISUSE));
        d->finalize();
        return false;
    }
    return true;
}

bool QSQLiteResult::exec()
{
    Q_D(QSQLiteResult);
    const QVector<QVariant> values = boundValues();

    d->skippedStatus = false;
    d->skipRow = false;
    d->rInf.clear();
    clearValues();
    setLastError(QSqlError());

    int res = sqlite3_reset(d->stmt);
    if (res != SQLITE_OK) {
        setLastError(qMakeError(d->access, QCoreApplication::translate("QSQLiteResult",
                                "Unable to reset statement"), QSqlError::StatementError, res));
        d->finalize();
        return false;
    }
    int paramCount = sqlite3_bind_parameter_count(d->stmt);
    if (paramCount == values.count()) {
        for (int i = 0; i < paramCount; ++i) {
            res = SQLITE_OK;
            const QVariant value = values.at(i);

            if (value.isNull()) {
                res = sqlite3_bind_null(d->stmt, i + 1);
            } else {
                switch (value.type()) {
                case QVariant::ByteArray: {
                    const QByteArray *ba = static_cast<const QByteArray *>(value.constData());
                    res = sqlite3_bind_blob(d->stmt, i + 1, ba->constData(),
                                            ba->size(), SQLITE_STATIC);
                    break;
                }
                case QVariant::Int:
                case QVariant::Bool:
                    res = sqlite3_bind_int(d->stmt, i + 1, value.toInt());
                    break;
                case QVariant::Double:
                    res = sqlite3_bind_double(d->stmt, i + 1, value.toDouble());
                    break;
                case QVariant::UInt:
                case QVariant::LongLong:
                    res = sqlite3_bind_int64(d->stmt, i + 1, value.toLongLong());
                    break;
                case QVariant::DateTime: {
                    const QDateTime dateTime = value.toDateTime();
                    const QString str = dateTime.toString(QStringLiteral("yyyy-MM-ddThh:mm:ss.zzz"));
                    res = sqlite3_bind_text16(d->stmt, i + 1, str.utf16(),
                                              str.size() * sizeof(ushort), SQLITE_TRANSIENT);
                    break;
                }
                case QVariant::Time: {
                    const QTime time = value.toTime();
                    const QString str = time.toString(QStringLiteral("hh:mm:ss.zzz"));
                    res = sqlite3_bind_text16(d->stmt, i + 1, str.utf16(),
                                              str.size() * sizeof(ushort), SQLITE_TRANSIENT);
                    break;
                }
                case QVariant::String: {
                    // lifetime of string == lifetime of its qvariant
                    const QString *str = static_cast<const QString *>(value.constData());
                    res = sqlite3_bind_text16(d->stmt, i + 1, str->utf16(),
                                              (str->size()) * sizeof(QChar), SQLITE_STATIC);
                    break;
                }
                default: {
                    QString str = value.toString();
                    // SQLITE_TRANSIENT makes sure that sqlite buffers the data
                    res = sqlite3_bind_text16(d->stmt, i + 1, str.utf16(),
                                              (str.size()) * sizeof(QChar), SQLITE_TRANSIENT);
                    break;
                }
                }
            }
            if (res != SQLITE_OK) {
                setLastError(qMakeError(d->access, QCoreApplication::translate("QSQLiteResult",
                                        "Unable to bind parameters"), QSqlError::StatementError, res));
                d->finalize();
                return false;
            }
        }
    } else {
        setLastError(QSqlError(QCoreApplication::translate("QSQLiteResult",
                               "Parameter count mismatch"), QString(), QSqlError::StatementError));
        return false;
    }
    d->skippedStatus = d->fetchNext(d->firstRow, 0, true);
    if (lastError().isValid()) {
        setSelect(false);
        setActive(false);
        return false;
    }
    setSelect(!d->rInf.isEmpty());
    setActive(true);
    return true;
}

bool QSQLiteResult::gotoNext(QSqlCachedResult::ValueCache &row, int idx)
{
    return d_func()->fetchNext(row, idx, false);
}

int QSQLiteResult::size()
{
    return -1;
}

int QSQLiteResult::numRowsAffected()
{
    return sqlite3_changes(d_func()->access);
}

QVariant QSQLiteResult::lastInsertId() const
{
    if (isActive()) {
        qint64 id = sqlite3_last_insert_rowid(d_func()->access);
        if (id) {
            return id;
        }
    }
    return QVariant();
}

QSqlRecord QSQLiteResult::record() const
{
    if (!isActive() || !isSelect()) {
        return QSqlRecord();
    }
    return d_func()->rInf;
}

void QSQLiteResult::detachFromResultSet()
{
    if (d_func()->stmt) {
        sqlite3_reset(d_func()->stmt);
    }
}

QVariant QSQLiteResult::handle() const
{
    return QVariant::fromValue(d_func()->stmt);
}

/////////////////////////////////////////////////////////

QSQLiteDriver::QSQLiteDriver(QObject *parent)
    : QSqlDriver(*new QSQLiteDriverPrivate, parent)
{
}

QSQLiteDriver::QSQLiteDriver(sqlite3 *connection, QObject *parent)
    : QSqlDriver(*new QSQLiteDriverPrivate, parent)
{
    Q_D(QSQLiteDriver);

    d->access = connection;
    setOpen(true);
    setOpenError(false);
}

QSQLiteDriver::~QSQLiteDriver()
{
}

bool QSQLiteDriver::hasFeature(DriverFeature f) const
{
    switch (f) {
    case BLOB:
    case Transactions:
    case Unicode:
    case LastInsertId:
    case PreparedQueries:
    case PositionalPlaceholders:
    case SimpleLocking:
    case FinishQuery:
    case LowPrecisionNumbers:
        return true;
    case QuerySize:
    case NamedPlaceholders:
    case BatchOperations:
    case EventNotifications:
    case MultipleResultSets:
    case CancelQuery:
        return false;
    }
    return false;
}

/*
   SQLite dbs have no user name, passwords, hosts or ports.
   just file names.
*/
bool QSQLiteDriver::open(const QString &db, const QString &, const QString &, const QString &, int, const QString &conOpts)
{
    Q_D(QSQLiteDriver);

    if (isOpen()) {
        close();
    }

    int timeout = 5000;
    bool sharedCache = false;
    bool openReadOnlyOption = false;
    bool openUriOption = false;

    const QStringList opts = QString(conOpts).remove(QLatin1Char(' ')).split(QLatin1Char(';'));
    for (const QString &option : opts) {
        if (option.startsWith(QLatin1String("QSQLITE_BUSY_TIMEOUT="))) {
            bool ok;
            const int nt = option.midRef(21).toInt(&ok);
            if (ok) {
                timeout = nt;
            }
        } else if (option == QLatin1String("QSQLITE_OPEN_READONLY")) {
            openReadOnlyOption = true;
        } else if (option == QLatin1String("QSQLITE_OPEN_URI")) {
            openUriOption = true;
        } else if (option == QLatin1String("QSQLITE_ENABLE_SHARED_CACHE")) {
            sharedCache = true;
        }
    }

    int openMode = (openReadOnlyOption ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE));
    if (openUriOption) {
        openMode |= SQLITE_OPEN_URI;
    }

    sqlite3_enable_shared_cache(sharedCache);

    if (sqlite3_open_v2(db.toUtf8().constData(), &d->access, openMode, nullptr) == SQLITE_OK) {
        sqlite3_busy_timeout(d->access, timeout);
        sqlite3_extended_result_codes(d->access, 1);
        setOpen(true);
        setOpenError(false);
        return true;
    } else {
        if (d->access) {
            sqlite3_close(d->access);
            d->access = nullptr;
        }

        setLastError(qMakeError(d->access, tr("Error opening database"),
                                QSqlError::ConnectionError));
        setOpenError(true);
        return false;
    }
}

void QSQLiteDriver::close()
{
    Q_D(QSQLiteDriver);

    if (isOpen()) {
        Q_FOREACH (QSQLiteResult *result, d->results) {
            result->d_func()->finalize();
        }

        if (sqlite3_close(d->access) != SQLITE_OK)
            setLastError(qMakeError(d->access, tr("Error closing database"),
                                    QSqlError::ConnectionError));
        d->access = nullptr;
        setOpen(false);
        setOpenError(false);
    }
}

QSqlResult *QSQLiteDriver::createResult() const
{
    return new QSQLiteResult(this);
}

bool QSQLiteDriver::beginTransaction()
{
    if (!isOpen() || isOpenError()) {
        return false;
    }

    QSqlQuery q(createResult());
    if (!q.exec(QStringLiteral("BEGIN"))) {
        setLastError(QSqlError(tr("Unable to begin transaction"),
                               q.lastError().databaseText(), QSqlError::TransactionError));
        return false;
    }

    return true;
}

bool QSQLiteDriver::commitTransaction()
{
    if (!isOpen() || isOpenError()) {
        return false;
    }

    QSqlQuery q(createResult());
    if (!q.exec(QStringLiteral("COMMIT"))) {
        setLastError(QSqlError(tr("Unable to commit transaction"),
                               q.lastError().databaseText(), QSqlError::TransactionError));
        return false;
    }

    return true;
}

bool QSQLiteDriver::rollbackTransaction()
{
    if (!isOpen() || isOpenError()) {
        return false;
    }

    QSqlQuery q(createResult());
    if (!q.exec(QStringLiteral("ROLLBACK"))) {
        setLastError(QSqlError(tr("Unable to rollback transaction"),
                               q.lastError().databaseText(), QSqlError::TransactionError));
        return false;
    }

    return true;
}

QStringList QSQLiteDriver::tables(QSql::TableType type) const
{
    QStringList res;
    if (!isOpen()) {
        return res;
    }

    QSqlQuery q(createResult());
    q.setForwardOnly(true);

    QString sql = QStringLiteral("SELECT name FROM sqlite_master WHERE %1 "
                                 "UNION ALL SELECT name FROM sqlite_temp_master WHERE %1");
    if ((type & QSql::Tables) && (type & QSql::Views)) {
        sql = sql.arg(QStringLiteral("type='table' OR type='view'"));
    } else if (type & QSql::Tables) {
        sql = sql.arg(QStringLiteral("type='table'"));
    } else if (type & QSql::Views) {
        sql = sql.arg(QStringLiteral("type='view'"));
    } else {
        sql.clear();
    }

    if (!sql.isEmpty() && q.exec(sql)) {
        while (q.next()) {
            res.append(q.value(0).toString());
        }
    }

    if (type & QSql::SystemTables) {
        // there are no internal tables beside this one:
        res.append(QStringLiteral("sqlite_master"));
    }

    return res;
}

static QSqlIndex qGetTableInfo(QSqlQuery &q, const QString &tableName, bool onlyPIndex = false)
{
    QString schema;
    QString table(tableName);
    int indexOfSeparator = tableName.indexOf(QLatin1Char('.'));
    if (indexOfSeparator > -1) {
        schema = tableName.left(indexOfSeparator).append(QLatin1Char('.'));
        table = tableName.mid(indexOfSeparator + 1);
    }
    q.exec(QStringLiteral("PRAGMA ") + schema + QStringLiteral("table_info (") + _q_escapeIdentifier(table) + QLatin1Char(')'));

    QSqlIndex ind;
    while (q.next()) {
        bool isPk = q.value(5).toInt();
        if (onlyPIndex && !isPk) {
            continue;
        }
        QString typeName = q.value(2).toString().toLower();
        QSqlField fld(q.value(1).toString(), qGetColumnType(typeName));
        if (isPk && (typeName == QLatin1String("integer")))
            // INTEGER PRIMARY KEY fields are auto-generated in sqlite
            // INT PRIMARY KEY is not the same as INTEGER PRIMARY KEY!
        {
            fld.setAutoValue(true);
        }
        fld.setRequired(q.value(3).toInt() != 0);
        fld.setDefaultValue(q.value(4));
        ind.append(fld);
    }
    return ind;
}

QSqlIndex QSQLiteDriver::primaryIndex(const QString &tblname) const
{
    if (!isOpen()) {
        return QSqlIndex();
    }

    QString table = tblname;
    if (isIdentifierEscaped(table, QSqlDriver::TableName)) {
        table = stripDelimiters(table, QSqlDriver::TableName);
    }

    QSqlQuery q(createResult());
    q.setForwardOnly(true);
    return qGetTableInfo(q, table, true);
}

QSqlRecord QSQLiteDriver::record(const QString &tbl) const
{
    if (!isOpen()) {
        return QSqlRecord();
    }

    QString table = tbl;
    if (isIdentifierEscaped(table, QSqlDriver::TableName)) {
        table = stripDelimiters(table, QSqlDriver::TableName);
    }

    QSqlQuery q(createResult());
    q.setForwardOnly(true);
    return qGetTableInfo(q, table);
}

QVariant QSQLiteDriver::handle() const
{
    Q_D(const QSQLiteDriver);
    return QVariant::fromValue(d->access);
}

QString QSQLiteDriver::escapeIdentifier(const QString &identifier, IdentifierType type) const
{
    Q_UNUSED(type);
    return _q_escapeIdentifier(identifier);
}

QT_END_NAMESPACE
