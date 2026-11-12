#include "input/MenuBuilder.h"

#include <QFile>
#include <QFileSystemWatcher>
#include <QHash>
#include <QTextStream>
#include <QTimer>

namespace {

constexpr QLatin1String kMenuMarker("#menu:");

struct Entry {
    QString key;        // joined path, identity for override lookup
    QStringList path;
    QString mpvKey;     // mpv key token of the binding line (A10)
};

struct Node {
    QString text;
    bool leaf = false;
    QString command;
    QString mpvKey;     // set for leaves only (A10)
    QList<Node> children;
};

struct Binding {
    QString command;
    QString mpvKey;
};

// One row of the flat binding table (A22).
struct Row {
    QString key;
    QString command;
    QString menuPath;
};

QString bindingKey(const QString &bindingPart)
{
    qsizetype i = 0;
    while (i < bindingPart.size() && bindingPart.at(i).isSpace())
        ++i;
    qsizetype j = i;
    while (j < bindingPart.size() && !bindingPart.at(j).isSpace())
        ++j;
    if (j == i)
        return {};
    return bindingPart.mid(i, j - i);
}

QString bindingCommand(const QString &bindingPart)
{
    qsizetype i = 0;
    while (i < bindingPart.size() && bindingPart.at(i).isSpace())
        ++i;
    qsizetype j = i;
    while (j < bindingPart.size() && !bindingPart.at(j).isSpace())
        ++j;
    if (j == i)
        return {};
    while (j < bindingPart.size() && bindingPart.at(j).isSpace())
        ++j;
    return bindingPart.mid(j).trimmed();
}

// Empty segments collapse, so a stray ">>" cannot create a nameless level.
QStringList menuSegments(const QString &pathPart)
{
    QStringList path;
    const QStringList segments = pathPart.split(u'>', Qt::SkipEmptyParts);
    for (const QString &segment : segments) {
        const QString trimmed = segment.trimmed();
        if (!trimmed.isEmpty())
            path.append(trimmed);
    }
    return path;
}

void parseFile(const QString &filePath, QList<Entry> &order, QHash<QString, Binding> &bindings)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();

        // mpv comment lines carry no binding, so they must not contribute
        // menu entries either (a commented-out binding leaves the menu).
        qsizetype first = 0;
        while (first < line.size() && line.at(first).isSpace())
            ++first;
        if (first < line.size() && line.at(first) == u'#')
            continue;

        const qsizetype marker = line.indexOf(kMenuMarker);
        if (marker < 0)
            continue; // plain mpv binding, not a menu entry

        const QString pathPart = line.mid(marker + kMenuMarker.size()).trimmed();
        if (pathPart.isEmpty())
            continue;

        const QStringList path = menuSegments(pathPart);
        if (path.isEmpty())
            continue;

        const QString command = bindingCommand(line.left(marker));
        if (command.isEmpty())
            continue;

        // A10: the key token of the same binding line ("<key> <command>").
        const QString mpvKey = bindingKey(line.left(marker));

        const QString joined = path.join(QChar(u'\x1F'));
        if (!bindings.contains(joined))
            order.append({joined, path, mpvKey});
        bindings.insert(joined, {command, mpvKey}); // later files override
    }
}

// A22. Deliberately its own pass rather than a parameterized parseFile:
// parseFile keys on the menu path and drops every line without a "#menu:"
// marker, and this one needs the opposite on both counts.
void parseBindings(const QString &filePath, QList<Row> &rows, QHash<QString, qsizetype> &index)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();

        qsizetype first = 0;
        while (first < line.size() && line.at(first).isSpace())
            ++first;
        if (first >= line.size() || line.at(first) == u'#')
            continue; // a commented-out binding is not a binding

        const qsizetype marker = line.indexOf(kMenuMarker);
        const QString bindingPart = marker < 0 ? line : line.left(marker);
        const QString key = bindingKey(bindingPart);
        const QString command = bindingCommand(bindingPart);
        if (key.isEmpty() || command.isEmpty())
            continue;

        const QString menuPath =
            marker < 0 ? QString()
                       : menuSegments(line.mid(marker + kMenuMarker.size())).join(QStringLiteral(" > "));

        // mpv resolves a duplicate binding by letting the last line win, so a
        // repeat of the same key replaces the row where it already sits
        // instead of appending a second one.
        const auto existing = index.constFind(key);
        if (existing != index.constEnd()) {
            rows[existing.value()] = Row{key, command, menuPath};
        } else {
            index.insert(key, rows.size());
            rows.append(Row{key, command, menuPath});
        }
    }
}

