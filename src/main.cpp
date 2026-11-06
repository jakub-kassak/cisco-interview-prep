#include "paint_canvas.h"

#include <QApplication>
#include <QFileDialog>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStatusBar>
#include <QToolBar>

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
        QString path = QFileDialog::getSaveFileName(
            &window, "Save canvas", "drawing.png", "PNG image (*.png)");
        if (path.isEmpty()) {
            return;
        }
        if (!path.endsWith(".png", Qt::CaseInsensitive)) {
            path += ".png";
        }
        if (!canvas->snapshot().save(path, "PNG")) {
            QMessageBox::warning(&window, "Save failed",
                                 "Could not write the image to the selected file.");
        }
    });

    window.statusBar()->showMessage(
        "Left-click and drag to draw · Black pen · 960 × 540 bitmap");
    window.resize(1000, 640);
    window.show();
    return app.exec();
}
