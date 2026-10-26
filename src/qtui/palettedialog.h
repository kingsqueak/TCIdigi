// ----------------------------------------------------------------------------
// palettedialog.h -- the waterfall palette in the Qt shell
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

#ifndef FLDIGI_QTUI_PALETTEDIALOG_H
#define FLDIGI_QTUI_PALETTEDIALOG_H

class QWidget;

// Configure > Waterfall colors: pick one of fldigi's palette files or edit
// the nine colours, seen live on the waterfall. Save keeps it; Cancel puts
// back the palette the dialog opened with.
void editWaterfallPalette(QWidget* parent);

#endif
