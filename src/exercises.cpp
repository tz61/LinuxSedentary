#include "exercises.h"

#include <QRandomGenerator>

namespace Exercises {

const QVector<Exercise> &all()
{
    static const QVector<Exercise> list = {
        {QStringLiteral("Walk around"),
         QStringLiteral("Get up and walk for a couple of minutes. Fetch a glass of water on the way.")},
        {QStringLiteral("Calf raises"),
         QStringLiteral("Stand tall, rise onto your toes, hold for two seconds and lower slowly. Repeat 15 times.")},
        {QStringLiteral("Shoulder rolls"),
         QStringLiteral("Roll your shoulders backwards ten times, then forwards ten times, nice and slow.")},
        {QStringLiteral("Neck stretch"),
         QStringLiteral("Tilt your ear towards your shoulder and hold for 15 seconds. Switch sides. Do not force it.")},
        {QStringLiteral("Chest opener"),
         QStringLiteral("Clasp your hands behind your back, straighten your arms and lift your chest. Hold for 20 seconds.")},
        {QStringLiteral("Hip flexor stretch"),
         QStringLiteral("Step one foot forward into a gentle lunge and push your hips forward. Hold 20 seconds per side.")},
        {QStringLiteral("Hamstring stretch"),
         QStringLiteral("Put a heel on a low step, keep the leg straight and lean forward from the hips. 20 seconds per leg.")},
        {QStringLiteral("Squats"),
         QStringLiteral("Feet shoulder-width apart, sit back as if onto a chair and stand up again. Ten slow reps.")},
        {QStringLiteral("Wall push-ups"),
         QStringLiteral("Hands on a wall at shoulder height, step back and do ten slow push-ups.")},
        {QStringLiteral("Back extension"),
         QStringLiteral("Stand with your hands on your lower back and gently lean backwards. Hold five seconds, repeat five times.")},
        {QStringLiteral("Wrist and finger stretch"),
         QStringLiteral("Stretch one arm out, palm up, and gently pull the fingers back with the other hand. 15 seconds each.")},
        {QStringLiteral("Rest your eyes"),
         QStringLiteral("Look at something at least six metres away for 20 seconds. Blink slowly a few times.")},
        {QStringLiteral("Side bends"),
         QStringLiteral("Raise one arm over your head and lean to the opposite side. Hold ten seconds, switch, repeat three times.")},
        {QStringLiteral("Take the stairs"),
         QStringLiteral("If there are stairs nearby, walk up and down one flight.")},
        {QStringLiteral("Deep breathing"),
         QStringLiteral("Stand by a window. Breathe in for four seconds, hold for four, out for six. Repeat five times.")},
    };
    return list;
}

int randomIndex(int avoid)
{
    const int n = all().size();
    if (n <= 1)
        return 0;
    int i = int(QRandomGenerator::global()->bounded(n));
    if (i == avoid)
        i = (i + 1) % n;
    return i;
}

} // namespace Exercises
