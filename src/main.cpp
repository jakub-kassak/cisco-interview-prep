#include "paint_canvas.h"

#include <QApplication>
#include <QFileDialog>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStatusBar>
#include <QToolBar>

namespace {
void saveCanvas(const PaintCanvas &canvas, QWidget &parent)
{
    QString path = QFileDialog::getSaveFileName(
        &parent, "Save canvas", "drawing.png", "PNG image (*.png)");
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(".png", Qt::CaseInsensitive)) {
        path += ".png";
    }
    if (!canvas.snapshot().save(path, "PNG")) {
        QMessageBox::warning(&parent, "Save failed",
                             "Could not write the image to the selected file.");
    }
}
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("PaintShare");

    QMainWindow window;
    window.setWindowTitle("PaintShare — Drawing prototype");

    auto *canvas = new PaintCanvas;
    auto *scrollArea = new QScrollArea;
    scrollArea->setWidget(canvas); // Qt parents own and delete their children.
    scrollArea->setAlignment(Qt::AlignCenter);
    window.setCentralWidget(scrollArea);

    auto *toolbar = window.addToolBar("Canvas");
    toolbar->setMovable(false);
    auto *clearButton = new QPushButton("Clear");
    auto *saveButton = new QPushButton("Save PNG…");
    toolbar->addWidget(clearButton);
    toolbar->addWidget(saveButton);

    QObject::connect(clearButton, &QPushButton::clicked,
                     canvas, &PaintCanvas::clear);
    QObject::connect(saveButton, &QPushButton::clicked, &window, [&] {
        saveCanvas(*canvas, window);
    });

    window.statusBar()->showMessage(
        "Left-click and drag to draw · Black pen · 960 × 540 bitmap");
    window.resize(1000, 640);
    window.show();
    return app.exec();
}
