#include "Editor.h"
#include <QApplication>
#ifdef Q_OS_WIN
#include "ReplayTray.h"
#endif

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Editor editor;
#ifdef Q_OS_WIN
    ReplayTray replayTray(editor);
#endif
    editor.show();
    return app.exec();
}
