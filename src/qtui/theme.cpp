// ----------------------------------------------------------------------------
// theme.cpp -- flat light/dark appearance for the Qt shell
//
// Copyright (C) 2026
//
// This file is part of fldigi.
//
// Fldigi is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// ----------------------------------------------------------------------------

#include "theme.h"

#include <QApplication>
#include <QColor>
#include <QGuiApplication>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

namespace {
const char* kSettingsKey = "appearance";
}

Theme::Theme(QObject* parent)
	: QObject(parent)
{
	QSettings settings;
	m_mode = fromString(settings.value(kSettingsKey, QStringLiteral("system")).toString());

	QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"));
	if (fusion)
		QApplication::setStyle(fusion);

	apply();

	// Qt 6.5+ reports the desktop scheme on Windows, macOS, and Linux
	// desktops that publish one (GNOME, KDE, and the Windows/macOS
	// platform themes). Unknown stays on the light palette until the
	// user pins Light or Dark.
	connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
		this, [this](Qt::ColorScheme) {
			if (m_mode == Mode::System)
				apply();
		});
}

Theme::Mode Theme::fromString(const QString& text)
{
	const QString name = text.trimmed().toLower();
	if (name == QLatin1String("light"))
		return Mode::Light;
	if (name == QLatin1String("dark"))
		return Mode::Dark;
	return Mode::System;
}

QString Theme::toString(Mode mode)
{
	switch (mode) {
	case Mode::Light: return QStringLiteral("light");
	case Mode::Dark: return QStringLiteral("dark");
	case Mode::System: break;
	}
	return QStringLiteral("system");
}

void Theme::setMode(Mode mode, bool persist)
{
	m_mode = mode;
	if (persist) {
		QSettings settings;
		settings.setValue(kSettingsKey, toString(mode));
	}
	apply();
	emit modeChanged(mode);
}

