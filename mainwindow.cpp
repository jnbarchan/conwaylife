#include <QDebug>
#include <QFuture>
#include <QGraphicsSceneMouseEvent>
#include <QLabel>
#include <QRandomGenerator>
#include <QtConcurrent>
#include <QThread>
#include <QWheelEvent>
#include <QWidgetAction>

#include "mainwindow.h"
#include "ui_mainwindow.h"


////////// MainWindow Class //////////

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

#if !COUNTER_COLOURS
    ui->actionShowColours->setChecked(false);
    ui->actionShowColours->setEnabled(false);
#endif

    // create the "speed" slider as a submenu of the "Speed" menu item
    this->speedSlider = new QSlider(Qt::Horizontal, ui->menuSpeed);
    speedSlider->setFixedWidth(200);
    speedSlider->setTracking(false);
    speedSlider->setRange(0, 1000);
    QWidgetAction *speedAction = new QWidgetAction(this);
    speedAction->setDefaultWidget(speedSlider);
    ui->menuSpeed->addAction(speedAction);
    // connect `menuSpeed` about to be shown to calculate desired widget action height
    connect(ui->menuSpeed, &QMenu::aboutToShow, this, &MainWindow::menuSpeedAboutToBeShown);

    // make "Use Settings" menu item enable/disable "Thread Settings" menu item
    ui->menuThreadSettings->setEnabled(ui->actionUseThreads->isChecked());
    connect(ui->actionUseThreads, &QAction::toggled, ui->menuThreadSettings, &QMenu::setEnabled);

    // replace the design-time "Thread Count" menu action
    // by a widget with a layout holding the label and a spinbox
    this->threadCountSpinBox = new QSpinBox(ui->menuThreadSettings);
    threadCountSpinBox->setRange(1, 255);
    threadCountSpinBox->setValue(QThread::idealThreadCount());
    QWidget *w = new QWidget(this);
    QHBoxLayout *hl = new QHBoxLayout;
    hl->setContentsMargins(24, 0, 0, 0);
    hl->addWidget(new QLabel(QString("Thread Count (%1)").arg(QThread::idealThreadCount())), 0, Qt::AlignLeft);
    hl->addWidget(threadCountSpinBox, 0, Qt::AlignRight);
    w->setLayout(hl);
    QWidgetAction *threadCountAction = new QWidgetAction(this);
    threadCountAction->setDefaultWidget(w);
    ui->menuThreadSettings->insertAction(ui->actionThreadCount, threadCountAction);
    ui->menuThreadSettings->removeAction(ui->actionThreadCount);
    ui->actionThreadCount = threadCountAction;

    // create an exclusively-checkable group for using QtConcurrent vs QThreads
    QActionGroup *groupWhatThreads = new QActionGroup(ui->menuThreadSettings);
    groupWhatThreads->addAction(ui->actionUseQtConcurrent);
    groupWhatThreads->addAction(ui->actionUseQThreads);
    groupWhatThreads->setExclusive(true);

    this->titlePrefix = this->windowTitle();
    this->generationNumber = 0;

    this->isRunning = this->screenBoardNeedsRefresh = false;
    ui->actionRun->setVisible(true);
    ui->actionPause->setVisible(false);

    // set the run generations timer to once per 0.5 second
    this->timer.setInterval(500);
    connect(&timer, &QTimer::timeout, this, &MainWindow::timerTimeout);
    // connect slider value changed to change generation speed
    speedSlider->setValue(speedSlider->maximum() - timer.interval());
    connect(speedSlider, &QSlider::valueChanged, this, &MainWindow::speedSliderChange);

    // connect menu actions
    connect(ui->actionNew, &QAction::triggered, this, &MainWindow::newBoard);
    connect(ui->actionRandomize, &QAction::triggered, this, &MainWindow::actionRandomize);
    connect(ui->actionRun, &QAction::triggered, this, &MainWindow::actionRun);
    connect(ui->actionPause, &QAction::triggered, this, &MainWindow::actionPause);
    connect(ui->actionStep, &QAction::triggered, this, &MainWindow::actionStep);
    connect(ui->actionExit, &QAction::triggered, this, &MainWindow::actionExit);
    connect(ui->actionFastest, &QAction::triggered, this, &MainWindow::actionFastest);

    // create graphics scene & view
    this->graphicsScene = new LifeGraphicsScene(this);
    this->graphicsView = new LifeGraphicsView(this);
    ui->centralwidget->layout()->addWidget(graphicsView);
    this->graphicsView->setScene(graphicsScene);

    // connect mouse click to toggle counter
    connect(graphicsScene, &LifeGraphicsScene::mouseClicked, this, &MainWindow::scenePosClick);
    // connect context menu click to create context menu
    connect(graphicsScene, &LifeGraphicsScene::contextMenuClicked, this, &MainWindow::sceneContextMenuClick);

    // create an empty board
