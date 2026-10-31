#include "settings.h"
#include "paths.h"
#include "ui_common.h"
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

using namespace UI;

static QString rgba(QColor c, double a) {
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(a, 0, 'f', 2);
}

// A chevron for combo boxes, drawn in the current palette (stylesheets need an image file).
static QString arrowImage() {
    const Palette &c = P();
    const QString path = Paths::runtimeDir() + QStringLiteral("/ytmusic-chevron-%1.png").arg(c.onSurfaceVariant.name().mid(1));
    if (!QFileInfo::exists(path)) {
        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        Icons::paint(p, QRectF(4, 4, 24, 24), QStringLiteral("down"), c.onSurfaceVariant);
        p.end();
        pm.save(path);
    }
    return path;
}

QString UI::appStyleSheet() {
    const Palette &c = P();
    const QString sv = c.surfaceVariant.name(), s = c.surface.name(), on = c.onSurface.name(), onv = c.onSurfaceVariant.name(),
                  ol = c.outline.name(), pr = c.primary.name(), onp = c.onPrimary.name();
    QString css = QStringLiteral(
        "QMenu{background:%1;border:1px solid %5;border-radius:14px;padding:6px}"
        "QMenu::item{color:%3;padding:8px 22px 8px 14px;border-radius:9px;margin:1px 0}"
        "QMenu::item:selected{background:%5}"
        "QMenu::item:disabled{color:%4}"
        "QMenu::separator{height:1px;background:%5;margin:5px 10px}"
        "QMenu::right-arrow{width:10px;height:10px}"
        "QToolTip{background:%1;color:%3;border:1px solid %5;border-radius:8px;padding:5px 8px}"

        "QDialog{background:%2}"
        "QDialog QLabel{color:%3}"
        "QDialog QPushButton{background:transparent;color:%3;border:1px solid %5;border-radius:15px;padding:6px 16px;min-height:18px;font-weight:500}"
        "QDialog QPushButton:hover{background:%5}"
        "QDialog QPushButton:pressed{background:%1}"
        "QDialog QPushButton:disabled{color:%4;border-color:%1}"
        "QDialog QPushButton:default{background:%6;color:%7;border:none}"
        "QDialog QPushButton#accent{background:%6;color:%7;border:none}"
        "QDialog QLineEdit{background:%1;color:%3;border:1px solid %5;border-radius:10px;padding:7px 10px;"
        "selection-background-color:%6;selection-color:%7}"
        "QDialog QLineEdit:focus{border:1px solid %6}"
        "QDialog QComboBox{background:%1;color:%3;border:1px solid %5;border-radius:12px;padding:6px 30px 6px 12px;min-width:170px;"
        "combobox-popup:0}"
        "QDialog QComboBox:hover{border:1px solid %4}"
        "QDialog QComboBox:on{border:1px solid %6}"
        "QDialog QComboBox::drop-down{border:none;width:28px;subcontrol-origin:padding;subcontrol-position:center right}"
        "QDialog QComboBox::down-arrow{image:url(%8);width:14px;height:14px}"
        "QDialog QComboBox QAbstractItemView{background:%1;color:%3;border:1px solid %5;border-radius:10px;padding:4px;outline:0;"
        "selection-background-color:%5;selection-color:%3}"
        "QDialog QComboBox QAbstractItemView::item{min-height:30px;padding:0 8px;border-radius:7px}"
        "QDialog QComboBox QAbstractItemView::item:disabled{color:%4}"
        "QDialog QScrollBar:vertical{background:transparent;width:8px;margin:4px 2px}"
        "QDialog QScrollBar::handle:vertical{background:%5;border-radius:3px;min-height:30px}"
        "QDialog QScrollBar::add-line:vertical,QDialog QScrollBar::sub-line:vertical{height:0}"
        "QDialog QScrollBar::add-page:vertical,QDialog QScrollBar::sub-page:vertical{background:transparent}"
        "QDialog QAbstractItemView{background:%1;color:%3;border:1px solid %5;border-radius:10px;"
        "selection-background-color:%5;selection-color:%3;alternate-background-color:%2}"
        "QDialog QHeaderView::section{background:%2;color:%4;border:none;padding:6px}"
        "QDialog QToolButton{background:transparent;border:1px solid %5;border-radius:8px;padding:4px;color:%3}"
        "QDialog QToolButton:hover{background:%5}")
        .arg(sv, s, on, onv, ol, pr, onp, arrowImage());
    return css;
}