bool Theme::darkEffective() const
{
	if (m_mode == Mode::Dark)
		return true;
	if (m_mode == Mode::Light)
		return false;
	return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

QPalette Theme::makePalette(bool dark)
{
	QPalette p;
	auto set = [&p](QPalette::ColorRole role, const char* hex) {
		p.setColor(role, QColor(QString::fromLatin1(hex)));
	};
	if (!dark) {
		set(QPalette::Window,          "#f4f5f7");
		set(QPalette::WindowText,      "#1c1e21");
		set(QPalette::Base,            "#ffffff");
		set(QPalette::AlternateBase,   "#eef0f3");
		set(QPalette::Text,            "#1c1e21");
		set(QPalette::Button,          "#e8eaee");
		set(QPalette::ButtonText,      "#1c1e21");
		set(QPalette::BrightText,      "#ffffff");
		set(QPalette::Highlight,       "#2f6fed");
		set(QPalette::HighlightedText, "#ffffff");
		set(QPalette::Mid,             "#d5d8de");
		set(QPalette::Dark,            "#b7bcc6");
		set(QPalette::Light,           "#ffffff");
		set(QPalette::Midlight,        "#e6e8ed");
		set(QPalette::Shadow,          "#c5c9d2");
		set(QPalette::PlaceholderText, "#6b7280");
		set(QPalette::ToolTipBase,     "#ffffff");
		set(QPalette::ToolTipText,     "#1c1e21");
		set(QPalette::Link,            "#2f6fed");
		p.setColor(QPalette::Disabled, QPalette::Text, QColor("#9aa0a8"));
		p.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#9aa0a8"));
		p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#9aa0a8"));
	} else {
		set(QPalette::Window,          "#1c1d21");
		set(QPalette::WindowText,      "#e7e8eb");
		set(QPalette::Base,            "#26272c");
		set(QPalette::AlternateBase,   "#222328");
		set(QPalette::Text,            "#e7e8eb");
		set(QPalette::Button,          "#2e3036");
		set(QPalette::ButtonText,      "#e7e8eb");
		set(QPalette::BrightText,      "#ffffff");
		set(QPalette::Highlight,       "#4c8dff");
		set(QPalette::HighlightedText, "#0d1117");
		set(QPalette::Mid,             "#3c3f46");
		set(QPalette::Dark,            "#15161a");
		set(QPalette::Light,           "#3c3f46");
		set(QPalette::Midlight,        "#34363c");
		set(QPalette::Shadow,          "#0e0f12");
		set(QPalette::PlaceholderText, "#8b909a");
		set(QPalette::ToolTipBase,     "#2e3036");
		set(QPalette::ToolTipText,     "#e7e8eb");
		set(QPalette::Link,            "#8eb6ff");
		p.setColor(QPalette::Disabled, QPalette::Text, QColor("#6e737c"));
		p.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#6e737c"));
		p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#6e737c"));
	}
	return p;
}

QString Theme::styleSheet()
{
	return QStringLiteral(
		"QWidget { font-size: 13px; }"
		"QMainWindow { background: palette(window); }"
		"QMenuBar { background: palette(window); border: none; padding: 2px 4px; }"
		"QMenuBar::item { padding: 5px 10px; background: transparent; border-radius: 6px; }"
		"QMenuBar::item:selected { background: palette(mid); }"
		"QMenu { background: palette(base); border: 1px solid palette(mid); padding: 4px; }"
		"QMenu::item { padding: 6px 28px 6px 16px; border-radius: 6px; }"
		"QMenu::item:selected { background: palette(highlight); color: palette(highlighted-text); }"
		"QComboBox, QLineEdit, QPlainTextEdit {"
		"  background: palette(base);"
		"  color: palette(text);"
		"  border: 1px solid palette(mid);"
		"  border-radius: 8px;"
		"  padding: 4px 8px;"
		"  selection-background-color: palette(highlight);"
		"  selection-color: palette(highlighted-text);"
		"}"
		"QLineEdit#qrzCall[worked=\"true\"] {"
		"  background: #d8f3e0;"
		"  color: #1c1e21;"
		"  border: 1px solid #20a048;"
		"}"
		"QPlainTextEdit#rxText, QWidget#rxPicture {"
		"  background: palette(base);"
		"  color: palette(text);"
		"  border: 1px solid palette(mid);"
		"  border-top-left-radius: 8px;"
		"  border-top-right-radius: 8px;"
		"  border-bottom-left-radius: 0;"
		"  border-bottom-right-radius: 0;"
		"  border-bottom-width: 0;"
		"}"
		"QPlainTextEdit#txText {"
		"  border-top-left-radius: 0;"
		"  border-top-right-radius: 0;"
		"  border-bottom-left-radius: 8px;"
		"  border-bottom-right-radius: 8px;"
		"  border-top-width: 0;"
		"}"
		"QToolButton#modemList {"
		"  background: palette(base);"
		"  color: palette(text);"
		"  border: 1px solid palette(mid);"
		"  border-radius: 8px;"
		"  padding: 4px 22px 4px 10px;"
		"  text-align: left;"
		"}"
		"QToolButton#modemList:hover { background: palette(midlight); }"
		"QToolButton#modemList::menu-indicator { subcontrol-position: right center; subcontrol-origin: padding; }"
		"QComboBox::drop-down { border: none; width: 22px; }"
		"QComboBox::down-arrow { image: url(:/chevron.svg); width: 10px; height: 6px; }"
		"QComboBox QAbstractItemView {"
		"  background: palette(base);"
		"  color: palette(text);"
		"  border: 1px solid palette(mid);"
		"  selection-background-color: palette(highlight);"
		"  selection-color: palette(highlighted-text);"
		"  outline: 0;"
		"}"
		"QPushButton {"
		"  background: palette(button);"
		"  color: palette(button-text);"
		"  border: 1px solid palette(mid);"
		"  border-radius: 8px;"
		"  padding: 6px 12px;"
		"}"
		"QPushButton:hover { background: palette(midlight); }"
		"QPushButton:pressed { background: palette(mid); }"
		"QPushButton:disabled {"
		"  color: palette(placeholder-text);"
		"  background: palette(window);"
		"}"
		"QWidget#freqReadout {"
		"  font-size: 28px;"
		"  font-weight: 600;"
		"  background: palette(base);"
		"  color: palette(placeholder-text);"
		"  border: 1px solid palette(mid);"
		"  border-radius: 8px;"
		"  padding: 4px 14px;"
		"}"
		"QLabel#sectionLabel {"
		"  color: palette(placeholder-text);"
		"  font-size: 11px;"
		"  padding: 0;"
		"}"
		"QPushButton#stripButton, QPushButton#toggleButton, QPushButton#rxId, QPushButton#txId,"
		"QPushButton#trxButton, QPushButton#tuneButton {"
		"  padding: 4px 10px;"
		"  border-radius: 6px;"
		"}"
		"QPushButton#toggleButton:checked, QPushButton#toggleButton:checked:hover,"
		"QPushButton#rxId:checked, QPushButton#rxId:checked:hover,"
		"QPushButton#txId:checked, QPushButton#txId:checked:hover {"
		"  background: palette(highlight);"
		"  color: palette(highlighted-text);"
		"  border: 1px solid palette(highlight);"
		"}"
		"QPushButton#trxButton:checked, QPushButton#trxButton:checked:hover,"
		"QPushButton#tuneButton:checked, QPushButton#tuneButton:checked:hover {"
		"  background: #c42828;"
		"  color: white;"
		"  border: 1px solid #c42828;"
		"}"
		"QPushButton#macroTimer, QPushButton#macroTimer:hover {"
		"  background: #20a048;"
		"  color: white;"
		"  border: 1px solid #178a3c;"
		"  font-weight: 600;"
		"}"
		"QLabel#statusField {"
		"  background: palette(base);"
		"  color: palette(text);"
		"  border: 1px solid palette(mid);"
		"  border-radius: 6px;"
		"  padding: 4px 8px;"
		"}"
		"QSpinBox, QDoubleSpinBox {"
		"  background: palette(base);"
		"  color: palette(text);"
		"  border: 1px solid palette(mid);"
		"  border-radius: 6px;"
		"  padding: 0 2px 0 6px;"
		"}"
		"QAbstractSpinBox QLineEdit {"
		"  background: transparent;"
		"  border: none;"
		"  border-radius: 0;"
		"  padding: 0;"
		"  margin: 0;"
		"}"
		"QSpinBox::up-button, QDoubleSpinBox::up-button {"
		"  subcontrol-origin: border;"
		"  subcontrol-position: top right;"
		"  width: 16px;"
		"  border: none;"
		"  border-left: 1px solid palette(mid);"
		"  background: palette(button);"
		"  border-top-right-radius: 5px;"
		"}"
		"QSpinBox::down-button, QDoubleSpinBox::down-button {"
		"  subcontrol-origin: border;"
		"  subcontrol-position: bottom right;"
		"  width: 16px;"
		"  border: none;"
		"  border-left: 1px solid palette(mid);"
		"  border-top: 1px solid palette(mid);"
		"  background: palette(button);"
		"  border-bottom-right-radius: 5px;"
		"}"
		"QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {"
		"  image: url(:/chevron-up.svg);"
		"  width: 8px;"
		"  height: 5px;"
		"}"
		"QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {"
		"  image: url(:/chevron.svg);"
		"  width: 8px;"
		"  height: 5px;"
		"}"
		"QProgressBar#signalMeter, QProgressBar#vuMeter {"
		"  border: 1px solid palette(mid);"
		"  border-radius: 4px;"
		"  background: palette(base);"
		"}"
		"QProgressBar#signalMeter::chunk, QProgressBar#vuMeter::chunk {"
		"  background: palette(highlight);"
		"  border-radius: 3px;"
		"}"
		"QSlider::groove:vertical {"
		"  background: palette(base);"
		"  border: 1px solid palette(mid);"
		"  width: 6px;"
		"  border-radius: 3px;"
		"}"
		"QSlider#squelchOverlay {"
		"  background: transparent;"
		"}"
		"QSlider#squelchOverlay::groove:vertical {"
		"  background: transparent;"
		"  border: none;"
		"  width: 18px;"
		"}"
		"QSlider#squelchOverlay::handle:vertical {"
		"  background: palette(window-text);"
		"  border: none;"
		"  height: 3px;"
		"  margin: 0;"
		"  border-radius: 1px;"
		"}"
		"QSlider::handle:vertical {"
		"  background: palette(button);"
		"  border: 1px solid palette(mid);"
		"  height: 14px;"
		"  margin: 0 -5px;"
		"  border-radius: 4px;"
		"}"
		"QSlider::groove:horizontal {"
		"  background: palette(base);"
		"  border: 1px solid palette(mid);"
		"  height: 6px;"
		"  border-radius: 3px;"
		"}"
		"QSlider::handle:horizontal {"
		"  background: palette(button);"
		"  border: 1px solid palette(mid);"
		"  width: 14px;"
		"  margin: -5px 0;"
		"  border-radius: 4px;"
		"}"
		"QWidget#signalBrowser {"
		"  background: palette(base);"
		"  border: 1px solid palette(mid);"
		"  border-radius: 8px;"
		"}"
		"QWidget#signalBrowser[floating=\"true\"] {"
		"  border: none;"
		"  border-radius: 0;"
		"}"
		"QListWidget#pskBrowser {"
		"  background: transparent;"
		"  alternate-background-color: palette(alternate-base);"
		"  color: palette(text);"
		"  border: none;"
		"  outline: 0;"
		"}"
		"QListWidget#pskBrowser::item { padding: 1px 8px; }"
		"QListWidget#pskBrowser::item:selected {"
		"  background: palette(highlight);"
		"  color: palette(highlighted-text);"
		"}"
		"QStatusBar {"
		"  background: palette(window);"
		"  color: palette(window-text);"
		"  border-top: 1px solid palette(mid);"
		"}"
		"QScrollBar:vertical { background: transparent; width: 10px; margin: 4px 2px; }"
		"QScrollBar::handle:vertical { background: palette(mid); border-radius: 4px; min-height: 24px; }"
		"QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
		"QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px 4px; }"
		"QScrollBar::handle:horizontal { background: palette(mid); border-radius: 4px; min-width: 24px; }"
		"QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
		"QToolTip {"
		"  background: palette(tooltip-base);"
		"  color: palette(tooltip-text);"
		"  border: 1px solid palette(mid);"
		"  padding: 4px 6px;"
		"}"
	);
}

void Theme::apply()
{
	if (!qobject_cast<QApplication*>(QCoreApplication::instance()))
		return;
	QApplication::setPalette(makePalette(darkEffective()));
	qApp->setStyleSheet(styleSheet());
	m_applied = true;
}