#if BOARD_C_ARRAYS
    board0 = board1 = nullptr;
#endif
    newBoard();

    //VERYTEMPORARY
    actionFastest();
//    ui->actionUseThreads->setChecked(true);
//    ui->actionUseQtConcurrent->setChecked(true);
    actionRandomize();
}

MainWindow::~MainWindow()
{
    delete ui;

#if BOARD_C_ARRAYS
    if (board0 != nullptr)
    {
        for (int i = 0; i < BOARD_COUNT(board0); i++)
            if (board0[i] != nullptr)
                delete[] board0[i];
        delete[] board0;
    }
    board0 = nullptr;
    if (board1 != nullptr)
    {
        for (int i = 0; i < BOARD_COUNT(board1); i++)
            if (board1[i] != nullptr)
                delete[] board1[i];
        delete[] board1;
    }
    board1 = nullptr;
#endif
}

bool MainWindow::showColours() const
{
#if COUNTER_COLOURS
    return ui->actionShowColours->isChecked();
#else
    return false;
#endif
}

bool MainWindow::useThreads() const
{
    return ui->actionUseThreads->isChecked();
}

int MainWindow::useThreadCount() const
{
    return threadCountSpinBox->value();
}

bool MainWindow::useQtConcurrent() const
{
    return ui->actionUseQtConcurrent->isChecked();
}

bool MainWindow::runDisplay() const
{
    return ui->actionDisplay->isChecked();
}

QPoint MainWindow::scenePosToBoardPos(const QPointF &scenePos) const
{
    // convert a scene position to a board position
    return QPoint((scenePos.x() / LifeGraphicsScene::cellSize) + (boardSize / 2), (scenePos.y() / LifeGraphicsScene::cellSize) + (boardSize / 2));
}

QPointF MainWindow::boardPosToScenePos(const QPoint &boardPos) const
{
    // convert a board position to a scene position
    return QPointF((boardPos.x() - (boardSize / 2)) * LifeGraphicsScene::cellSize, (boardPos.y() - (boardSize / 2)) * LifeGraphicsScene::cellSize);
}

bool MainWindow::boardPosIsValid(const QPoint &boardPos) const
{
    // return whether a board position is within the bounds of the board
    Board &board(*curBoard);
    return (boardPos.y() >= 0 && boardPos.y() < BOARD_COUNT(board)
            && boardPos.x() >= 0 && boardPos.x() < BOARDROW_COUNT(BOARDROW_AT(board, boardPos.y())));
}

Qt::GlobalColor MainWindow::colourForCounter(const Cell &cell) const
{
    // return the colour to use for a counter in a cell
#if COUNTER_COLOURS
    if (showColours())
    {
        if (cell.age < 1)
            return Qt::yellow;
        else if (cell.age < 2)
            return Qt::green;
        else if (cell.age < 4)
            return Qt::blue;
        else if (cell.age < 8)
            return Qt::red;
    }
#else
    Q_UNUSED(cell);
#endif
    return Qt::black;
}