// ------------------------------------------------------------------- Switch
Switch::Switch(QWidget *parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(44, 24);
}

void Switch::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Palette &c = P();
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    const bool on = isChecked();
    QColor track = on ? c.primary : c.surface;
    if (!isEnabled()) track.setAlphaF(0.4);
    p.setPen(on ? Qt::NoPen : QPen(hovered && isEnabled() ? c.onSurfaceVariant : c.outline, 1.5));
    p.setBrush(track);
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
    const qreal d = r.height() - 8;
    const QRectF knob(on ? r.right() - 4 - d : r.left() + 4, r.top() + 4, d, d);
    QColor k = on ? c.onPrimary : c.onSurfaceVariant;
    if (!isEnabled()) k.setAlphaF(0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(k);
    p.drawEllipse(knob);
}

// --------------------------------------------------------------- IconButton
IconButton::IconButton(const QString &i, QWidget *parent) : QAbstractButton(parent), icon(i) {
    setCursor(Qt::PointingHandCursor);
    setFixedSize(34, 34);
}

void IconButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    drawIconButton(p, QRectF(rect()), icon, hovered);
}

// ----------------------------------------------------------- SettingsDialog
static qint64 dirBytes(const QString &path) {
    qint64 n = 0;
    const QFileInfoList fl = QDir(path).entryInfoList(QDir::Files);
    for (const QFileInfo &fi : fl) n += fi.size();
    return n;
}

static QString homeShort(const QString &path) {
    const QString h = QDir::homePath();
    return path.startsWith(h) ? QStringLiteral("~") + path.mid(h.size()) : path;
}

