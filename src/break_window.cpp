#include "break_window.h"

#include "exercises.h"
#include "format.h"
#include "icons.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <QWindow>

namespace {

constexpr int kShadowMargin = 28;
// Buttons ignore input right after the card appears, so a keystroke or
// click meant for another window cannot dismiss it by accident.
constexpr int kArmDelayMs = 900;
constexpr int kDoneCloseMs = 20'000;
const QColor kDim(0, 0, 0, 150);

QScreen *activeScreen()
{
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    return screen ? screen : QGuiApplication::primaryScreen();
}

// Darkens a screen that does not show the card.
class Dimmer : public QWidget
{
public:
    explicit Dimmer(QScreen *screen)
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_DeleteOnClose);
        create();
        windowHandle()->setScreen(screen);
        setGeometry(screen->geometry());
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter(this).fillRect(rect(), kDim);
    }
};

QFont scaledFont(const QFont &base, qreal factor, bool bold)
{
    QFont f = base;
    if (f.pointSizeF() > 0)
        f.setPointSizeF(f.pointSizeF() * factor);
    else
        f.setPixelSize(int(f.pixelSize() * factor));
    f.setBold(bold);
    return f;
}

} // namespace

BreakWindow::BreakWindow(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(QStringLiteral("Time to stand up"));
    setWindowIcon(Icons::appIcon());

    m_card = new QFrame(this);
    m_card->setObjectName(QStringLiteral("card"));
    m_card->setFixedWidth(500);
    m_card->setStyleSheet(QStringLiteral(
        "QFrame#card { background: palette(window); border: 1px solid palette(mid); border-radius: 18px; }"
        "QFrame#tip { background: palette(base); border: none; border-radius: 12px; }"));
    auto *shadow = new QGraphicsDropShadowEffect(m_card);
    shadow->setBlurRadius(kShadowMargin);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 110));
    m_card->setGraphicsEffect(shadow);

    auto *outer = new QGridLayout(this);
    outer->setContentsMargins(kShadowMargin, kShadowMargin, kShadowMargin, kShadowMargin);
    outer->addWidget(m_card, 0, 0, Qt::AlignCenter);

    auto *layout = new QVBoxLayout(m_card);
    layout->setContentsMargins(28, 24, 28, 22);
    layout->setSpacing(14);

    // Header: icon, title, message.
    auto *header = new QHBoxLayout;
    header->setSpacing(16);
    m_icon = new QLabel(m_card);
    m_icon->setPixmap(Icons::appIcon().pixmap(56, 56));
    m_icon->setAlignment(Qt::AlignTop);
    header->addWidget(m_icon, 0, Qt::AlignTop);
    auto *headerText = new QVBoxLayout;
    headerText->setSpacing(4);
    m_title = new QLabel(m_card);
    m_title->setFont(scaledFont(font(), 1.7, true));
    m_message = new QLabel(m_card);
    m_message->setWordWrap(true);
    headerText->addWidget(m_title);
    headerText->addWidget(m_message);
    header->addLayout(headerText, 1);
    layout->addLayout(header);

    // Exercise suggestion.
    auto *tip = new QFrame(m_card);
    tip->setObjectName(QStringLiteral("tip"));
    auto *tipLayout = new QVBoxLayout(tip);
    tipLayout->setContentsMargins(16, 12, 12, 14);
    tipLayout->setSpacing(4);
    auto *tipHeader = new QHBoxLayout;
    auto *tryThis = new QLabel(QStringLiteral("TRY THIS"), tip);
    QFont small = scaledFont(font(), 0.8, true);
    small.setLetterSpacing(QFont::PercentageSpacing, 110);
    tryThis->setFont(small);
    Icons::setMuted(tryThis);
    m_nextTip = new QPushButton(QStringLiteral("Another one"), tip);
    m_nextTip->setFlat(true);
    m_nextTip->setFocusPolicy(Qt::NoFocus);
    m_nextTip->setCursor(Qt::PointingHandCursor);
    tipHeader->addWidget(tryThis);
    tipHeader->addStretch();
    tipHeader->addWidget(m_nextTip);
    m_tipTitle = new QLabel(tip);
    m_tipTitle->setFont(scaledFont(font(), 1.15, true));
    m_tipText = new QLabel(tip);
    m_tipText->setWordWrap(true);
    tipLayout->addLayout(tipHeader);
    tipLayout->addWidget(m_tipTitle);
    tipLayout->addWidget(m_tipText);
    layout->addWidget(tip);

    // Break countdown.
    m_countdown = new QLabel(m_card);
    QFont big = scaledFont(font(), 3.6, false);
    big.setWeight(QFont::Light);
    m_countdown->setFont(big);
    m_countdown->setAlignment(Qt::AlignCenter);
    m_progress = new QProgressBar(m_card);
    m_progress->setRange(0, 1000);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(8);
    m_hint = new QLabel(m_card);
    m_hint->setWordWrap(true);
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setText(QStringLiteral("Still at the keyboard? A break works best away from the screen."));
    QPalette hintPalette = m_hint->palette();
    hintPalette.setColor(QPalette::WindowText, Icons::warnColor());
    m_hint->setPalette(hintPalette);
    layout->addWidget(m_countdown);
    layout->addWidget(m_progress);
    layout->addWidget(m_hint);

    // Buttons.
    auto *buttons = new QHBoxLayout;
    m_skip = new QPushButton(QStringLiteral("Skip"), m_card);
    m_skip->setFlat(true);
    m_snooze = new QPushButton(m_card);
    m_primary = new QPushButton(m_card);
    m_primary->setDefault(true);
    QFont primaryFont = m_primary->font();
    primaryFont.setBold(true);
    m_primary->setFont(primaryFont);
    m_primary->setMinimumWidth(140);
    buttons->addWidget(m_skip);
    buttons->addStretch();
    buttons->addWidget(m_snooze);
    buttons->addWidget(m_primary);
    layout->addSpacing(4);
    layout->addLayout(buttons);

    connect(m_nextTip, &QPushButton::clicked, this, [this] { showTip(Exercises::randomIndex(m_tip)); });
    connect(m_skip, &QPushButton::clicked, this, &BreakWindow::skipClicked);
    connect(m_snooze, &QPushButton::clicked, this, &BreakWindow::snoozeClicked);
    connect(m_primary, &QPushButton::clicked, this, &BreakWindow::onPrimary);

    m_armTimer.setSingleShot(true);
    connect(&m_armTimer, &QTimer::timeout, this, [this] {
        for (QPushButton *b : {m_skip, m_snooze, m_primary})
            b->setEnabled(true);
    });
    m_doneTimer.setSingleShot(true);
    connect(&m_doneTimer, &QTimer::timeout, this, &BreakWindow::dismiss);
}