void MainWindow::showCounterForBoardPos(const QPoint &boardPos)
{
    // show the counter (if any) at a board position
    if (screenBoardNeedsRefresh)
        return;
    QPointF scenePos(boardPosToScenePos(boardPos));
    QRectF sceneRect(scenePos, QSize(LifeGraphicsScene::cellSize, LifeGraphicsScene::cellSize));
    graphicsScene->invalidate(sceneRect);
}

int MainWindow::countNeighbours(int y, int x) const
{
    // return how many neighbours a cell has
    const Board &board(*curBoard);
    int neighbours = 0;
    if (--y >= 0)
    {
        if (x > 0 && BOARDCELL_AT(board, y, x - 1).occupied)
            neighbours++;
        if (BOARDCELL_AT(board, y, x).occupied)
            neighbours++;
        if (x < BOARDROW_COUNT(BOARDROW_AT(board, y)) - 1 && BOARDCELL_AT(board, y, x + 1).occupied)
            neighbours++;
    }
    y++;
    if (x > 0 && BOARDCELL_AT(board, y, x - 1).occupied)
        neighbours++;
    if (x < BOARDROW_COUNT(BOARDROW_AT(board, y)) - 1 && BOARDCELL_AT(board, y, x + 1).occupied)
        neighbours++;
    if (++y <= BOARD_COUNT(board) - 1)
    {
        if (x > 0 && BOARDCELL_AT(board, y, x - 1).occupied)
            neighbours++;
        if (BOARDCELL_AT(board, y, x).occupied)
            neighbours++;
        if (x < BOARDROW_COUNT(BOARDROW_AT(board, y)) - 1 && BOARDCELL_AT(board, y, x + 1).occupied)
            neighbours++;
    }
    return neighbours;
}

void MainWindow::stepPass1(bool multiThread /*= false*/, int startRow /*= 0*/, int incRow /*=1*/)
{
    // populate `nextBoard` from `curBoard` by generating a step

    /* RULES:
     * 1. Survival:
     *      counter with 2/3 neighbours => survives
     * 2. Death:
     *      counter with 4+ neighbours => dies (overcrowding)
     *      counter with 0/1 neighbours => dies (isolation)
     * 3. Birth:
     *      no counter with 3 neighbours => birth
     */

//    static bool _debug = true;

//    QElapsedTimer et;
//    et.start();

    const Board &board(*curBoard);
    Board &newBoard(*nextBoard);
    int yStart = 0;
    int yStep = 1;
    if (multiThread)
    {
        Q_ASSERT(incRow > 0);
        Q_ASSERT(startRow >= 0 && startRow < incRow);
        yStart = startRow;
        yStep = incRow;
    }
    for (int y = yStart; y < BOARD_COUNT(board); y += yStep)
        for (int x = 0; x < BOARDROW_COUNT(BOARDROW_AT(board, y)); x++)
        {
            int neighbours = countNeighbours(y, x);
            const Cell &cell(BOARDCELL_AT(board, y, x));
            Cell &newCell(BOARDCELL_SQUARE(newBoard, y, x));
            if (cell.occupied)
            {
                newCell.occupied = (neighbours == 2 || neighbours == 3);
#if COUNTER_COLOURS
                newCell.age = newCell.occupied ? cell.age + 1 : 0;
#endif
            }
            else
            {
                newCell.occupied = (neighbours == 3);
#if COUNTER_COLOURS
                newCell.age = 0;
#endif
            }
        }
//    if (_debug)
//    {
//        QString cpuInfo;
//#ifdef Q_OS_LINUX
////        int cpu = sched_getcpu();
////        cpuInfo = QString("(CPU #%1)").arg(cpu);
//#endif
//        qDebug() << "stepPass1()" << ((et.nsecsElapsed() + 500) / 1000) << cpuInfo;
//    }
}

void MainWindow::showWholeBoard()
{
    // update to show the new board's counters
    graphicsScene->invalidate();
    screenBoardNeedsRefresh = false;
}

void MainWindow::showTitle()
{
    setWindowTitle(QString("%1 [%2]").arg(titlePrefix).arg(generationNumber));
}

