// ----------------------------------------------------------------------------
// mainwindow.h -- Qt shell main window
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

#ifndef FLDIGI_QTUI_MAINWINDOW_H
#define FLDIGI_QTUI_MAINWINDOW_H

#include "theme.h"

#include <QByteArray>
#include <QColor>
#include <QMainWindow>
#include <QString>

class FreqReadout;
class ModemMenu;
class QFont;
class QShowEvent;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;

class MainWindow : public QMainWindow
{
	Q_OBJECT
public:
	explicit MainWindow(Theme* theme, QWidget* parent = nullptr);

protected:
	void showEvent(QShowEvent* event) override;

private:
	void buildMenus();
	void syncAppearance();
	void setTciAudio(bool on);
	void notConnected(const QString& what);
	void holdStatus(const QString& text);
	void applyTextFont(const QFont& font);
	void chooseTextFont();
	// Received text and the echo of sent text take these colors in the
	// receive pane; sent text also in the transmit box. An invalid color
	// follows the theme. Notices always follow the theme.
	void applyTextColors();
	// Copy the modem transmit buffer into the transmit box, including while
	// that box has focus. m_txSeen is the last text taken from the modem.
	void showEngineTxText();
	// New receive text waits in m_rxHeld while the pane is scrolled back,
	// so reading history is not interrupted. Reaching the bottom adds it.
	void addRxText(const QString& text, const QByteArray& kinds);
	void releaseRxText();

	Theme* m_theme = nullptr;
	bool m_applying = false;
	ModemMenu* m_mode = nullptr;
	QPushButton* m_modeStatus = nullptr;
	QDoubleSpinBox* m_rxLevel = nullptr;
	FreqReadout* m_freq = nullptr;
	QLabel* m_status = nullptr;
	QPlainTextEdit* m_rx = nullptr;
	QString m_rxHeld;
	// One ENGINE_RX_* kind for each QChar of m_rxHeld.
	QByteArray m_rxHeldKinds;
	QColor m_rxColor;
	QColor m_txColor;
	// Received backspaces for text already in the pane, applied on release.
	int m_rxErase = 0;
	QPlainTextEdit* m_tx = nullptr;
	QString m_txSeen;
	QTimer* m_statusTimer = nullptr;
	QAction* m_appearanceActions[3] = {};
};

#endif