SettingsDialog::SettingsDialog(Core *c, QWidget *parent) : QDialog(parent), core(c) {
    setWindowTitle(QStringLiteral("Settings"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setObjectName(QStringLiteral("settings"));
    resize(600, qMin(720, parent ? parent->height() - 20 : 720));
    setMinimumSize(520, 420);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);

    // header: title + close
    auto *header = new QWidget;
    header->setObjectName(QStringLiteral("header"));
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(26, 20, 18, 12);
    auto *titles = new QVBoxLayout;
    titles->setSpacing(2);
    auto *title = new QLabel(QStringLiteral("Settings"));
    title->setObjectName(QStringLiteral("hTitle"));
    auto *sub = new QLabel(QStringLiteral("Changes apply right away"));
    sub->setObjectName(QStringLiteral("rowHint"));
    titles->addWidget(title);
    titles->addWidget(sub);
    hl->addLayout(titles);
    hl->addStretch();
    auto *close = new IconButton(QStringLiteral("x"));
    close->setToolTip(QStringLiteral("Close (Esc)"));
    connect(close, &QAbstractButton::clicked, this, &QDialog::accept);
    hl->addWidget(close, 0, Qt::AlignTop);
    root->addWidget(header);

    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("scroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    auto *body = new QWidget;
    body->setObjectName(QStringLiteral("body"));
    body->setAutoFillBackground(false);
    auto *bl = new QVBoxLayout(body);
    bl->setContentsMargins(22, 6, 22, 22);
    bl->setSpacing(8);
    scroll->setWidget(body);
    root->addWidget(scroll, 1);

    Downloads *dl = core->downloads;
    const bool ff = Downloads::haveFfmpeg();

    // ---- Playback
    QFrame *play = section(bl, QStringLiteral("Playback"));
    auto *quality = new QComboBox;
    quality->addItem(QStringLiteral("Best available"), 0);
    quality->addItem(QStringLiteral("Balanced · up to 128 kbps"), 1);
    quality->addItem(QStringLiteral("Data saver · up to 64 kbps"), 2);
    quality->setCurrentIndex(core->audioQuality);
    connect(quality, &QComboBox::currentIndexChanged, this, [this, quality] { core->setAudioQuality(quality->currentData().toInt()); });
    row(play, QStringLiteral("Audio quality"), QStringLiteral("For streaming. Lower uses less data."), quality);

    auto *memory = new QComboBox;
    memory->addItem(QStringLiteral("Free quickly · 30 s"), 0);
    memory->addItem(QStringLiteral("Normal · 2 min"), 1);
    memory->addItem(QStringLiteral("Never · instant resume"), 2);
    memory->setCurrentIndex(core->memoryMode);
    connect(memory, &QComboBox::currentIndexChanged, this, [this, memory] { core->setMemoryMode(memory->currentData().toInt()); });
    row(play, QStringLiteral("Release player memory"), QStringLiteral("Shut the audio engine down after being idle this long."), memory);

    auto *page = new QComboBox;
    for (int n : {25, 50, 100}) page->addItem(QStringLiteral("%1 songs").arg(n), n);
    page->setCurrentIndex(qMax(0, page->findData(core->pageSize)));
    connect(page, &QComboBox::currentIndexChanged, this, [this, page] { core->setPageSize(page->currentData().toInt()); });
    row(play, QStringLiteral("Results per search"), QStringLiteral("Also how many “Load more” adds."), page);

    // ---- Downloads
    QFrame *dls = section(bl, QStringLiteral("Downloads"));
    auto *change = new QPushButton(QStringLiteral("Change…"));
    QLabel *dirHint = nullptr;
    row(dls, QStringLiteral("Download folder"), homeShort(dl->dir), change, &dirHint);
    connect(change, &QPushButton::clicked, this, [this, dl, dirHint] {
        QDir().mkpath(dl->dir);
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Download folder"), dl->dir);
        if (d.isEmpty()) return;
        core->setDownloadDir(d);
        dirHint->setText(homeShort(dl->dir));
    });

    auto *fmt = new QComboBox;
    fmt->addItem(QStringLiteral("Original · M4A, no re-encode"), 0);
    fmt->addItem(ff ? QStringLiteral("MP3 · 320 kbps") : QStringLiteral("MP3 · needs ffmpeg"), 1);
    if (!ff) {
        fmt->setItemData(1, false, Qt::UserRole - 1);   // disable the item
    }
    fmt->setCurrentIndex(ff ? dl->format : 0);
    connect(fmt, &QComboBox::currentIndexChanged, this, [this, fmt] { core->setDownloadFormat(fmt->currentData().toInt()); });
    row(dls, QStringLiteral("File format"), QStringLiteral("Original is fastest and keeps YouTube’s audio as is."), fmt);

    auto *tags = new Switch;
    tags->setChecked(ff && dl->embedTags);
    tags->setEnabled(ff);
    connect(tags, &QAbstractButton::toggled, this, [this](bool on) { core->setDownloadTags(on); });
    row(dls, QStringLiteral("Embed cover art & tags"),
        ff ? QStringLiteral("Title, artist and thumbnail inside the file.") : QStringLiteral("Install ffmpeg to enable."), tags);

    auto *open = new QPushButton(QStringLiteral("Open folder"));
    auto countText = [dl] {
        const int n = dl->downloadedCount();
        return n == 0 ? QStringLiteral("Nothing downloaded yet. Right-click a song → Download.")
                      : QStringLiteral("%1 song%2 on this PC · plays offline, starts instantly").arg(n).arg(n == 1 ? "" : "s");
    };
    QLabel *countHint = nullptr;
    row(dls, QStringLiteral("Downloaded songs"), countText(), open, &countHint);
    connect(open, &QPushButton::clicked, this, [dl] {
        QDir().mkpath(dl->dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dl->dir));
    });
    connect(dl, &Downloads::changed, countHint, [countHint, countText] { countHint->setText(countText()); });

    // ---- Window
    QFrame *win = section(bl, QStringLiteral("Window"));
    auto *hidden = new Switch;
    hidden->setChecked(core->startHidden);
    connect(hidden, &QAbstractButton::toggled, this, [this](bool on) { core->setStartHidden(on); });
    row(win, QStringLiteral("Start hidden in the tray"), QStringLiteral("Open the window from the tray icon or the launcher."), hidden);
    auto *tray = new Switch;
    tray->setChecked(core->closeToTray);
    connect(tray, &QAbstractButton::toggled, this, [this](bool on) { core->setCloseToTray(on); });
    row(win, QStringLiteral("Close keeps playing"), QStringLiteral("The close button hides to the tray. Off: it quits."), tray);

    // ---- Library
    QFrame *lib = section(bl, QStringLiteral("Library"));
    auto *exp = new QPushButton(QStringLiteral("Export…"));
    row(lib, QStringLiteral("Back up your library"), QStringLiteral("Likes, playlists and searches to a JSON file (works with the Android app)."), exp);
    connect(exp, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export library"), QDir::homePath() + QStringLiteral("/ytmusic-backup.json"), QStringLiteral("JSON (*.json)"));
        if (path.isEmpty()) return;
        const QString err = core->exportBackup(path);
        emit toast(err.isEmpty() ? QStringLiteral("Library exported") : QStringLiteral("Export failed: ") + err);
    });
    auto *imp = new QPushButton(QStringLiteral("Restore…"));
    row(lib, QStringLiteral("Restore from a backup"), QStringLiteral("Merges into what you have; nothing is removed."), imp);
    connect(imp, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Restore from file"), QDir::homePath(), QStringLiteral("JSON (*.json)"));
        if (path.isEmpty()) return;
        bool ok = false;
        emit toast(core->importBackup(path, &ok));
    });

    // ---- Storage & history
    QFrame *st = section(bl, QStringLiteral("Storage & history"));
    auto *bSearch = new QPushButton(QStringLiteral("Clear"));
    bSearch->setEnabled(!core->history.isEmpty());
    row(st, QStringLiteral("Recent searches"), QStringLiteral("The chips shown under an empty search."), bSearch);
    connect(bSearch, &QPushButton::clicked, this, [this, bSearch] { core->clearHistory(); bSearch->setEnabled(false); });

    auto *bPlays = new QPushButton(QStringLiteral("Clear"));
    bPlays->setObjectName(QStringLiteral("danger"));
    row(st, QStringLiteral("Play history & For you"), QStringLiteral("“Jump back in” and the recommendation shelves."), bPlays);
    connect(bPlays, &QPushButton::clicked, this, [this, bPlays] {
        if (QMessageBox::question(this, QStringLiteral("Clear play history"), QStringLiteral("Clear your recently played list and the For you shelves?")) != QMessageBox::Yes) return;
        core->clearRecentPlays();
        bPlays->setEnabled(false);
    });

    const QString thumbDir = Paths::cacheDir() + QStringLiteral("/thumbs");
    auto *bThumbs = new QPushButton(QStringLiteral("Clear"));
    QLabel *thumbHint = nullptr;
    auto thumbText = [thumbDir] { return QStringLiteral("Cover pictures kept on disk · %1 MB").arg(dirBytes(thumbDir) / 1048576.0, 0, 'f', 1); };
    row(st, QStringLiteral("Picture cache"), thumbText(), bThumbs, &thumbHint);
    connect(bThumbs, &QPushButton::clicked, this, [thumbDir, thumbHint, thumbText] {
        const QFileInfoList fl = QDir(thumbDir).entryInfoList(QDir::Files);
        for (const QFileInfo &fi : fl) QFile::remove(fi.filePath());
        thumbHint->setText(thumbText());
    });

    auto *about = new QLabel(QStringLiteral("Library stored in %1").arg(homeShort(Paths::stateDir())));
    about->setObjectName(QStringLiteral("footer"));
    about->setAlignment(Qt::AlignCenter);
    bl->addSpacing(10);
    bl->addWidget(about);
    bl->addStretch();

    connect(&Theme::I(), &Theme::changed, this, &SettingsDialog::restyle);
    restyle();
}