void MainWindow::stepPass2()
{
    // swap `curBoard` and `nextBoard`
    if (this->curBoard == &this->board0)
    {
        this->curBoard = &this->board1;
        this->nextBoard = &this->board0;
    }
    else
    {
        this->curBoard = &this->board0;
        this->nextBoard = &this->board1;
    }
    this->generationNumber++;
    // whole board will need refreshing next time it is shown
    screenBoardNeedsRefresh = true;
    // if not displaying as we run then stop here
    if (isRunning && !runDisplay())
    {
        // only reshow title every 500ms or 10 generations
        if (timer.interval() >= 500 || generationNumber % 10 == 0)
            showTitle();
        return;
    }
    // update to show the new board's counters
    showWholeBoard();
    showTitle();
}

void MainWindow::createOrClearBoard(Board &board)
{
    // create a new board
    Cell cell;
#if BOARD_C_ARRAYS
    if (board == nullptr)
    {
        board = new BoardRow[boardSize];
        for (int i = 0; i < BOARD_COUNT(board); i++)
            board[i] = new Cell[boardSize];
    }
    for (int i = 0; i < BOARD_COUNT(board); i++)
        for (int j = 0; j < BOARDROW_COUNT(board[i]); j++)
            board[i][j] = cell;
#else
    board.resize(boardSize);
    for (int i = 0; i < BOARD_COUNT(board); i++)
    {
        board[i].resize(boardSize);
        board[i].fill(cell);
    }
#endif
}

/*slot*/ void MainWindow::newBoard()
{
    actionPause();
    // create or clear board0 & board1
    createOrClearBoard(board0);
    createOrClearBoard(board1);
    this->curBoard = &this->board0;
    this->nextBoard = &this->board1;

    int size = boardSize * LifeGraphicsScene::cellSize;
    // make scene rectangle of size `size` centred at (0, 0)
    graphicsScene->setSceneRect(-size / 2, -size / 2, size, size);

    this->generationNumber = 0;
    showWholeBoard();
    showTitle();
}

/*slot*/ void MainWindow::menuSpeedAboutToBeShown()
{
    // set `speedSlider` widget to be same height as `menuSpeed`
    const QRect &geom(ui->menuRunSettings->actionGeometry(ui->menuSpeed->menuAction()));
    speedSlider->setFixedHeight(geom.height());
}

/*slot*/ void MainWindow::actionRandomize()
{
    newBoard();
    // randomly fill board with counters
    Board &board(*curBoard);
    for (int y = 0; y < BOARD_COUNT(board); y++)
        for (int x = 0; x < BOARDROW_COUNT(BOARDROW_AT(board, y)); x++)
        {
            Cell &cell(BOARDCELL_SQUARE(board, y, x));
            quint32 rand = QRandomGenerator::global()->generate();
            if (rand & 1)
            {
                cell.occupied = true;
#if COUNTER_COLOURS
                cell.age = 0;
#endif
            }
        }
    showWholeBoard();
}

/*slot*/ void MainWindow::actionRun()
{
    // run the generations continuously
    ui->actionRun->setVisible(false);
    ui->actionPause->setVisible(true);
    ui->actionStep->setEnabled(false);

    runStatistics.startGeneration = generationNumber;
    runStatistics.elapsedTimer.start();

    timer.start();
    this->isRunning = true;
}

