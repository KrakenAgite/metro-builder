#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>

class Metro;

// Succès à débloquer, conservés d'une partie à l'autre (QSettings)
class Achievements : public QObject
{
    Q_OBJECT
public:
    struct Def {
        QString id, title, description;
    };

    explicit Achievements(QObject *parent = nullptr);
    static const QVector<Def> &all();

    // succès mesurables sur la partie en cours (appelé régulièrement)
    void check(const Metro &metro);
    // succès liés à une action (export, itinéraire…)
    void unlock(const QString &id);

    bool has(const QString &id) const;
    QDateTime when(const QString &id) const;
    int count() const;

signals:
    void unlocked(const Achievements::Def &def);
};