QFrame *SettingsDialog::section(QVBoxLayout *into, const QString &title) {
    auto *l = new QLabel(title.toUpper());
    l->setObjectName(QStringLiteral("section"));
    into->addSpacing(into->count() ? 12 : 2);
    into->addWidget(l);
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("card"));
    auto *cl = new QVBoxLayout(card);
    cl->setContentsMargins(18, 4, 14, 4);
    cl->setSpacing(0);
    into->addWidget(card);
    return card;
}

void SettingsDialog::row(QFrame *card, const QString &title, const QString &hint, QWidget *control, QLabel **hintOut) {
    auto *cl = static_cast<QVBoxLayout *>(card->layout());
    if (cl->count() > 0) {
        auto *line = new QFrame;
        line->setObjectName(QStringLiteral("divider"));
        line->setFixedHeight(1);
        cl->addWidget(line);
    }
    auto *w = new QWidget;
    auto *hl = new QHBoxLayout(w);
    hl->setContentsMargins(0, 12, 0, 12);
    hl->setSpacing(16);
    auto *texts = new QVBoxLayout;
    texts->setSpacing(3);
    auto *t = new QLabel(title);
    t->setObjectName(QStringLiteral("rowTitle"));
    auto *h = new QLabel(hint);
    h->setObjectName(QStringLiteral("rowHint"));
    h->setWordWrap(true);
    h->setTextInteractionFlags(Qt::TextSelectableByMouse);
    texts->addWidget(t);
    texts->addWidget(h);
    hl->addLayout(texts, 1);
    hl->addWidget(control, 0, Qt::AlignVCenter | Qt::AlignRight);
    if (auto *b = qobject_cast<QPushButton *>(control)) { b->setCursor(Qt::PointingHandCursor); b->setAutoDefault(false); }
    if (auto *cb = qobject_cast<QComboBox *>(control)) cb->setCursor(Qt::PointingHandCursor);
    cl->addWidget(w);
    if (hintOut) *hintOut = h;
}

