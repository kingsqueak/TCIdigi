// ----------------------------------------------------------------------------
// theme.h -- flat light/dark appearance for the Qt shell
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

#ifndef FLDIGI_QTUI_THEME_H
#define FLDIGI_QTUI_THEME_H

#include <QObject>
#include <QPalette>
#include <QString>

class Theme : public QObject
{
	Q_OBJECT
public:
	enum class Mode { System, Light, Dark };

	explicit Theme(QObject* parent = nullptr);

	Mode mode() const { return m_mode; }
	void setMode(Mode mode, bool persist);

	static Mode fromString(const QString& text);
	static QString toString(Mode mode);

signals:
	void modeChanged(Theme::Mode mode);

private:
	void apply();
	bool darkEffective() const;
	static QPalette makePalette(bool dark);
	static QString styleSheet();

	Mode m_mode = Mode::System;
	bool m_applied = false;
};

#endif
