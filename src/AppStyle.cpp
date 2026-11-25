#include "AppStyle.h"

#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QFontDatabase>
#include <QPalette>

void applyAppStyle(QApplication &app)
{
    const auto theme = QSettings("Klippit","Klippit").value("appearance/theme").toString();
    QString base = "#14191e", surface = "#1c242b", accent = "#91e1bf";
    if (theme == "Nord") { base = "#242b36"; surface = "#2e3440"; accent = "#88c0d0"; }
    if (theme == "Tokyo Night") { base = "#161821"; surface = "#202334"; accent = "#7aa2f7"; }
    if (theme == "Catppuccin Mocha") { base = "#1e1e2e"; surface = "#313244"; accent = "#cba6f7"; }
    app.setStyle("Fusion");
    auto palette = app.palette();
    palette.setColor(QPalette::Window, QColor(base));
    palette.setColor(QPalette::Base, QColor("#171e24"));
    palette.setColor(QPalette::AlternateBase, QColor("#222b33"));
    palette.setColor(QPalette::Button, QColor("#252e36"));
    palette.setColor(QPalette::Mid, QColor("#485762"));
    palette.setColor(QPalette::Dark, QColor("#101419"));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipText})
        palette.setColor(role, QColor("#edf3f4"));
    palette.setColor(QPalette::ToolTipBase, QColor("#252e36"));
    palette.setColor(QPalette::PlaceholderText, QColor("#9aaab3"));
    palette.setColor(QPalette::Link, QColor(accent));
    palette.setColor(QPalette::Highlight, QColor(accent));
    palette.setColor(QPalette::HighlightedText, QColor("#13231b"));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, QColor("#82909a"));
    app.setPalette(palette);

    auto font = app.font();
    const auto families = QFontDatabase::families();
    for (const auto &family : {QStringLiteral("Inter"), QStringLiteral("Geist")}) {
        if (families.contains(family)) {
            font.setFamily(family);
            break;
        }
    }
    font.setPointSize(10);
    app.setFont(font);

    QFile sheet(":/appearance/klippit.qss");
    if (sheet.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(sheet.readAll()).replace("#1c242b", surface).replace("#91e1bf", accent));
}
