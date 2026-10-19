#pragma once

#include <QString>
#include <QVector>

struct Exercise {
    QString title;
    QString description;
};

namespace Exercises {

const QVector<Exercise> &all();
// A random exercise, different from the one at index `avoid` when possible.
int randomIndex(int avoid = -1);

} // namespace Exercises
