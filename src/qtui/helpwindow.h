// ----------------------------------------------------------------------------
// helpwindow.h -- TCIdigi's built-in help
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

#ifndef FLDIGI_QTUI_HELPWINDOW_H
#define FLDIGI_QTUI_HELPWINDOW_H

class QString;
class QWidget;

// Opens the help window (one, reused) at page, a file name in the help
// resource such as "index.html". The pages are built into the program, so
// help works without a network connection.
void showHelp(QWidget* parent, const QString& page);

#endif