/*slot*/ void MainWindow::actionPause()
{
    // pause running the generations continuously
    ui->actionRun->setVisible(true);
    ui->actionPause->setVisible(false);
    ui->actionStep->setEnabled(true);

    if (isRunning)
    {
        Q_ASSERT(runStatistics.elapsedTimer.isValid());
        qint64 elapsedTime = runStatistics.elapsedTimer.elapsed();
        if (elapsedTime == 0)
            elapsedTime = 1;
        QString threadUsage = !useThreads() ? "No threads" : useQtConcurrent() ? "QtConcurrent" : "QThreads";
        int threadCount = useThreads() ? useThreadCount() : 1;
        int elapsedGenerations = generationNumber - runStatistics.startGeneration;
        int generationsPerSecond = elapsedGenerations * 1000 / elapsedTime;
        QString message(QString("elapsedTimer: [Use threads: %1, Thread count: %2] %3 generations in %4 milliseconds (%5/sec)")
                        .arg(threadUsage).arg(threadCount)
                        .arg(elapsedGenerations).arg(elapsedTime).arg(generationsPerSecond));
        qDebug().noquote() << message;
    }

    timer.stop();
    this->isRunning = false;

    showTitle();
    if (screenBoardNeedsRefresh)
        showWholeBoard();
}

/*slot*/ void MainWindow::actionStep()
{
    // progress through a single generation

    static bool _debug = false;
    QElapsedTimer et;
    et.start();

    if (useThreads())
    {
        // do rows in sub-threads
        static bool _debug2 = false;
        QElapsedTimer et2;
        et2.start();

        int threadCount = useThreadCount();
        int incRow = threadCount;
        Q_ASSERT(incRow > 0);
        QList<QFuture<void>> futures;
        QList<QThread *> threads;

        // do every `incRow` numbered rows starting from 1/2/3... in sub-threads
        for (int startRow = 1; startRow < incRow; startRow++)
            if (useQtConcurrent())
            {
                futures.append(QtConcurrent::run([=]()->void { this->stepPass1(true, startRow, incRow); }));
            }
            else
            {
                QThread *thread = QThread::create([=]()->void { this->stepPass1(true, startRow, incRow); });
                connect(thread, &QThread::finished, thread, &QObject::deleteLater);
                thread->start();
                threads.append(thread);
            }
        if (_debug2)
        {
            qDebug() << "----------";
            qDebug() << "All threads started" << ((et2.nsecsElapsed() + 500) / 1000);
        }

        // do every `incRow` numbered rows starting from 0 in main thread
        et2.start();
        this->stepPass1(true, 0, incRow);
        if (_debug2)
            qDebug() << "Main thread" << ((et2.nsecsElapsed() + 500) / 1000);
//        if (true)
//        {
//            QString cpuInfo;
//#ifdef Q_OS_LINUX
//            int cpu = sched_getcpu();
//            cpu_set_t set;
//            sched_getaffinity(0, sizeof(set), &set);
//            QString affinities;
//            for (int i = 0; i < CPU_COUNT(&set); i++)
//                if (CPU_ISSET(i, &set))
//                    affinities += QString("%1/").arg(i);
//            cpuInfo = QString("(CPU #%1, affinity %2)").arg(cpu).arg(affinities);
//#endif
//            qDebug() << "Main thread CPU " << cpuInfo;
//        }
        // wait for all sub-threads to complete their rows
        et2.start();
        if (useQtConcurrent())
        {
            for (QFuture<void> &future : futures)
                future.waitForFinished();
        }
        else
        {
            for (QThread *thread : threads)
                thread->wait();
        }
        if (_debug2)
            qDebug() << "All threads wait" << ((et2.nsecsElapsed() + 500) / 1000);
    }
    else
    {
        stepPass1();
    }
    if (_debug)
        qDebug() << "Main thread end stepPass1()" << ((et.nsecsElapsed() + 500) / 1000);

    stepPass2();
}

/*slot*/ void MainWindow::actionExit()
{
    actionPause();
    // exit the application
    qApp->quit();
}

/*slot*/ void MainWindow::scenePosClick(const QPointF scenePos)
{
    // respond to a click on the scene by toggling existence of a counter on the board
    QPoint boardPos(scenePosToBoardPos(scenePos));
    if (!boardPosIsValid(boardPos))
        return;
    Board &board(*curBoard);
    Cell &cell(BOARDCELL_SQUARE(board, boardPos.y(), boardPos.x()));
    cell.occupied = !cell.occupied;
#if COUNTER_COLOURS
    cell.age = 0;
#endif
    showCounterForBoardPos(boardPos);
}