BreakWindow::~BreakWindow()
{
    clearDimmers();
}

void BreakWindow::showReminder(int sittingSeconds, bool fullscreen, bool allowSnooze,
                               const QString &snoozeLabel, bool allowSkip)
{
    const bool fresh = !isVisible() || m_mode != Mode::Reminder;
    m_mode = Mode::Reminder;
    m_doneTimer.stop();

    m_title->setText(QStringLiteral("Time to stand up"));
    m_message->setText(QStringLiteral("You have been sitting for %1. Get up, move around and rest your eyes.")
                           .arg(Format::duration(sittingSeconds)));
    if (fresh)
        showTip(Exercises::randomIndex(m_tip));
    m_tipTitle->parentWidget()->show();
    m_countdown->hide();
    m_progress->hide();
    m_hint->hide();
    m_skip->setVisible(allowSkip);
    m_snooze->setVisible(allowSnooze);
    m_snooze->setText(snoozeLabel);
    m_primary->setText(QStringLiteral("Start break"));

    if (fresh)
        armButtons();
    present(fullscreen);
}

void BreakWindow::showBreak(int breakSeconds, bool fullscreen)
{
    m_mode = Mode::Break;
    m_breakSeconds = std::max(1, breakSeconds);
    m_doneTimer.stop();

    m_title->setText(QStringLiteral("Break time"));
    m_message->setText(QStringLiteral("Stand up and step away from the screen. This closes by itself when the break is over."));
    if (m_tip < 0)
        showTip(Exercises::randomIndex());
    m_tipTitle->parentWidget()->show();
    m_countdown->show();
    m_progress->show();
    m_skip->hide();
    m_snooze->hide();
    m_primary->setText(QStringLiteral("End break early"));
    updateBreak(breakSeconds, false);

    if (!isVisible())
        armButtons();
    present(fullscreen);
}

void BreakWindow::updateBreak(int remainingSeconds, bool userActive)
{
    if (m_mode != Mode::Break)
        return;
    m_countdown->setText(Format::clock(remainingSeconds));
    m_progress->setValue(int(qint64(m_breakSeconds - remainingSeconds) * 1000 / m_breakSeconds));
    if (m_hint->isVisible() != userActive) {
        m_hint->setVisible(userActive);
        if (!m_fullscreen && isVisible())
            present(false);
    }
}

