// ----------------------------------------------------------------------------
// palettedialog.cpp -- the waterfall palette in the Qt shell
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

#include "palettedialog.h"

#include "engine.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <array>

namespace {

using Palette = std::array<int, 27>;

QColor stop(const Palette& p, int i)
{
	return QColor(p[3 * i], p[3 * i + 1], p[3 * i + 2]);
}

// The 256 levels as fldigi's WFdisp::setcolors makes them: eight runs of
// 32 between the nine colours, weakest signal on the left.
class Gradient : public QWidget
{
public:
	explicit Gradient(QWidget* parent = nullptr) : QWidget(parent)
	{
		setMinimumHeight(28);
	}
	void setPalette9(const Palette& p)
	{
		m_palette = p;
		update();
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		const int w = width();
		for (int x = 0; x < w; ++x) {
			const int level = x * 256 / std::max(1, w);
			const int n = std::min(7, level / 32);
			const int i = level - 32 * n;
			const QColor a = stop(m_palette, n);
			const QColor b = stop(m_palette, n + 1);
			const QColor c(a.red() + i * (b.red() - a.red()) / 32,
				a.green() + i * (b.green() - a.green()) / 32,
				a.blue() + i * (b.blue() - a.blue()) / 32);
			painter.setPen(c);
			painter.drawLine(x, 0, x, height() - 1);
		}
		painter.setPen(palette().color(QPalette::Mid));
		painter.drawRect(rect().adjusted(0, 0, -1, -1));
	}

private:
	Palette m_palette {};
};

QIcon swatch(const QColor& color)
{
	QPixmap chip(22, 22);
	chip.fill(color);
	return QIcon(chip);
}

} // namespace

void editWaterfallPalette(QWidget* parent)
{
	if (!engine_up()) {
		QMessageBox::information(parent, QStringLiteral("Waterfall colors"),
			QStringLiteral("The modem is not running yet."));
		return;
	}
	Palette start {};
	engine_palette(start.data());
	char startName[256] = { 0 };
	engine_palette_name(startName, (int)sizeof startName);

	Palette current = start;
	QString name = QString::fromUtf8(startName);

	QDialog dialog(parent);
	dialog.setObjectName(QStringLiteral("paletteDialog"));
	dialog.setWindowTitle(QStringLiteral("Waterfall colors"));
	dialog.setMinimumWidth(520);
	auto* column = new QVBoxLayout(&dialog);

	auto* about = new QLabel(QStringLiteral(
		"Nine colours, from the weakest signal on the left to the strongest on the right. "
		"The waterfall shows each change at once."));
	about->setWordWrap(true);
	column->addWidget(about);

	auto* pickRow = new QHBoxLayout;
	pickRow->addWidget(new QLabel(QStringLiteral("Palette")));
	auto* files = new QComboBox;
	files->setObjectName(QStringLiteral("paletteFiles"));
	files->addItem(name.isEmpty() ? QStringLiteral("Current") : QStringLiteral("Current (%1)").arg(name));
	char list[8192] = { 0 };
	engine_palette_files(list, (int)sizeof list);
	for (const QString& file : QString::fromUtf8(list).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
		files->addItem(file);
	pickRow->addWidget(files, 1);
	column->addLayout(pickRow);

	auto* gradient = new Gradient;
	column->addWidget(gradient);

	auto* stops = new QHBoxLayout;
	stops->addWidget(new QLabel(QStringLiteral("Weak")));
	std::array<QPushButton*, 9> buttons {};
	for (int i = 0; i < 9; ++i) {
		buttons[i] = new QPushButton;
		buttons[i]->setObjectName(QStringLiteral("paletteStop%1").arg(i));
		buttons[i]->setIconSize(QSize(22, 22));
		buttons[i]->setToolTip(QStringLiteral("Colour %1 of 9. Click to change it.").arg(i + 1));
		stops->addWidget(buttons[i]);
	}
	stops->addWidget(new QLabel(QStringLiteral("Strong")));
	column->addLayout(stops);

	// Shows and applies the palette being edited.
	const auto show = [&] {
		gradient->setPalette9(current);
		for (int i = 0; i < 9; ++i)
			buttons[i]->setIcon(swatch(stop(current, i)));
		engine_set_palette(current.data(), nullptr);
	};
	show();

	QObject::connect(files, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
		Palette loaded {};
		if (index == 0) {
			current = start;
			name = QString::fromUtf8(startName);
		} else if (engine_read_palette(files->itemText(index).toUtf8().constData(), loaded.data())) {
			current = loaded;
			name = files->itemText(index);
		} else {
			QMessageBox::warning(&dialog, QStringLiteral("Waterfall colors"),
				QStringLiteral("%1 could not be read.").arg(files->itemText(index)));
			return;
		}
		show();
	});
	for (int i = 0; i < 9; ++i) {
		QObject::connect(buttons[i], &QPushButton::clicked, &dialog, [&, i] {
			QColorDialog picker(stop(current, i), &dialog);
			picker.setWindowTitle(QStringLiteral("Colour %1 of 9").arg(i + 1));
			picker.setOption(QColorDialog::DontUseNativeDialog, true);
			if (picker.exec() != QDialog::Accepted)
				return;
			const QColor c = picker.selectedColor();
			current[3 * i] = c.red();
			current[3 * i + 1] = c.green();
			current[3 * i + 2] = c.blue();
			name.clear();	// edited: no longer that file's palette
			show();
		});
	}

	auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
	auto* saveAs = box->addButton(QStringLiteral("Save as file…"), QDialogButtonBox::ActionRole);
	saveAs->setToolTip(QStringLiteral("Keep these colours as a palette file, to pick again later."));
	column->addWidget(box);
	QObject::connect(saveAs, &QPushButton::clicked, &dialog, [&] {
		bool ok = false;
		QString file = QInputDialog::getText(&dialog, QStringLiteral("Save palette"),
			QStringLiteral("Palette name"), QLineEdit::Normal,
			name.isEmpty() ? QStringLiteral("my colors")
				: name.endsWith(QStringLiteral(".pal")) ? name.left(name.size() - 4) : name, &ok).trimmed();
		if (!ok || file.isEmpty())
			return;
		if (file.contains(QLatin1Char('/')) || file.contains(QLatin1Char('\\'))) {
			QMessageBox::warning(&dialog, QStringLiteral("Save palette"),
				QStringLiteral("A palette name cannot contain / or \\."));
			return;
		}
		if (!file.endsWith(QStringLiteral(".pal"), Qt::CaseInsensitive))
			file += QStringLiteral(".pal");
		// The Mac disk ignores case: Default.pal is default.pal.
		const int existing = files->findText(file, Qt::MatchFixedString);
		if (existing > 0) {
			file = files->itemText(existing);
			if (QMessageBox::question(&dialog, QStringLiteral("Save palette"),
				    QStringLiteral("Replace %1?").arg(file)) != QMessageBox::Yes)
				return;
		}
		if (!engine_write_palette(file.toUtf8().constData(), current.data())) {
			QMessageBox::warning(&dialog, QStringLiteral("Save palette"),
				QStringLiteral("%1 could not be written.").arg(file));
			return;
		}
		name = file;
		if (files->findText(file, Qt::MatchFixedString) < 0) {
			const QSignalBlocker block(files);
			files->addItem(file);
			files->setCurrentIndex(files->count() - 1);
		}
	});
	QObject::connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	if (dialog.exec() == QDialog::Accepted) {
		engine_set_palette(current.data(), name.isEmpty() ? "custom" : name.toUtf8().constData());
		engine_save_prefs();
	} else {
		engine_set_palette(start.data(), nullptr);
	}
}