/*slot*/ void MainWindow::sceneContextMenuClick(const QPointF scenePos, const QPoint screenPos)
{
    // respond to a right-click on the scene by showing a context menu
    QPoint boardPos(scenePosToBoardPos(scenePos));
    if (!boardPosIsValid(boardPos))
        return;

    struct Formation {
        QString name;
        QList<QPoint> deltas;
    };
    struct CategoryFormations {
        QString title;
        QVector<Formation> formations;
    };
    static const QVector<CategoryFormations> categories = {
        { "Still Lifes",
          {
              { "Block", { {0,0}, {1,0}, {0,1}, {1,1} } },
              { "Beehive", { {1,0}, {2,0}, {0,1}, {3,1}, {1,2}, {2,2} } },
          }
        },
        { "Oscillators",
          {
              { "Blinker", { {0,0}, {1,0}, {2,0} } },
              { "Beacon", { {0,0}, {0,1}, {1,0}, {3,2}, {2,3}, {3,3} } },
              { "Toad", { {1,0}, {2,0}, {0,1}, {3,2}, {1,3}, {2,3} } },
              { "Clock", { {2,0}, {0,1}, {2,1}, {1,2}, {3,2}, {1,3} } },
          }
        },
        { "Spaceships",
          {
              { "Glider", { {3,1}, {1,1}, {3,2}, {2,3}, {3,3} } },
          }
        },
        { "Glider Guns",
          {
              { "Gosper Glider Gun", {
                    {23,1},
                    {22,2}, {24,2},
                    {12,3}, {13,3}, {21,3}, {23,3}, {24,3}, {35,3}, {36,3},
                    {11,4}, {13,4}, {20,4}, {21,4}, {23,4}, {24,4}, {35,4}, {36,4},
                    {1,5}, {2,5}, {10,5}, {17,5}, {18,5}, {19,5}, {21,5}, {23,5}, {24,5},
                    {1,6}, {2,6}, {10,6}, {13,6}, {16,6}, {19,6}, {22,6}, {24,6},
                    {10,7}, {17,7}, {18,7}, {23,7},
                    {11,8}, {13,8},
                    {12,9}, {13,9},
                }
              },
          }
        },
    };

    QMenu menu;
    for (int i = 0; i < categories.length(); i++)
    {
        QMenu *subMenu = menu.addMenu(categories[i].title);
        const QVector<Formation> &formations(categories[i].formations);
        for (int j = 0; j < formations.length(); j++)
        {
            QAction *action = subMenu->addAction(formations[j].name);
            action->setData(QPoint(i, j));
        }
    }
    QAction *selectedAction = menu.exec(screenPos);
    if (selectedAction == nullptr)
        return;
    QPoint point = selectedAction->data().toPoint();
    int i(point.x()), j(point.y());
    const Formation &formation(categories[i].formations[j]);

    Board &board(*curBoard);
    for (const QPoint &delta : formation.deltas)
    {
        QPoint boardPos2(boardPos.x() + delta.x(), boardPos.y() + delta.y());
        if (!boardPosIsValid(boardPos2))
            continue;
        Cell &cell(BOARDCELL_SQUARE(board, boardPos2.y(), boardPos2.x()));
        cell.occupied = true;
#if COUNTER_COLOURS
        cell.age = 0;
#endif
        showCounterForBoardPos(boardPos2);
    }
}

/*slot*/ void MainWindow::speedSliderChange(int value)
{
    // alter the timer timeout to correspond to the slider position
    timer.setInterval(speedSlider->maximum() - value);
}

/*slot*/ void MainWindow::actionFastest()
{
    speedSlider->setValue(speedSlider->maximum());
    ui->actionDisplay->setChecked(false);
}

/*slot*/ void MainWindow::timerTimeout()
{
    // produce the next generation on `this->timer` timeout
//    for (int i = 0; i < 10; i++)
        actionStep();
}


////////// LifeGraphicsView Class //////////

