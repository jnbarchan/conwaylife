#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QElapsedTimer>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMainWindow>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVector>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class LifeGraphicsScene;
class LifeGraphicsView;
class LifeCounter;


// compile-time support for using C-style arrays for the board, rather than Qt `QVector`s
#define BOARD_C_ARRAYS 1

// compile-time support for counter colours, or not
#define COUNTER_COLOURS 0

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    struct Cell {
        bool occupied = false;
#if COUNTER_COLOURS
        int age = 0;
#endif
    };

#if BOARD_C_ARRAYS
    typedef Cell *BoardRow;
    typedef BoardRow *Board;
    #define BOARD_COUNT(board) (board ? MainWindow::boardSize : 0)
    #define BOARDROW_COUNT(boardrow) (boardrow ? MainWindow::boardSize : 0)
    #define BOARDCELL_AT(board, y, x) board[y][x]
    #define BOARDCELL_SQUARE(board, y, x) board[y][x]
    #define BOARDROW_AT(board, y) board[y]
#else
    typedef QVector<Cell> BoardRow;
    typedef QVector<BoardRow> Board;
    #define BOARD_COUNT(board) board.count()
    #define BOARDROW_COUNT(boardrow) boardrow.count()
    #define BOARDCELL_AT(board, y, x) board.at(y).at(x)
    #define BOARDCELL_SQUARE(board, y, x) board[y][x]
    #define BOARDROW_AT(board, y) board.at(y)
#endif

    static constexpr int boardSize = 1000;

    Board *curBoard;

    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    QPoint scenePosToBoardPos(const QPointF &scenePos) const;
    QPointF boardPosToScenePos(const QPoint &boardPos) const;
    Qt::GlobalColor colourForCounter(const Cell &cell) const;

private:
    Ui::MainWindow *ui;
    QSlider *speedSlider;
    QSpinBox *threadCountSpinBox;
    LifeGraphicsScene *graphicsScene;
    LifeGraphicsView *graphicsView;
    QTimer timer;
    Board board0, board1;
    Board *nextBoard;
    QString titlePrefix;
    int generationNumber;
    bool isRunning, screenBoardNeedsRefresh;
    struct {
        QElapsedTimer elapsedTimer;
        int startGeneration;
    } runStatistics;

    bool showColours() const;
    bool useThreads() const;
    int useThreadCount() const;
    bool useQtConcurrent() const;
    bool runDisplay() const;
    bool boardPosIsValid(const QPoint &boardPos) const;
    void showCounterForBoardPos(const QPoint &boardPos);
    int countNeighbours(int y, int x) const;
    void stepPass1(bool multiThread = false, int startRow = 0, int incRow =1);
    void showWholeBoard();
    void showTitle();
    void stepPass2();
    void createOrClearBoard(Board &board);

private slots:
    void newBoard();
    void menuSpeedAboutToBeShown();
    void actionRandomize();
    void actionRun();
    void actionPause();
    void actionStep();
    void actionExit();
    void scenePosClick(const QPointF scenePos);
    void sceneContextMenuClick(const QPointF scenePos, const QPoint screenPos);
    void speedSliderChange(int value);
    void actionFastest();
    void timerTimeout();
};


class LifeGraphicsView : public QGraphicsView
{
    Q_OBJECT

public:
    LifeGraphicsView(QWidget *parent = nullptr);

protected:
    virtual void wheelEvent(QWheelEvent *event) override;
};

class LifeGraphicsScene : public QGraphicsScene
{
    Q_OBJECT

public:
    static constexpr int counterSize = 20;
    static constexpr int cellSize = 25;
    LifeGraphicsScene(MainWindow *mainWindow, QWidget *parent = nullptr);

private:
    const MainWindow *mainWindow;

protected:
    virtual void contextMenuEvent(QGraphicsSceneContextMenuEvent *contextMenuEvent) override;
    virtual void mousePressEvent(QGraphicsSceneMouseEvent *mouseEvent) override;
    virtual void drawForeground(QPainter *painter, const QRectF &rect) override;

signals:
    void contextMenuClicked(QPointF scenePos, QPoint screenPos);
    void mouseClicked(QPointF scenePos);
};


#endif // MAINWINDOW_H