void BreakWindow::showDone(const QString &nextBreak)
{
    m_mode = Mode::Done;
    m_title->setText(QStringLiteral("Break complete"));
    m_message->setText(QStringLiteral("Nice work! The next break is in %1.").arg(nextBreak));
    m_tipTitle->parentWidget()->hide();
    m_countdown->hide();
    m_progress->hide();
    m_hint->hide();
    m_skip->hide();
    m_snooze->hide();
    m_primary->setText(QStringLiteral("Back to work"));
    armButtons();
    // No need to keep the screen dimmed now.
    present(false);
    m_doneTimer.start(kDoneCloseMs);
}

void BreakWindow::dismiss()
{
    m_doneTimer.stop();
    m_armTimer.stop();
    clearDimmers();
    hide();
    m_tip = -1;
}

void BreakWindow::present(bool fullscreen)
{
    QScreen *screen = isVisible() && windowHandle() ? windowHandle()->screen() : activeScreen();
    const bool modeChange = isVisible() && fullscreen != m_fullscreen;
    if (modeChange) {
        hide();
        clearDimmers();
    }
    m_fullscreen = fullscreen;
    setAttribute(Qt::WA_ShowWithoutActivating, !fullscreen);
    if (!windowHandle())
        create();
    if (!isVisible())
        windowHandle()->setScreen(screen);

    if (fullscreen) {
        if (!isVisible()) {
            setGeometry(screen->geometry());
            showFullScreen();
            const auto screens = QGuiApplication::screens();
            for (QScreen *other : screens) {
                if (other == screen)
                    continue;
                auto *dimmer = new Dimmer(other);
                dimmer->showFullScreen();
                m_dimmers.append(dimmer);
            }
        }
        raise();
        activateWindow();
    } else {
        if (windowState() & Qt::WindowFullScreen)
            setWindowState(Qt::WindowNoState);
        // Children were just shown or hidden; apply that before measuring
        // so the window also shrinks (e.g. countdown -> "Break complete").
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        m_card->layout()->activate();
        layout()->activate();
        resize(sizeHint());
        const QRect area = screen->availableGeometry();
        move(area.center() - QPoint(width() / 2, height() / 2));
        show();
        raise();
    }
    update();
}

void BreakWindow::clearDimmers()
{
    for (const QPointer<QWidget> &dimmer : std::as_const(m_dimmers)) {
        if (dimmer)
            dimmer->close();
    }
    m_dimmers.clear();
}

void BreakWindow::showTip(int index)
{
    const auto &list = Exercises::all();
    if (index < 0 || index >= list.size())
        return;
    m_tip = index;
    m_tipTitle->setText(list.at(index).title);
    m_tipText->setText(list.at(index).description);
}

void BreakWindow::armButtons()
{
    for (QPushButton *b : {m_skip, m_snooze, m_primary})
        b->setEnabled(false);
    m_armTimer.start(kArmDelayMs);
}

void BreakWindow::onPrimary()
{
    switch (m_mode) {
    case Mode::Reminder:
        Q_EMIT startBreakClicked();
        break;
    case Mode::Break:
        Q_EMIT endBreakClicked();
        break;
    case Mode::Done:
        dismiss();
        break;
    }
}

void BreakWindow::paintEvent(QPaintEvent *)
{
    if (m_fullscreen)
        QPainter(this).fillRect(rect(), kDim);
}

void BreakWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_primary->isEnabled())
            onPrimary();
        return;
    case Qt::Key_Escape:
        if (m_mode == Mode::Reminder && m_snooze->isVisible() && m_snooze->isEnabled())
            Q_EMIT snoozeClicked();
        else if (m_mode == Mode::Done)
            dismiss();
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}

void BreakWindow::mousePressEvent(QMouseEvent *event)
{
    // The card has no title bar; drag it by its background instead.
    if (event->button() == Qt::LeftButton && !m_fullscreen && windowHandle())
        windowHandle()->startSystemMove();
    QWidget::mousePressEvent(event);
}

void BreakWindow::closeEvent(QCloseEvent *event)
{
    // Alt+F4 and the like.
    event->ignore();
    if (m_mode == Mode::Reminder && m_snooze->isVisible())
        Q_EMIT snoozeClicked();
    else if (m_mode == Mode::Done)
        dismiss();
}