void SettingsDialog::restyle() {
    const Palette &c = P();
    setStyleSheet(UI::appStyleSheet() + QStringLiteral(
        "#settings{background:transparent}"
        "#scroll,#body{background:transparent}"
        "#hTitle{color:%1;font-size:22px;font-weight:700}"
        "#section{color:%4;font-size:11px;font-weight:700;letter-spacing:1px;padding-left:6px}"
        "#card{background:%2;border:1px solid %3;border-radius:18px}"
        "#divider{background:%3;border:none}"
        "#rowTitle{color:%1;font-size:14px;font-weight:500}"
        "#rowHint{color:%5;font-size:12px}"
        "#footer{color:%5;font-size:11px}"
        "QPushButton#danger{color:%6;border-color:%7}"
        "QPushButton#danger:hover{background:%7}")
        .arg(c.onSurface.name(), c.surfaceVariant.name(), c.outline.name(), c.secondary.name(), c.onSurfaceVariant.name(),
             c.error.name(), rgba(c.error, 0.25)));
    update();
}

void SettingsDialog::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Palette &c = P();
    QColor bg = c.surface;
    bg.setAlphaF(0.97);
    p.setBrush(bg);
    p.setPen(QPen(c.outline, 1));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 24, 24);
}

void SettingsDialog::mousePressEvent(QMouseEvent *e) {
    // frameless: drag the window by any empty spot (mostly the header)
    if (e->button() == Qt::LeftButton && windowHandle()) windowHandle()->startSystemMove();
}

void SettingsDialog::showEvent(QShowEvent *e) {
    QDialog::showEvent(e);
    if (QWidget *pw = parentWidget()) move(pw->geometry().center() - rect().center());
}