LifeGraphicsView::LifeGraphicsView(QWidget *parent /*= nullptr*/)
    : QGraphicsView(parent)
{

}

/*virtual*/ void LifeGraphicsView::wheelEvent(QWheelEvent *event) /*override*/
{
    // zoom in/out
    // ensure we deal with with wheel event
    event->accept();
    // set delta scale and the resulting scaling
    qreal deltaScale = 1.0;
    deltaScale += (event->angleDelta().y() > 0) ? 0.1 : -0.1;
    qreal horizontalScale = transform().m11() * deltaScale;
    qreal verticalScale = transform().m22() * deltaScale;
    Q_ASSERT(qFuzzyCompare(horizontalScale, verticalScale));
    // get the view size
    QSize viewSize(rect().size());
    // get the new scaled scence size
    QSizeF sceneSize(sceneRect().size());
    sceneSize *= horizontalScale;
    // ignore if too large/small (limit zoom out/in)
    if (sceneSize.width() > viewSize.width() * 100 || sceneSize.height() > viewSize.height() * 100)
        return;
    if (viewSize.width() > sceneSize.width() * 100 || viewSize.height() > sceneSize.height() * 100)
        return;
    // do the zoom
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    scale(deltaScale, deltaScale);
}


////////// LifeGraphicsScene Class //////////

LifeGraphicsScene::LifeGraphicsScene(MainWindow *mainWindow, QWidget *parent /*= nullptr*/)
    : QGraphicsScene(parent)
{
    Q_ASSERT(mainWindow);
    this->mainWindow = mainWindow;
}

/*virtual*/ void LifeGraphicsScene::contextMenuEvent(QGraphicsSceneContextMenuEvent *contextMenuEvent) /*override*/
{
    // emit a `contextMenuClicked` signal
   emit contextMenuClicked(contextMenuEvent->scenePos(), contextMenuEvent->screenPos());
}

/*virtual*/ void LifeGraphicsScene::mousePressEvent(QGraphicsSceneMouseEvent *mouseEvent) /*override*/
{
    // call the base implementation
    QGraphicsScene::mousePressEvent(mouseEvent);
    // emit a `mouseClicked` signal
    if (mouseEvent->button() == Qt::LeftButton)
        emit mouseClicked(mouseEvent->scenePos());
}

/*virtual*/ void LifeGraphicsScene::drawForeground(QPainter *painter, const QRectF &rect) /*override*/
{
    // call the base method
    QGraphicsScene::drawForeground(painter, rect);

    // draw that part of the scene which lies in `rect`
    const MainWindow::Board &board(*mainWindow->curBoard);
    int yStart = 0, yEnd = BOARD_COUNT(board) - 1;
    int xStart = 0, xEnd = BOARDROW_COUNT(BOARDROW_AT(board, 0)) - 1;
    if (!rect.isEmpty())
    {
        Q_ASSERT(rect.width() >= 0 && rect.height() >= 0);
        QPoint boardTopLeft(mainWindow->scenePosToBoardPos(rect.topLeft()));
        QPoint boardBottomRight(mainWindow->scenePosToBoardPos(rect.bottomRight()));
        yStart = qMax(yStart, boardTopLeft.y());
        yEnd = qMin(yEnd, boardBottomRight.y());
        xStart = qMax(xStart, boardTopLeft.x());
        xEnd = qMin(xEnd, boardBottomRight.x());
    }
    painter->setClipRect(rect);
    for (int y = yStart; y <= yEnd; y++)
        for (int x = xStart; x <= xEnd; x++)
        {
            const MainWindow::Cell &cell(BOARDCELL_AT(board, y, x));
            if (!cell.occupied)
                continue;
            QPoint boardPos(x, y);
            QPointF scenePos(mainWindow->boardPosToScenePos(boardPos));
            QRectF rectCounter(scenePos, QSize(counterSize, counterSize));
            painter->setBrush(mainWindow->colourForCounter(cell));
            painter->drawEllipse(rectCounter);
        }
}