Node *findChild(QList<Node> &children, const QString &text)
{
    for (Node &node : children) {
        if (node.text == text)
            return &node;
    }
    return nullptr;
}

void insertPath(QList<Node> &root, const QStringList &path, const QString &command, const QString &mpvKey)
{
    QList<Node> *level = &root;
    for (qsizetype i = 0; i < path.size(); ++i) {
        const QString &segment = path.at(i);
        Node *child = findChild(*level, segment);
        if (!child) {
            Node node;
            node.text = segment;
            level->append(node);
            child = &level->last();
        }
        if (i == path.size() - 1) {
            child->leaf = true;
            child->command = command;
            child->mpvKey = mpvKey;
        } else {
            level = &child->children;
        }
    }
}

QVariantList toVariantList(const QList<Node> &nodes)
{
    QVariantList list;
    list.reserve(nodes.size());
    for (const Node &node : nodes) {
        QVariantMap item;
        item.insert(QStringLiteral("text"), node.text);
        if (node.leaf) {
            item.insert(QStringLiteral("command"), node.command);
            item.insert(QStringLiteral("key"), node.mpvKey);
        } else {
            item.insert(QStringLiteral("children"), toVariantList(node.children));
        }
        list.append(item);
    }
    return list;
}

} // namespace

MenuBuilder::MenuBuilder(QObject *parent)
    : QObject(parent)
{
}

QVariantList MenuBuilder::buildMenuFromConf(const QString &filePath)
{
    return buildMenu(QStringList{filePath});
}

QVariantList MenuBuilder::buildMenu(const QStringList &files)
{
    QList<Entry> order;
    QHash<QString, Binding> bindings;

    for (const QString &file : files)
        parseFile(file, order, bindings);

    QList<Node> root;
    for (const Entry &entry : order) {
        const Binding &binding = bindings.value(entry.key);
        insertPath(root, entry.path, binding.command, binding.mpvKey);
    }

    return toVariantList(root);
}

QVariantList MenuBuilder::bindingTable(const QStringList &files)
{
    QList<Row> rows;
    QHash<QString, qsizetype> index; // key -> position, so a duplicate stays put

    for (const QString &file : files)
        parseBindings(file, rows, index);

    QVariantList table;
    table.reserve(rows.size());
    for (const Row &row : rows) {
        QVariantMap item;
        item.insert(QStringLiteral("key"), row.key);
        item.insert(QStringLiteral("command"), row.command);
        item.insert(QStringLiteral("menuPath"), row.menuPath);
        table.append(item);
    }
    return table;
}

void MenuBuilder::watchFiles(const QStringList &paths)
{
    m_watched = paths;

    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        m_debounce = new QTimer(this);
        m_debounce->setSingleShot(true);
        m_debounce->setInterval(250);

        connect(m_watcher, &QFileSystemWatcher::fileChanged, this,
                [this](const QString &path) {
                    // Editors often delete-and-recreate on save, which drops
                    // the watch; re-add unconditionally when the file is back.
                    if (QFile::exists(path) && !m_watcher->files().contains(path))
                        m_watcher->addPath(path);
                    m_debounce->start();
                });
        connect(m_debounce, &QTimer::timeout, this, [this]() {
            // Staged parse: only notify when the re-parse still yields a
            // menu, so a half-written or emptied file never wipes it.
            const QVariantList staged = buildMenu(m_watched);
            if (!staged.isEmpty())
                emit menuChanged();
        });
    }

    for (const QString &path : m_watched) {
        if (QFile::exists(path) && !m_watcher->files().contains(path))
            m_watcher->addPath(path);
    }
}
