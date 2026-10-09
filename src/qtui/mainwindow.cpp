// ----------------------------------------------------------------------------
// mainwindow.cpp -- Qt shell main window
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

#include "mainwindow.h"

#include "engine.h"
#include "qrzclient.h"
#include "waterfallview.h"
#include "tcidigi_version.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFontDialog>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QStyleOption>
#include <QStyleOptionViewItem>

#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QAbstractItemView>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>
#include <QWheelEvent>

#include <cmath>
#include <functional>

namespace {

bool callChar(QChar ch)
{
	return ch.isLetterOrNumber() || ch == QLatin1Char('/');
}

QString callTokenAt(const QString& text, int index)
{
	if (text.isEmpty() || index < 0 || index >= text.size() || !callChar(text.at(index)))
		return {};
	int start = index;
	int end = index + 1;
	while (start > 0 && callChar(text.at(start - 1)))
		--start;
	while (end < text.size() && callChar(text.at(end)))
		++end;
	return text.mid(start, end - start);
}

bool looksLikeCall(const QString& raw)
{
	static const QRegularExpression re(
		QStringLiteral("^(?:[A-Z0-9]{1,4}/)?[A-Z]{1,2}[0-9][A-Z0-9]{1,4}(?:/[A-Z0-9]{1,4})?$"),
		QRegularExpression::CaseInsensitiveOption);
	return re.match(raw.trimmed()).hasMatch();
}

QString callAtText(const QString& text, int index)
{
	QString token = callTokenAt(text, index);
	if (!looksLikeCall(token) && index > 0)
		token = callTokenAt(text, index - 1);
	if (!looksLikeCall(token))
		return {};
	return token.toUpper();
}

int textIndexAt(const QFontMetrics& fm, const QString& text, int x)
{
	if (text.isEmpty() || x < 0)
		return -1;
	int acc = 0;
	for (int i = 0; i < text.size(); ++i) {
		acc += fm.horizontalAdvance(text.at(i));
		if (x < acc)
			return i;
	}
	return -1;
}

} // helpers

class QrzRowActions : public QObject
{
	Q_OBJECT
public:
	explicit QrzRowActions(QObject* parent = nullptr)
		: QObject(parent)
	{
	}

	std::function<void(const QString&)> setCall;

	void apply(const QString& call) const
	{
		if (setCall)
			setCall(call);
	}
};

void qrzApplyCall(QWidget* row, const QString& call)
{
	if (!row)
		return;
	if (auto* actions = row->findChild<QrzRowActions*>(QStringLiteral("qrzActions")))
		actions->apply(call);
}

namespace {
QPushButton* stripButton(const QString& text, const QString& tip)
{
	auto* button = new QPushButton(text);
	button->setObjectName(QStringLiteral("stripButton"));
	button->setToolTip(tip);
	button->setFocusPolicy(Qt::NoFocus);
	return button;
}

QPushButton* toggleButton(const QString& text, const QString& tip, bool on)
{
	auto* button = stripButton(text, tip);
	button->setObjectName(QStringLiteral("toggleButton"));
	button->setCheckable(true);
	button->setChecked(on);
	return button;
}

QSpinBox* counterBox(int min, int max, int value, int width, const QString& tip)
{
	auto* box = new QSpinBox;
	box->setRange(min, max);
	box->setValue(value);
	box->setFixedWidth(width);
	box->setAlignment(Qt::AlignRight);
	box->setToolTip(tip);
	return box;
}

QLabel* statusField(int width)
{
	auto* label = new QLabel;
	label->setObjectName(QStringLiteral("statusField"));
	label->setMinimumWidth(width);
	label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	return label;
}

// Receive over transmit. The 6px line is the only gap. m_ratio is the
// receive share of the two panes, and a window resize keeps that share.
class TextSplit : public QWidget
{
public:
	TextSplit(QWidget* top, QWidget* bottom, QWidget* parent = nullptr)
		: QWidget(parent)
		, m_top(top)
		, m_bottom(bottom)
	{
		top->setParent(this);
		bottom->setParent(this);
		m_line = new Line(this);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setMinimumHeight(m_minPane * 2 + kLine);
		QSettings settings;
		m_ratio = settings.value(QStringLiteral("textRatio"), 0.72).toDouble();
		if (m_ratio < 0.12 || m_ratio > 0.88)
			m_ratio = 0.72;
	}

	QSize sizeHint() const override { return QSize(640, 280); }

protected:
	void resizeEvent(QResizeEvent*) override { applyRatio(); }

private:
	static constexpr int kLine = 6;
	static constexpr int m_minPane = 64;

	class Line : public QWidget
	{
	public:
		explicit Line(TextSplit* split)
			: QWidget(split)
			, m_split(split)
		{
			setFixedHeight(kLine);
			setCursor(Qt::SplitVCursor);
			setMouseTracking(true);
			setToolTip(QStringLiteral("Drag to resize receive and transmit."));
		}

	protected:
		void paintEvent(QPaintEvent*) override
		{
			QPainter painter(this);
			painter.fillRect(rect(), palette().color(m_hot ? QPalette::Highlight : QPalette::Mid));
		}

		void enterEvent(QEnterEvent*) override
		{
			m_hot = true;
			update();
		}

		void leaveEvent(QEvent*) override
		{
			m_hot = false;
			update();
		}

		void mousePressEvent(QMouseEvent* event) override
		{
			if (event->button() == Qt::LeftButton)
				m_split->beginDrag(event->globalPosition().toPoint());
		}

		void mouseMoveEvent(QMouseEvent* event) override
		{
			if (event->buttons() & Qt::LeftButton)
				m_split->dragTo(event->globalPosition().toPoint());
		}

		void mouseReleaseEvent(QMouseEvent* event) override
		{
			if (event->button() == Qt::LeftButton)
				m_split->endDrag();
		}

	private:
		TextSplit* m_split = nullptr;
		bool m_hot = false;
	};

	void applyRatio()
	{
		const int avail = qMax(1, height() - kLine);
		int topH = qRound(m_ratio * avail);
		if (avail <= m_minPane * 2)
			topH = avail / 2;
		else
			topH = qBound(m_minPane, topH, avail - m_minPane);
		m_top->setGeometry(0, 0, width(), topH);
		m_line->setGeometry(0, topH, width(), kLine);
		m_bottom->setGeometry(0, topH + kLine, width(), avail - topH);
	}

	void beginDrag(const QPoint& globalPos)
	{
		m_dragOffset = globalPos.y() - mapToGlobal(QPoint(0, m_line->y())).y();
		m_dragging = true;
	}

	void dragTo(const QPoint& globalPos)
	{
		if (!m_dragging)
			return;
		const int avail = qMax(1, height() - kLine);
		const int local = mapFromGlobal(QPoint(0, globalPos.y() - m_dragOffset)).y();
		int topH = local;
		if (avail <= m_minPane * 2)
			topH = avail / 2;
		else
			topH = qBound(m_minPane, topH, avail - m_minPane);
		m_ratio = double(topH) / double(avail);
		applyRatio();
	}

	void endDrag()
	{
		if (!m_dragging)
			return;
		m_dragging = false;
		QSettings settings;
		settings.setValue(QStringLiteral("textRatio"), m_ratio);
	}

	QWidget* m_top = nullptr;
	QWidget* m_bottom = nullptr;
	Line* m_line = nullptr;
	double m_ratio = 0.72;
	bool m_dragging = false;
	int m_dragOffset = 0;
};

// Hell paints characters into a picture. The text pane stays underneath and
// comes back for every other mode.
class HellPicture : public QWidget
{
public:
	explicit HellPicture(QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setObjectName(QStringLiteral("rxPicture"));
		setAccessibleName(QStringLiteral("Hell receive"));
		setAttribute(Qt::WA_StyledBackground, true);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setMinimumHeight(64);
		setContextMenuPolicy(Qt::CustomContextMenu);
		setToolTip(QStringLiteral(
			"Hell receive. Characters are painted here instead of as text.\n"
			"Right-click and choose Clear to wipe the picture."));
		connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
			QMenu menu(this);
			QAction* clear = menu.addAction(QStringLiteral("Clear"));
			if (menu.exec(mapToGlobal(pos)) == clear && engine_up())
				engine_rx_picture_clear();
		});
	}

	void setFrame(const QImage& image)
	{
		m_image = image;
		update();
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		const QRect box = contentsRect();
		painter.fillRect(box, palette().color(QPalette::Base));
		if (m_image.isNull() || box.isEmpty())
			return;
		if (m_image.size() == box.size())
			painter.drawImage(box.topLeft(), m_image);
		else
			painter.drawImage(box, m_image);
	}

private:
	QImage m_image;
};

class RxPane : public QWidget
{
public:
	RxPane(QPlainTextEdit* text, HellPicture* picture, QWidget* parent = nullptr)
		: QWidget(parent)
		, m_text(text)
		, m_picture(picture)
	{
		text->setParent(this);
		picture->setParent(this);
		picture->hide();
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setMinimumHeight(64);
	}

	void setPicture(bool on)
	{
		if (m_pictureMode == on)
			return;
		m_pictureMode = on;
		m_text->setVisible(!on);
		m_picture->setVisible(on);
		m_text->setGeometry(rect());
		m_picture->setGeometry(rect());
	}

	HellPicture* pictureWidget() const { return m_picture; }

protected:
	void resizeEvent(QResizeEvent*) override
	{
		m_text->setGeometry(rect());
		m_picture->setGeometry(rect());
	}

private:
	QPlainTextEdit* m_text = nullptr;
	HellPicture* m_picture = nullptr;
	bool m_pictureMode = false;
};

// A modal dialog opened from a mouse press keeps Qt's button grab. Later
// left clicks are then delivered nowhere. Run fn only after every button is up.
static void runWhenMouseUp(QObject* context, std::function<void()> fn)
{
	auto* timer = new QTimer(context);
	timer->setInterval(15);
	QObject::connect(timer, &QTimer::timeout, context, [timer, fn = std::move(fn)] {
		if (QGuiApplication::mouseButtons() != Qt::NoButton)
			return;
		timer->stop();
		fn();
		timer->deleteLater();
	});
	timer->start();
}

// Four fldigi macro sets, twelve buttons each. Width tracks the font so an
// 8-character label fits, and a longer label widens that one button.
class MacroButtons : public QWidget
{
public:
	explicit MacroButtons(std::function<void(int)> activate, std::function<void(int)> edit,
		QWidget* parent = nullptr)
		: QWidget(parent)
		, m_activate(std::move(activate))
		, m_edit(std::move(edit))
	{
		auto* row = new QHBoxLayout(this);
		row->setContentsMargins(0, 0, 0, 0);
		row->setSpacing(6);
		for (int i = 0; i < kCount; ++i) {
			auto* button = new QPushButton(this);
			button->setObjectName(QStringLiteral("macroButton"));
			button->setFocusPolicy(Qt::NoFocus);
			button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
			button->setContextMenuPolicy(Qt::CustomContextMenu);
			m_buttons[i] = button;
			row->addWidget(button);
			connect(button, &QPushButton::clicked, this, [this, i] {
				m_activate(m_bankIndex * kCount + i);
			});
			connect(button, &QPushButton::customContextMenuRequested, this, [this, i] {
				const int index = m_bankIndex * kCount + i;
				runWhenMouseUp(this, [this, index] {
					if (m_edit)
						m_edit(index);
				});
			});
		}
		m_bank = new QPushButton(this);
		m_bank->setObjectName(QStringLiteral("macroBank"));
		m_bank->setFocusPolicy(Qt::NoFocus);
		m_bank->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
		m_bank->setContextMenuPolicy(Qt::CustomContextMenu);
		row->addWidget(m_bank);
		connect(m_bank, &QPushButton::clicked, this, [this] { step(1); });
		connect(m_bank, &QPushButton::customContextMenuRequested, this, [this] { step(-1); });
		applyBank();
	}

	void setLabel(int index, const QString& name)
	{
		if (index < 0 || index >= kCount || !m_buttons[index])
			return;
		if (m_buttons[index]->text() == name)
			return;
		m_buttons[index]->setText(name);
		m_buttons[index]->setToolTip(tipFor(index, name));
		applyWidths();
	}

	int bank() const { return m_bankIndex; }

	// slot is the button in the bank on screen, 0 at the left through 11.
	void trigger(int slot)
	{
		if (slot < 0 || slot >= kCount || !m_activate)
			return;
		m_activate(m_bankIndex * kCount + slot);
	}

	void applyWidths()
	{
		if (m_sizing || !m_buttons[0])
			return;
		m_sizing = true;
		m_buttons[0]->ensurePolished();
		for (int i = 0; i < kCount; ++i)
			m_buttons[i]->setFixedWidth(buttonWidth(m_buttons[i], m_buttons[i]->text(), true));
		m_bank->setFixedWidth(buttonWidth(m_bank, QStringLiteral("4"), false));
		updateGeometry();
		m_sizing = false;
	}

protected:
	void changeEvent(QEvent* event) override
	{
		if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
			applyWidths();
		QWidget::changeEvent(event);
	}

	void showEvent(QShowEvent* event) override
	{
		QWidget::showEvent(event);
		applyWidths();
	}

private:
	static constexpr int kCount = 12;
	static constexpr int kBanks = 4;

	static QString tipFor(int index, const QString& name)
	{
		const QString key = QStringLiteral("F%1").arg(index + 1);
		if (name.isEmpty())
			return QStringLiteral("Empty macro. %1 runs it. Right-click to edit.").arg(key);
		return QStringLiteral("Run this macro. %1 runs it. Right-click to edit.").arg(key);
	}

	static int buttonWidth(QPushButton* button, const QString& text, bool eightWide)
	{
		button->ensurePolished();
		const QFontMetrics metrics(button->font());
		const int eight = metrics.horizontalAdvance(QStringLiteral("00000000"));
		const int label = metrics.horizontalAdvance(text);
		const int textWidth = eightWide ? qMax(eight, label) : qMax(label, metrics.horizontalAdvance(QStringLiteral("4")));
		QStyleOptionButton option;
		option.initFrom(button);
		option.text = text.isEmpty() && eightWide ? QStringLiteral("00000000") : text;
		const QSize contents(textWidth, metrics.height());
		return button->style()->sizeFromContents(
			QStyle::CT_PushButton, &option, contents, button).width();
	}

	void step(int delta)
	{
		m_bankIndex = (m_bankIndex + delta) % kBanks;
		if (m_bankIndex < 0)
			m_bankIndex += kBanks;
		applyBank();
	}

	void applyBank()
	{
		static const char* names[kBanks][kCount] = {
			{ "RsID CQ", "ANS @>|", "QSO @>>", "KN @||", "SK @||", "Me/Qth",
			  "Brag", "", "T/R", "Tx @>>", "Rx @||", "TX @>|" },
			{ "C Ans @>|", "C rpt @>|", "C Rep @>|", "C Incr", "C Decr", "Log QSO",
			  "CW-CQ @>|", "", "CQ @-3+", "CQ-ID @>|", "", "" },
			{ "", "", "", "", "", "", "", "", "", "", "", "" },
			{ "", "", "", "", "", "", "", "", "", "", "", "" }
		};
		for (int i = 0; i < kCount; ++i) {
			const QString name = QString::fromLatin1(names[m_bankIndex][i]);
			m_buttons[i]->setText(name);
			m_buttons[i]->setToolTip(tipFor(i, name));
		}
		m_bank->setText(QString::number(m_bankIndex + 1));
		m_bank->setToolTip(QStringLiteral(
			"Macro set %1 of 4. Click for the next set, right-click for the previous.")
			.arg(m_bankIndex + 1));
		applyWidths();
	}

	std::function<void(int)> m_activate;
	std::function<void(int)> m_edit;
	QPushButton* m_buttons[kCount] = {};
	QPushButton* m_bank = nullptr;
	int m_bankIndex = 0;
	bool m_sizing = false;
};

// Detected signal level, with the squelch slider drawn on top of the bar.
class SignalColumn : public QWidget
{
public:
	explicit SignalColumn(QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setObjectName(QStringLiteral("signalColumn"));
		setFixedWidth(20);
		setMinimumHeight(96);
		setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
		setToolTip(QStringLiteral(
			"Detected signal level. The bar turns green when it passes the squelch mark."));
		m_slider = new QSlider(Qt::Vertical, this);
		m_slider->setObjectName(QStringLiteral("squelchOverlay"));
		m_slider->setRange(0, 100);
		// Minimum at the bottom, same direction as the level fill.
		m_slider->setInvertedAppearance(false);
		m_slider->setValue(15);
		connect(m_slider, &QSlider::valueChanged, this, [this](int) { update(); });
		m_slider->setFocusPolicy(Qt::NoFocus);
		m_slider->setCursor(Qt::SplitVCursor);
		m_slider->setToolTip(QStringLiteral("Squelch level on the signal meter."));
	}

	void setLevel(int level)
	{
		level = qBound(0, level, 100);
		if (m_level == level)
			return;
		m_level = level;
		update();
	}

	QSlider* squelch() const { return m_slider; }

protected:
	void resizeEvent(QResizeEvent*) override
	{
		m_slider->setGeometry(rect());
	}

	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		const QRect box = rect().adjusted(0, 0, -1, -1);
		painter.setPen(palette().color(QPalette::Mid));
		painter.setBrush(palette().color(QPalette::Base));
		painter.drawRect(box);
		if (m_level > 0) {
			const int inner = qMax(0, box.height() - 2);
			const int filled = inner * qBound(0, m_level, 100) / 100;
			const bool over = m_level > m_slider->value();
			painter.fillRect(box.left() + 1, box.bottom() - filled, qMax(1, box.width() - 1), filled,
				over ? QColor(32, 160, 72) : palette().color(QPalette::Highlight));
		}
		// Same 0..100 scale as the level meter: 0 is -80 dB, 100 is +20 dB.
		painter.setPen(palette().color(QPalette::WindowText));
		for (int db = -60; db <= 0; db += 20) {
			const int y = box.bottom() - (db + 80) * (box.height() - 1) / 100;
			painter.drawLine(box.left() + 1, y, box.left() + 4, y);
		}
	}

private:
	QSlider* m_slider = nullptr;
	int m_level = 0;
};

// Horizontal signal level. The engine reports 0..100 for -80..+20 dB.
class LevelMeter : public QWidget
{
public:
	explicit LevelMeter(QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setObjectName(QStringLiteral("levelMeter"));
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		setMinimumWidth(80);
		setToolTip(QStringLiteral(
			"Signal level. Blue up to -50 dB, green from -50 to -5, red from -5 to +20."));
	}

	QSize sizeHint() const override
	{
		const int labels = QFontMetrics(font()).height();
		return QSize(180, 16 + labels);
	}

	void setLevel(int level)
	{
		level = qBound(0, level, 100);
		if (m_level == level)
			return;
		m_level = level;
		update();
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		const QFontMetrics fm(font());
		const int labelH = fm.height();
		const QRect bar = rect().adjusted(0, 0, -1, -labelH);
		painter.setPen(palette().color(QPalette::Mid));
		painter.setBrush(palette().color(QPalette::Base));
		painter.drawRoundedRect(bar, 4, 4);

		auto xOf = [&](int mark) {
			return bar.left() + (mark + 80) * qMax(1, bar.width() - 1) / 100;
		};
		// 0 on the meter is -80 dB. Blue through -50, green to -5, red through +20.
		if (m_level > 0 && bar.width() > 4 && bar.height() > 4) {
			const QRect inner = bar.adjusted(1, 1, -1, -1);
			const int filled = qMax(1, inner.width() * m_level / 100);
			QPainterPath clip;
			clip.addRoundedRect(QRectF(inner.left(), inner.top(), filled, inner.height()), 3, 3);
			painter.save();
			painter.setClipPath(clip);
			painter.setPen(Qt::NoPen);
			const struct {
				int from;
				int to;
				QColor color;
			} zones[] = {
				{-80, -50, QColor(47, 111, 237)},
				{-50, -5, QColor(32, 160, 72)},
				{-5, 20, QColor(196, 40, 40)},
			};
			for (const auto& zone : zones) {
				const int x0 = xOf(zone.from);
				const int x1 = xOf(zone.to);
				painter.fillRect(QRect(x0, inner.top(), qMax(0, x1 - x0), inner.height()), zone.color);
			}
			painter.restore();
		}

		painter.setPen(palette().color(QPalette::PlaceholderText));
		const bool showTen = (xOf(0) - xOf(-20)) >= fm.horizontalAdvance(QStringLiteral("-20-10"));
		for (int mark = -80; mark <= 20; mark += 10) {
			const int x = xOf(mark);
			const bool labeled = (mark % 20) == 0 || mark == 20 || (mark == -10 && showTen);
			painter.drawLine(x, bar.bottom() - (labeled ? 7 : 4), x, bar.bottom());
			if (!labeled)
				continue;
			const QString text = QString::number(mark);
			const int tw = fm.horizontalAdvance(text);
			int tx = x - tw / 2;
			if (mark == -80)
				tx = bar.left();
			else if (mark == 20)
				tx = bar.right() - tw;
			painter.drawText(tx, rect().bottom() - 1, text);
		}
	}

private:
	int m_level = 0;
};

// Text, macros, and the waterfall share the spare height. The 6px bar is the
// top of the waterfall; dragging it keeps that share when the window resizes.
class DisplaySplit : public QWidget
{
public:
	DisplaySplit(QWidget* text, QWidget* macros, QWidget* scope, QWidget* parent = nullptr)
		: QWidget(parent)
		, m_text(text)
		, m_macros(macros)
		, m_scope(scope)
	{
		text->setParent(this);
		macros->setParent(this);
		scope->setParent(this);
		m_line = new Line(this);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		QSettings settings;
		m_ratio = settings.value(QStringLiteral("waterfallRatio"), 0.36).toDouble();
		if (m_ratio < 0.16 || m_ratio > 0.72)
			m_ratio = 0.36;
	}

	QSize sizeHint() const override { return QSize(800, 520); }
	QSize minimumSizeHint() const override
	{
		return QSize(640, m_minText + fixedHeight() + m_minScope);
	}

protected:
	void resizeEvent(QResizeEvent*) override { applyRatio(); }

private:
	static constexpr int kLine = 6;
	static constexpr int kGap = 8;
	static constexpr int m_minText = 134;
	static constexpr int m_minScope = 96;

	class Line : public QWidget
	{
	public:
		explicit Line(DisplaySplit* split)
			: QWidget(split)
			, m_split(split)
		{
			setFixedHeight(kLine);
			setCursor(Qt::SplitVCursor);
			setMouseTracking(true);
			setToolTip(QStringLiteral("Drag to resize the waterfall and signal level."));
		}

	protected:
		void paintEvent(QPaintEvent*) override
		{
			QPainter painter(this);
			painter.fillRect(rect(), palette().color(m_hot ? QPalette::Highlight : QPalette::Mid));
		}
		void enterEvent(QEnterEvent*) override
		{
			m_hot = true;
			update();
		}
		void leaveEvent(QEvent*) override
		{
			m_hot = false;
			update();
		}
		void mousePressEvent(QMouseEvent* event) override
		{
			if (event->button() == Qt::LeftButton)
				m_split->beginDrag(event->globalPosition().toPoint());
		}
		void mouseMoveEvent(QMouseEvent* event) override
		{
			if (event->buttons() & Qt::LeftButton)
				m_split->dragTo(event->globalPosition().toPoint());
		}
		void mouseReleaseEvent(QMouseEvent* event) override
		{
			if (event->button() == Qt::LeftButton)
				m_split->endDrag();
		}

	private:
		DisplaySplit* m_split = nullptr;
		bool m_hot = false;
	};

	int fixedHeight() const
	{
		// The same gap sits above the macro row and between that row and the drag bar.
		return m_macros->sizeHint().height() + kLine + 2 * kGap;
	}

	void applyRatio()
	{
		const int macroH = qMax(1, m_macros->sizeHint().height());
		const int avail = qMax(1, height() - macroH - kLine - 2 * kGap);
		int scopeH = qRound(m_ratio * avail);
		if (avail <= m_minText + m_minScope)
			scopeH = avail / 2;
		else
			scopeH = qBound(m_minScope, scopeH, avail - m_minText);
		const int textH = avail - scopeH;
		int y = 0;
		m_text->setGeometry(0, y, width(), textH);
		y += textH + kGap;
		m_macros->setGeometry(0, y, width(), macroH);
		y += macroH + kGap;
		m_line->setGeometry(0, y, width(), kLine);
		y += kLine;
		m_scope->setGeometry(0, y, width(), scopeH);
	}

	void beginDrag(const QPoint& globalPos)
	{
		m_dragOffset = globalPos.y() - mapToGlobal(QPoint(0, m_line->y())).y();
		m_dragging = true;
	}

	void dragTo(const QPoint& globalPos)
	{
		if (!m_dragging)
			return;
		const int macroH = qMax(1, m_macros->sizeHint().height());
		const int avail = qMax(1, height() - macroH - kLine - 2 * kGap);
		const int local = mapFromGlobal(QPoint(0, globalPos.y() - m_dragOffset)).y();
		int textH = local - macroH - 2 * kGap;
		if (avail <= m_minText + m_minScope)
			textH = avail / 2;
		else
			textH = qBound(m_minText, textH, avail - m_minScope);
		m_ratio = double(avail - textH) / double(avail);
		applyRatio();
	}

	void endDrag()
	{
		if (!m_dragging)
			return;
		m_dragging = false;
		QSettings settings;
		settings.setValue(QStringLiteral("waterfallRatio"), m_ratio);
	}

	QWidget* m_text = nullptr;
	QWidget* m_macros = nullptr;
	QWidget* m_scope = nullptr;
	Line* m_line = nullptr;
	double m_ratio = 0.36;
	bool m_dragging = false;
	int m_dragOffset = 0;
};

// Channel number and audio frequency stay put. The rest is the newest part of
// the decoded text that fits in a few wrapped lines.
QString fittedBrowserLine(const QString& full, const QFont& font, int width, int* height)
{
	const QFontMetrics fm(font);
	const int lineH = qMax(1, fm.lineSpacing());
	const int maxH = lineH * 8;
	const int textW = qMax(16, width);
	const auto bounds = [&](const QString& s) {
		return fm.boundingRect(QRect(0, 0, textW, 100000),
			int(Qt::AlignLeft | Qt::TextWordWrap), s);
	};
	const int fullH = bounds(full).height();
	if (fullH <= maxH) {
		if (height)
			*height = qMax(lineH, fullH);
		return full;
	}
	int prefix = 0;
	if (full.size() >= 8 && full.at(2) == QLatin1Char(' ') && full.at(7) == QLatin1Char(' '))
		prefix = 8;
	const QString head = full.left(prefix);
	const QString body = full.mid(prefix);
	int best = body.size();
	int low = 0;
	int high = body.size();
	while (low <= high) {
		const int mid = low + (high - low) / 2;
		if (bounds(head + body.mid(mid)).height() <= maxH) {
			best = mid;
			high = mid - 1;
		} else
			low = mid + 1;
	}
	const QString shown = head + body.mid(best);
	if (height)
		*height = qMax(lineH, bounds(shown).height());
	return shown;
}

class BrowserStreamDelegate : public QStyledItemDelegate
{
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	void paint(QPainter* painter, const QStyleOptionViewItem& option,
		const QModelIndex& index) const override
	{
		QStyleOptionViewItem opt = option;
		initStyleOption(&opt, index);
		opt.text = fittedBrowserLine(opt.text, opt.font, qMax(16, opt.rect.width() - 8), nullptr);
		opt.features |= QStyleOptionViewItem::WrapText;
		opt.textElideMode = Qt::ElideNone;
		const QWidget* widget = opt.widget;
		QStyle* style = widget ? widget->style() : QApplication::style();
		style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
	}

	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		const QString full = index.data(Qt::DisplayRole).toString();
		int width = 80;
		if (const auto* view = qobject_cast<const QAbstractItemView*>(option.widget))
			width = qMax(40, view->viewport()->width() - 8);
		else if (option.rect.width() > 0)
			width = option.rect.width();
		int textH = 0;
		fittedBrowserLine(full, option.font, qMax(16, width - 8), &textH);
		return QSize(width, textH + 6);
	}
};

class BrowserList : public QListWidget
{
public:
	using QListWidget::QListWidget;

protected:
	void resizeEvent(QResizeEvent* event) override
	{
		QListWidget::resizeEvent(event);
		const int width = viewport()->width();
		if (width == m_width)
			return;
		m_width = width;
		doItemsLayout();
	}

private:
	int m_width = -1;
};

// fldigi's embedded viewer: channel list, seek field, squelch, and Clear.
// Undock lifts this same widget into its own resizable window.
class BrowserPane : public QWidget
{
public:
	explicit BrowserPane(std::function<void(const QString&)> note, QWidget* parent = nullptr)
		: QWidget(parent)
		, m_note(std::move(note))
	{
		setObjectName(QStringLiteral("signalBrowser"));
		setAttribute(Qt::WA_StyledBackground, true);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setMinimumWidth(320);
		auto* column = new QVBoxLayout(this);
		column->setContentsMargins(6, 6, 6, 6);
		column->setSpacing(4);

		m_list = new BrowserList(this);
		m_list->setObjectName(QStringLiteral("pskBrowser"));
		m_list->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
		m_list->setAlternatingRowColors(true);
		m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
		m_list->setTextElideMode(Qt::ElideNone);
		m_list->setSelectionMode(QAbstractItemView::SingleSelection);
		m_list->setItemDelegate(new BrowserStreamDelegate(m_list));
		m_list->setToolTip(QStringLiteral(
			"Click a channel to tune it. Click a call sign to fill the lookup box.\n"
			"Each channel keeps the decoded text and clears after 300 seconds of silence.\n"
			"Right click clears that line."));
		m_list->setContextMenuPolicy(Qt::CustomContextMenu);
		column->addWidget(m_list, 1);

		m_seek = new QLineEdit(this);
		m_seek->setObjectName(QStringLiteral("viewerSeek"));
		m_seek->setPlaceholderText(QStringLiteral("seek - regular expression"));
		m_seek->setToolTip(QStringLiteral("seek - regular expression"));
		m_seek->setClearButtonEnabled(true);
		column->addWidget(m_seek);

		auto* bottom = new QHBoxLayout;
		bottom->setSpacing(6);
		// PSK viewer squelch is -3.0..6.0 in tenths. 3.0 is fldigi's default.
		m_squelch = new QSlider(Qt::Horizontal, this);
		m_squelch->setObjectName(QStringLiteral("viewerSquelch"));
		m_squelch->setRange(-30, 60);
		m_squelch->setValue(30);
		m_squelch->setSingleStep(1);
		m_squelch->setPageStep(10);
		m_squelch->setFixedHeight(22);
		m_squelch->setToolTip(QStringLiteral(
			"Viewer squelch. A higher setting hides weaker channels.\n"
			"PSK, RTTY, and CW each keep their own level."));
		m_squelchValue = new QLabel(QStringLiteral("3.0"), this);
		m_squelchValue->setObjectName(QStringLiteral("viewerSquelchValue"));
		m_squelchValue->setMinimumWidth(40);
		m_squelchValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		connect(m_squelch, &QSlider::valueChanged, this, [this](int tenths) {
			m_squelchValue->setText(QString::number(tenths / 10.0, 'f', 1));
		});

		auto* clear = stripButton(QStringLiteral("Clear"),
			QStringLiteral("Left click to clear text\nRight click to reset frequencies"));
		clear->setObjectName(QStringLiteral("stripButton"));
		clear->setFixedWidth(75);
		clear->setContextMenuPolicy(Qt::CustomContextMenu);
		m_undock = stripButton(QStringLiteral("Undock"),
			QStringLiteral("Undock the signal browser into its own window."));
		m_undock->setMinimumWidth(75);

		bottom->addWidget(m_squelch, 1);
		bottom->addWidget(m_squelchValue);
		bottom->addWidget(clear);
		bottom->addWidget(m_undock);
		column->addLayout(bottom);

		connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
			if (m_pick)
				m_pick(m_list->row(item));
			else if (m_note)
				m_note(QStringLiteral("Browser"));
			if (!m_call || !item)
				return;
			const QPoint vp = m_list->viewport()->mapFromGlobal(QCursor::pos());
			QStyleOptionViewItem opt;
			opt.initFrom(m_list);
			opt.rect = m_list->visualItemRect(item);
			opt.text = item->text();
			opt.features |= QStyleOptionViewItem::HasDisplay;
			opt.font = m_list->font();
			const QRect textRect = m_list->style()->subElementRect(
				QStyle::SE_ItemViewItemText, &opt, m_list);
			const int index = textIndexAt(
				QFontMetrics(m_list->font()), item->text(), vp.x() - textRect.x());
			const QString call = callAtText(item->text(), index);
			if (!call.isEmpty())
				m_call(call);
		});
		connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
			QListWidgetItem* item = m_list->itemAt(pos);
			if (!item)
				return;
			const int row = m_list->row(item);
			if (m_clearLine)
				m_clearLine(row);
			else
				item->setText(channelLabel(row + 1));
		});
		connect(clear, &QPushButton::clicked, this, [this] {
			if (m_clearAll)
				m_clearAll();
			else
				refill();
		});
		connect(clear, &QPushButton::customContextMenuRequested, this, [this] {
			if (m_note)
				m_note(QStringLiteral("Browser frequencies"));
		});
		connect(m_undock, &QPushButton::clicked, this, [this] {
			if (m_undockToggle)
				m_undockToggle();
		});
		refill();
		setFloating(false);
	}

	void setUndockHandler(std::function<void()> handler) { m_undockToggle = std::move(handler); }
	void setPickHandler(std::function<void(int)> pick) { m_pick = std::move(pick); }
	void setCallHandler(std::function<void(const QString&)> call) { m_call = std::move(call); }
	void setClearHandler(std::function<void(int)> clearLine, std::function<void()> clearAll)
	{
		m_clearLine = std::move(clearLine);
		m_clearAll = std::move(clearAll);
	}

	void setChannelText(int row, const QString& text)
	{
		if (!m_list || row < 0 || row >= m_list->count())
			return;
		if (QListWidgetItem* item = m_list->item(row)) {
			if (item->text() == text)
				return;
			item->setText(text);
			item->setToolTip(text);
		}
	}

	QSlider* viewerSquelch() const { return m_squelch; }

	void setFloating(bool floating)
	{
		m_undock->setText(floating ? QStringLiteral("Dock") : QStringLiteral("Undock"));
		m_undock->setToolTip(floating
			? QStringLiteral("Dock the signal browser beside receive and transmit.")
			: QStringLiteral("Undock the signal browser into its own window."));
		setProperty("floating", floating);
		style()->unpolish(this);
		style()->polish(this);
		update();
	}

private:
	static constexpr int kChannels = 30;

	static QString channelLabel(int channel)
	{
		return QStringLiteral("%1").arg(channel, 2, 10, QChar(' '));
	}

	void refill()
	{
		m_list->clear();
		for (int i = 1; i <= kChannels; ++i)
			m_list->addItem(channelLabel(i));
		m_list->clearSelection();
	}

	std::function<void(const QString&)> m_note;
	std::function<void(int)> m_pick;
	std::function<void(const QString&)> m_call;
	std::function<void(int)> m_clearLine;
	std::function<void()> m_clearAll;
	std::function<void()> m_undockToggle;
	QListWidget* m_list = nullptr;
	QLineEdit* m_seek = nullptr;
	QSlider* m_squelch = nullptr;
	QLabel* m_squelchValue = nullptr;
	QPushButton* m_undock = nullptr;
};

// Separate resizable window. Closing it hides the browser; it does not destroy the pane.
class FloatHost : public QWidget
{
public:
	explicit FloatHost(QWidget* parent)
		: QWidget(parent, Qt::Window)
	{
		setWindowTitle(QStringLiteral("Signal Browser"));
		setMinimumSize(340, 280);
		resize(460, 540);
		auto* column = new QVBoxLayout(this);
		column->setContentsMargins(0, 0, 0, 0);
		m_column = column;
	}

	void adopt(QWidget* pane) { m_column->addWidget(pane); }
	void release(QWidget* pane) { m_column->removeWidget(pane); }
	void setClosedHandler(std::function<void()> handler) { m_closed = std::move(handler); }

protected:
	void closeEvent(QCloseEvent* event) override
	{
		event->ignore();
		hide();
		if (m_closed)
			m_closed();
	}

private:
	QVBoxLayout* m_column = nullptr;
	std::function<void()> m_closed;
};

// Receive and transmit, with the browser on their left while it is docked.
// browserRatio is the browser's share of that row and is kept when the window resizes.
class TextRow : public QWidget
{
public:
	TextRow(BrowserPane* browser, QWidget* text, QWidget* floatParent)
		: QWidget(nullptr)
		, m_browser(browser)
		, m_text(text)
	{
		browser->setParent(this);
		text->setParent(this);
		m_line = new Line(this);
		m_line->hide();
		browser->hide();
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		QSettings settings;
		m_ratio = settings.value(QStringLiteral("browserRatio"), 0.5).toDouble();
		if (m_ratio < 0.18 || m_ratio > 0.75)
			m_ratio = 0.5;
		m_float = new FloatHost(floatParent);
		browser->setUndockHandler([this] { toggleDock(); });
		m_float->setClosedHandler([this] { onFloatClosed(); });
	}

	void setOpen(bool on)
	{
		if (m_open == on)
			return;
		m_open = on;
		if (!m_open) {
			m_float->hide();
			m_line->hide();
			if (m_browser->parentWidget() == this)
				m_browser->hide();
			apply();
			return;
		}
		if (m_docked)
			embed();
		else
			floatOut();
	}

	void setUserClosed(std::function<void()> handler) { m_userClosed = std::move(handler); }

protected:
	void resizeEvent(QResizeEvent*) override { apply(); }

private:
	static constexpr int kLine = 6;
	static constexpr int kMinBrowser = 320;
	static constexpr int kMinText = 280;

	class Line : public QWidget
	{
	public:
		explicit Line(TextRow* row)
			: QWidget(row)
			, m_row(row)
		{
			setFixedWidth(kLine);
			setCursor(Qt::SplitHCursor);
			setMouseTracking(true);
			setToolTip(QStringLiteral("Drag to resize the browser."));
		}

	protected:
		void paintEvent(QPaintEvent*) override
		{
			QPainter painter(this);
			painter.fillRect(rect(), palette().color(m_hot ? QPalette::Highlight : QPalette::Mid));
		}
		void enterEvent(QEnterEvent*) override
		{
			m_hot = true;
			update();
		}
		void leaveEvent(QEvent*) override
		{
			m_hot = false;
			update();
		}
		void mousePressEvent(QMouseEvent* event) override
		{
			if (event->button() == Qt::LeftButton)
				m_row->beginDrag(event->globalPosition().toPoint());
		}
		void mouseMoveEvent(QMouseEvent* event) override
		{
			if (event->buttons() & Qt::LeftButton)
				m_row->dragTo(event->globalPosition().toPoint());
		}
		void mouseReleaseEvent(QMouseEvent* event) override
		{
			if (event->button() == Qt::LeftButton)
				m_row->endDrag();
		}

	private:
		TextRow* m_row = nullptr;
		bool m_hot = false;
	};

	void toggleDock()
	{
		if (m_docked)
			floatOut();
		else
			embed();
	}

	void embed()
	{
		m_docked = true;
		m_open = true;
		m_float->release(m_browser);
		m_browser->setParent(this);
		m_browser->setFloating(false);
		m_browser->show();
		m_line->show();
		m_float->hide();
		apply();
	}

	void floatOut()
	{
		m_docked = false;
		m_open = true;
		m_line->hide();
		m_float->adopt(m_browser);
		m_browser->setFloating(true);
		m_browser->show();
		if (!m_placed) {
			const QRect frame = window()->frameGeometry();
			int x = frame.left() - m_float->width() - 16;
			if (x < 8)
				x = frame.left() + 32;
			m_float->move(x, frame.top() + 28);
			m_placed = true;
		}
		m_float->show();
		m_float->raise();
		apply();
	}

	void onFloatClosed()
	{
		if (!m_open)
			return;
		m_open = false;
		m_line->hide();
		apply();
		if (m_userClosed)
			m_userClosed();
	}

	void apply()
	{
		if (!m_open || !m_docked) {
			m_line->hide();
			if (m_browser->parentWidget() == this)
				m_browser->setGeometry(0, 0, 0, 0);
			m_text->setGeometry(0, 0, width(), height());
			return;
		}
		const int avail = qMax(1, width() - kLine);
		int browserW = qRound(m_ratio * avail);
		if (avail <= kMinBrowser + kMinText)
			browserW = avail / 2;
		else
			browserW = qBound(kMinBrowser, browserW, avail - kMinText);
		m_browser->setGeometry(0, 0, browserW, height());
		m_line->setGeometry(browserW, 0, kLine, height());
		m_line->show();
		m_line->raise();
		m_text->setGeometry(browserW + kLine, 0, avail - browserW, height());
	}

	void beginDrag(const QPoint& globalPos)
	{
		m_dragOffset = globalPos.x() - mapToGlobal(QPoint(m_line->x(), 0)).x();
		m_dragging = true;
	}

	void dragTo(const QPoint& globalPos)
	{
		if (!m_dragging)
			return;
		const int avail = qMax(1, width() - kLine);
		int browserW = mapFromGlobal(QPoint(globalPos.x() - m_dragOffset, 0)).x();
		if (avail <= kMinBrowser + kMinText)
			browserW = avail / 2;
		else
			browserW = qBound(kMinBrowser, browserW, avail - kMinText);
		m_ratio = double(browserW) / double(avail);
		apply();
	}

	void endDrag()
	{
		if (!m_dragging)
			return;
		m_dragging = false;
		QSettings settings;
		settings.setValue(QStringLiteral("browserRatio"), m_ratio);
	}

	BrowserPane* m_browser = nullptr;
	QWidget* m_text = nullptr;
	Line* m_line = nullptr;
	FloatHost* m_float = nullptr;
	std::function<void()> m_userClosed;
	double m_ratio = 0.5;
	bool m_open = false;
	bool m_docked = true;
	bool m_dragging = false;
	bool m_placed = false;
	int m_dragOffset = 0;
};

struct QrzAccount {
	QString user;
	QString password;
	QString logKey;
	QString myCall;
	QString myGrid;
	QString antenna;
};

QrzAccount qrzFromEngine()
{
	QrzAccount account;
	if (!engine_up())
		return account;
	char user[80];
	char password[80];
	char call[32];
	char grid[16];
	char antenna[80];
	engine_qrz_account(user, (int)sizeof user, password, (int)sizeof password,
		call, (int)sizeof call, grid, (int)sizeof grid,
		antenna, (int)sizeof antenna);
	account.user = QString::fromUtf8(user);
	account.password = QString::fromUtf8(password);
	account.myCall = QString::fromUtf8(call);
	account.myGrid = QString::fromUtf8(grid);
	account.antenna = QString::fromUtf8(antenna);
	return account;
}

QrzAccount qrzAccount()
{
	QSettings settings;
	QrzAccount account = qrzFromEngine();
	const QString user = settings.value(QStringLiteral("qrz/user")).toString().trimmed();
	const QString password = settings.value(QStringLiteral("qrz/password")).toString();
	const QString logKey = settings.value(QStringLiteral("qrz/xmlKey")).toString().trimmed();
	const QString myCall = settings.value(QStringLiteral("qrz/myCall")).toString().trimmed();
	const QString myGrid = settings.value(QStringLiteral("qrz/myGrid")).toString().trimmed();
	const QString antenna = settings.value(QStringLiteral("qrz/antenna")).toString().trimmed();
	if (!user.isEmpty())
		account.user = user;
	if (!password.isEmpty())
		account.password = password;
	account.logKey = logKey;
	if (!myCall.isEmpty())
		account.myCall = myCall;
	if (!myGrid.isEmpty())
		account.myGrid = myGrid;
	if (!antenna.isEmpty())
		account.antenna = antenna;
	return account;
}

void applyQrzAccount(QrzClient* client)
{
	const QrzAccount account = qrzAccount();
	client->setAccount(account.user, account.password, account.logKey);
}

void editTci(QWidget* parent)
{
	if (!engine_up()) {
		QMessageBox::information(parent, QStringLiteral("TCI"),
			QStringLiteral("The modem is not running yet."));
		return;
	}
	EngineTci current;
	engine_tci(&current);

	QDialog dialog(parent);
	dialog.setObjectName(QStringLiteral("tciDialog"));
	dialog.setWindowTitle(QStringLiteral("TCI"));
	dialog.setMinimumWidth(440);
	auto* form = new QFormLayout(&dialog);
	auto* about = new QLabel(QStringLiteral(
		"TCI talks to a Zeus or ExpertSDR3 radio. "
		"Turn it on for the dial, mode, and filter. "
		"Audio over TCI is used instead of the sound card."));
	about->setWordWrap(true);
	form->addRow(about);

	auto* enable = new QCheckBox(QStringLiteral("Enable TCI"));
	enable->setObjectName(QStringLiteral("tciEnable"));
	enable->setChecked(current.enable != 0);
	auto* audio = new QCheckBox(QStringLiteral("Use TCI for receive and transmit audio"));
	audio->setObjectName(QStringLiteral("tciAudio"));
	audio->setChecked(current.audio != 0);
	auto* host = new QLineEdit(QString::fromUtf8(current.host));
	host->setObjectName(QStringLiteral("tciHost"));
	host->setPlaceholderText(QStringLiteral("127.0.0.1"));
	auto* port = new QLineEdit(QString::fromUtf8(current.port));
	port->setObjectName(QStringLiteral("tciPort"));
	port->setPlaceholderText(QStringLiteral("40001"));
	auto* rx = new QSpinBox;
	rx->setObjectName(QStringLiteral("tciRx"));
	rx->setRange(0, 15);
	rx->setValue(current.rx < 0 ? 0 : current.rx);
	auto* status = new QLabel(QString::fromUtf8(current.status));
	status->setObjectName(QStringLiteral("tciStatus"));
	status->setWordWrap(true);

	form->addRow(enable);
	form->addRow(audio);
	form->addRow(QStringLiteral("Host"), host);
	form->addRow(QStringLiteral("Port"), port);
	form->addRow(QStringLiteral("Receiver"), rx);
	form->addRow(status);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close);
	form->addRow(buttons);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
		const QString hostText = host->text().trimmed();
		if (hostText.isEmpty()) {
			status->setText(QStringLiteral("Enter the TCI host, such as 127.0.0.1."));
			return;
		}
		bool portOk = false;
		const int portNum = port->text().trimmed().toInt(&portOk);
		if (!portOk || portNum < 1 || portNum > 65535) {
			status->setText(QStringLiteral("Enter a port from 1 to 65535."));
			return;
		}
		const QByteArray hostBytes = hostText.toUtf8();
		const QByteArray portBytes = QByteArray::number(portNum);
		engine_set_tci(enable->isChecked() ? 1 : 0, audio->isChecked() ? 1 : 0,
			hostBytes.constData(), portBytes.constData(), rx->value());
		dialog.accept();
	});
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	dialog.exec();
}

bool editQrzAccount(QWidget* parent)
{
	const QrzAccount current = qrzAccount();
	QDialog dialog(parent);
	dialog.setWindowTitle(QStringLiteral("QRZ account"));
	dialog.setMinimumWidth(420);
	auto* form = new QFormLayout(&dialog);
	auto* about = new QLabel(QStringLiteral(
		"Call sign lookup uses your QRZ username and password. "
		"The QRZ XML API key is on QRZ under My Logbook, Settings, QRZ Logbook API. "
		"Logging sends the contact only to that QRZ logbook. "
		"Call, grid, and antenna are in Configure, Station."));
	about->setWordWrap(true);
	form->addRow(about);

	auto* user = new QLineEdit(current.user);
	auto* password = new QLineEdit(current.password);
	password->setEchoMode(QLineEdit::Password);
	auto* logKey = new QLineEdit(current.logKey);
	logKey->setEchoMode(QLineEdit::PasswordEchoOnEdit);
	logKey->setPlaceholderText(QStringLiteral("Paste the key from QRZ"));
	logKey->setToolTip(QStringLiteral(
		"QRZ XML API key. On QRZ open My Logbook, then Settings, "
		"then QRZ Logbook API, and choose Show."));
	auto* result = new QLabel;
	result->setWordWrap(true);
	form->addRow(QStringLiteral("QRZ username"), user);
	form->addRow(QStringLiteral("QRZ password"), password);
	form->addRow(QStringLiteral("QRZ XML API key"), logKey);
	form->addRow(result);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close);
	auto* test = buttons->addButton(QStringLiteral("Test key"), QDialogButtonBox::ActionRole);
	form->addRow(buttons);

	auto* client = new QrzClient(&dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
		QSettings settings;
		settings.setValue(QStringLiteral("qrz/user"), user->text().trimmed());
		settings.setValue(QStringLiteral("qrz/password"), password->text());
		settings.setValue(QStringLiteral("qrz/xmlKey"), logKey->text().trimmed());
		dialog.accept();
	});
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	QObject::connect(test, &QPushButton::clicked, &dialog, [&] {
		client->setAccount(user->text(), password->text(), logKey->text());
		result->setText(QStringLiteral("Checking the logbook key…"));
		client->testLogbook();
	});
	QObject::connect(client, &QrzClient::tested, &dialog, [result](const QString& detail) {
		result->setText(detail);
	});
	QObject::connect(client, &QrzClient::failed, &dialog, [result](const QString& message) {
		result->setText(message);
	});
	return dialog.exec() == QDialog::Accepted;
}

QWidget* qrzLogRow(QWidget* parent, QrzClient* client, const std::function<void(const QString&)>& needAccount)
{
	auto* host = new QWidget(parent);
	host->setObjectName(QStringLiteral("qrzRow"));
	auto* row = new QHBoxLayout(host);
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(6);

	auto* call = new QLineEdit;
	call->setObjectName(QStringLiteral("qrzCall"));
	call->setPlaceholderText(QStringLiteral("Call"));
	call->setToolTip(QStringLiteral("Call sign to look up and log."));
	call->setMaxLength(20);
	call->setFixedWidth(110);
	call->setClearButtonEnabled(true);

	auto* lookup = stripButton(QStringLiteral("Lookup"),
		QStringLiteral("Look this call up on QRZ."));
	lookup->setObjectName(QStringLiteral("qrzLookup"));

	auto* name = new QLineEdit;
	name->setObjectName(QStringLiteral("qrzName"));
	name->setPlaceholderText(QStringLiteral("Name"));
	name->setToolTip(QStringLiteral("Name from QRZ. You can edit it before logging."));

	auto* qth = new QLineEdit;
	qth->setObjectName(QStringLiteral("qrzQth"));
	qth->setPlaceholderText(QStringLiteral("QTH"));
	qth->setToolTip(QStringLiteral("City and state from QRZ."));

	auto* grid = new QLineEdit;
	grid->setObjectName(QStringLiteral("qrzGrid"));
	grid->setPlaceholderText(QStringLiteral("Grid"));
	grid->setToolTip(QStringLiteral("Maidenhead grid from QRZ."));
	grid->setMaxLength(8);
	grid->setFixedWidth(84);

	auto* rstOut = new QLineEdit(QStringLiteral("599"));
	rstOut->setObjectName(QStringLiteral("qrzRstOut"));
	rstOut->setPlaceholderText(QStringLiteral("Sent"));
	rstOut->setToolTip(QStringLiteral("RST sent."));
	rstOut->setMaxLength(6);
	rstOut->setFixedWidth(58);

	auto* rstIn = new QLineEdit(QStringLiteral("599"));
	rstIn->setObjectName(QStringLiteral("qrzRstIn"));
	rstIn->setPlaceholderText(QStringLiteral("Rcvd"));
	rstIn->setToolTip(QStringLiteral("RST received."));
	rstIn->setMaxLength(6);
	rstIn->setFixedWidth(58);

	auto* log = stripButton(QStringLiteral("Log"),
		QStringLiteral("Upload this contact to the QRZ logbook."));
	log->setObjectName(QStringLiteral("qrzLog"));

	auto* result = new QLabel(QStringLiteral("QRZ"));
	result->setObjectName(QStringLiteral("qrzResult"));
	result->setMinimumWidth(160);
	result->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	result->setCursor(Qt::PointingHandCursor);
	result->setTextInteractionFlags(Qt::NoTextInteraction);

	row->addWidget(call);
	row->addWidget(lookup);
	row->addWidget(name, 1);
	row->addWidget(qth, 1);
	row->addWidget(grid);
	row->addWidget(rstOut);
	row->addWidget(rstIn);
	row->addWidget(log);
	row->addWidget(result, 1);

	// UTC of the lookup, plus the comment for this entry. The time is cleared
	// when the call is edited, so a later Log uses the time of the lookup
	// when one was made. The comment stays until the contact is logged.
	struct QrzStamp : QObject {
		QString date;
		QString time;
		QString comment;
		QString status;
		QString statusTip;
		std::function<void()> onDoubleClick;
		using QObject::QObject;
		bool eventFilter(QObject* watched, QEvent* event) override
		{
			if (event->type() == QEvent::MouseButtonDblClick && onDoubleClick) {
				const auto* mouse = static_cast<const QMouseEvent*>(event);
				if (mouse->button() == Qt::LeftButton) {
					onDoubleClick();
					return true;
				}
			}
			return QObject::eventFilter(watched, event);
		}
	};
	auto* stamp = new QrzStamp(host);
	stamp->status = QStringLiteral("QRZ");
	stamp->statusTip = QStringLiteral("QRZ lookup and logbook status.");

	auto paintResult = [=] {
		QString shown = stamp->status;
		if (!stamp->comment.trimmed().isEmpty())
			shown += QStringLiteral("  ·  comment");
		result->setText(shown);
		QString tip = stamp->statusTip;
		const QString note = stamp->comment.trimmed();
		if (!note.isEmpty()) {
			if (!tip.isEmpty())
				tip += QStringLiteral("\n\n");
			tip += QStringLiteral("Comment: ") + note;
		}
		if (!tip.isEmpty())
			tip += QLatin1Char('\n');
		tip += QStringLiteral("Double-click to enter comments for this log entry.");
		result->setToolTip(tip);
	};
	paintResult();

	auto editComment = [=] {
		QDialog dialog(host->window());
		dialog.setWindowTitle(QStringLiteral("Comments"));
		dialog.setObjectName(QStringLiteral("qrzCommentDialog"));
		dialog.setMinimumSize(420, 220);
		auto* layout = new QVBoxLayout(&dialog);
		auto* about = new QLabel(QStringLiteral(
			"These comments are sent with this contact when you log it to QRZ."));
		about->setWordWrap(true);
		auto* edit = new QPlainTextEdit(stamp->comment);
		edit->setObjectName(QStringLiteral("qrzComment"));
		edit->setPlaceholderText(QStringLiteral("Comments"));
		edit->setMinimumHeight(120);
		auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close);
		layout->addWidget(about);
		layout->addWidget(edit, 1);
		layout->addWidget(buttons);
		QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
			stamp->comment = edit->toPlainText();
			dialog.accept();
		});
		QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		edit->setFocus();
		if (dialog.exec() == QDialog::Accepted)
			paintResult();
	};
	stamp->onDoubleClick = editComment;
	result->installEventFilter(stamp);

	auto setBusy = [=](bool busy) {
		lookup->setEnabled(!busy);
		log->setEnabled(!busy);
		call->setReadOnly(busy);
	};
	QObject::connect(client, &QrzClient::busyChanged, host, setBusy);

	auto showWorked = [=](bool worked) {
		call->setProperty("worked", worked);
		call->style()->unpolish(call);
		call->style()->polish(call);
		call->update();
		call->setToolTip(worked
			? QStringLiteral("Worked this station before.")
			: QStringLiteral("Call sign to look up and log."));
	};
	auto doLookup = [=] {
		applyQrzAccount(client);
		if (!client->hasLogin()) {
			needAccount(QStringLiteral("QRZ username and password"));
			return;
		}
		showWorked(false);
		stamp->status = QStringLiteral("Looking up…");
		paintResult();
		client->lookup(call->text());
	};
	QObject::connect(lookup, &QPushButton::clicked, host, doLookup);
	QObject::connect(call, &QLineEdit::returnPressed, host, doLookup);
	QObject::connect(call, &QLineEdit::textEdited, host, [=] {
		stamp->date.clear();
		stamp->time.clear();
		showWorked(false);
		if (call->text().trimmed().isEmpty() && !stamp->comment.isEmpty()) {
			stamp->comment.clear();
			paintResult();
		}
	});

	QObject::connect(client, &QrzClient::lookupReady, host, [=](const QrzStation& station) {
		call->setText(station.call);
		name->setText(station.name);
		qth->setText(station.qth);
		grid->setText(station.grid);
		const QDateTime now = QDateTime::currentDateTimeUtc();
		stamp->date = now.toString(QStringLiteral("yyyyMMdd"));
		stamp->time = now.toString(QStringLiteral("HHmmss"));
		QString text = station.call;
		if (!station.name.isEmpty())
			text += QStringLiteral("  ") + station.name;
		if (!station.qth.isEmpty())
			text += QStringLiteral(", ") + station.qth;
		stamp->status = text;
		stamp->statusTip = station.country.isEmpty() ? text : station.country;
		paintResult();
	});
	QObject::connect(client, &QrzClient::workedBefore, host, [=](const QString& workedCall, bool worked) {
		if (call->text().trimmed().compare(workedCall, Qt::CaseInsensitive) != 0)
			return;
		showWorked(worked);
		if (!worked)
			return;
		const QString mark = QStringLiteral("worked before");
		if (!stamp->status.contains(mark))
			stamp->status += QStringLiteral("  ·  ") + mark;
		stamp->statusTip = QStringLiteral("This station is already in the QRZ logbook.");
		paintResult();
	});
	QObject::connect(client, &QrzClient::logged, host, [=](const QString& logId) {
		stamp->comment.clear();
		stamp->status = logId.isEmpty()
			? QStringLiteral("Logged to QRZ.")
			: QStringLiteral("Logged to QRZ. ") + logId;
		stamp->statusTip = stamp->status;
		paintResult();
		call->clear();
		name->clear();
		qth->clear();
		grid->clear();
		stamp->date.clear();
		stamp->time.clear();
		showWorked(false);
	});
	QObject::connect(client, &QrzClient::failed, host, [=](const QString& message) {
		stamp->status = message;
		stamp->statusTip = message;
		paintResult();
	});

	QObject::connect(log, &QPushButton::clicked, host, [=] {
		applyQrzAccount(client);
		if (!client->hasLogKey()) {
			needAccount(QStringLiteral("QRZ logbook key"));
			return;
		}
		const QrzAccount account = qrzAccount();
		EngineView view{};
		if (engine_up())
			engine_view(&view);
		QrzQso qso;
		qso.call = call->text();
		qso.name = name->text();
		qso.qth = qth->text();
		qso.grid = grid->text();
		qso.rstSent = rstOut->text();
		qso.rstRcvd = rstIn->text();
		qso.modem = QString::fromUtf8(view.modem);
		qso.rigMode = QString::fromUtf8(view.rig_mode);
		qso.dialHz = view.frequency_hz;
		qso.carrierHz = view.carrier_hz;
		qso.reverse = view.reverse != 0;
		qso.myCall = account.myCall;
		qso.myGrid = account.myGrid;
		qso.dateOn = stamp->date;
		qso.timeOn = stamp->time;
		qso.comment = stamp->comment;
		stamp->status = QStringLiteral("Logging…");
		paintResult();
		client->insert(qso);
	});

	auto* actions = new QrzRowActions(host);
	actions->setObjectName(QStringLiteral("qrzActions"));
	actions->setCall = [=](const QString& text) {
		const QString next = text.trimmed().toUpper();
		if (next.isEmpty() || call->text() == next)
			return;
		call->setText(next);
		name->clear();
		qth->clear();
		grid->clear();
		stamp->date.clear();
		stamp->time.clear();
		stamp->comment.clear();
		showWorked(false);
		stamp->status = QStringLiteral("QRZ");
		stamp->statusTip = QStringLiteral("QRZ lookup and logbook status.");
		paintResult();
	};

	return host;
}
}

// Physical Control. Qt calls that key Meta on macOS and Control elsewhere.
bool controlKey(const QKeyEvent* key)
{
	if (!key || key->isAutoRepeat())
		return false;
	const Qt::KeyboardModifiers mods = key->modifiers()
		& (Qt::ShiftModifier | Qt::AltModifier | Qt::ControlModifier | Qt::MetaModifier);
#if defined(Q_OS_MACOS)
	return mods == Qt::MetaModifier;
#else
	return mods == Qt::ControlModifier;
#endif
}

class TxKeyFilter : public QObject
{
public:
	QObject* box = nullptr;
	QObject* viewport = nullptr;
	QWidget* window = nullptr;
	std::function<void()> transmit;
	std::function<void()> receive;
	// True when transmit or tune was stopped.
	std::function<bool()> abortTransmit;
	// F1 through F12 run the twelve macros in the bank on screen, left to right.
	std::function<void(int)> functionMacro;
	qint64 escapeMs = 0;
	using QObject::QObject;

	bool inTransmitBox(QObject* watched) const
	{
		return watched == box || (viewport && watched == viewport);
	}

	bool handleEscape(QObject* watched, QKeyEvent* key)
	{
		auto* widget = qobject_cast<QWidget*>(watched);
		if (!widget || !window || widget->window() != window)
			return false;
		if (key->isAutoRepeat())
			return false;
		const Qt::KeyboardModifiers mods = key->modifiers()
			& (Qt::ShiftModifier | Qt::AltModifier | Qt::ControlModifier | Qt::MetaModifier);
		if (mods != Qt::NoModifier)
			return false;
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		const bool twice = escapeMs != 0 && (now - escapeMs) <= 1000;
		escapeMs = twice ? 0 : now;
		if (!twice || !abortTransmit)
			return false;
		return abortTransmit();
	}

	bool handleFunction(QObject* watched, QKeyEvent* key)
	{
		auto* widget = qobject_cast<QWidget*>(watched);
		if (!widget || !window || widget->window() != window)
			return false;
		const int code = key->key();
		if (code < Qt::Key_F1 || code > Qt::Key_F12)
			return false;
		const Qt::KeyboardModifiers mods = key->modifiers()
			& (Qt::ShiftModifier | Qt::AltModifier | Qt::ControlModifier | Qt::MetaModifier);
		if (mods != Qt::NoModifier)
			return false;
		// A held key would start the macro again. One press runs it once.
		if (key->isAutoRepeat())
			return true;
		if (functionMacro)
			functionMacro(code - Qt::Key_F1);
		return true;
	}

	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (event->type() != QEvent::KeyPress)
			return false;
		auto* key = static_cast<QKeyEvent*>(event);
		if (key->key() == Qt::Key_Escape)
			return handleEscape(watched, key);
		if (handleFunction(watched, key))
			return true;
		if (!inTransmitBox(watched) || !controlKey(key))
			return false;
		if (key->key() == Qt::Key_T) {
			if (transmit)
				transmit();
			return true;
		}
		if (key->key() == Qt::Key_R) {
			if (receive)
				receive();
			return true;
		}
		return false;
	}
};

void pushOperator()
{
	if (!engine_up())
		return;
	QSettings settings;
	EngineStation station;
	engine_station(&station);
	auto apply = [&settings](const char* key, char* dst, int cap, bool upper) {
		if (!settings.contains(QString::fromLatin1(key)))
			return;
		QString text = settings.value(QString::fromLatin1(key)).toString().trimmed();
		if (upper)
			text = text.toUpper();
		std::snprintf(dst, (size_t)cap, "%s", text.toUtf8().constData());
	};
	apply("qrz/myCall", station.call, (int)sizeof station.call, true);
	apply("station/operCall", station.oper_call, (int)sizeof station.oper_call, true);
	apply("station/name", station.name, (int)sizeof station.name, false);
	apply("station/qth", station.qth, (int)sizeof station.qth, false);
	apply("qrz/myGrid", station.grid, (int)sizeof station.grid, true);
	apply("qrz/antenna", station.antenna, (int)sizeof station.antenna, false);
	apply("pskrep/host", station.psk_host, (int)sizeof station.psk_host, false);
	apply("pskrep/port", station.psk_port, (int)sizeof station.psk_port, false);
	if (settings.contains(QStringLiteral("pskrep/qrg")))
		station.psk_qrg = settings.value(QStringLiteral("pskrep/qrg")).toBool() ? 1 : 0;
	else
		station.psk_qrg = 1;
	engine_set_station(&station);
}

bool maidenhead(const QString& grid)
{
	static const QRegularExpression pattern(
		QStringLiteral("^[A-R]{2}[0-9]{2}[A-X]{2}$"));
	return pattern.match(grid.trimmed().toUpper()).hasMatch();
}

void editStation(QWidget* parent)
{
	if (!engine_up()) {
		QMessageBox::information(parent, QStringLiteral("Station"),
			QStringLiteral("The modem is not running yet."));
		return;
	}
	EngineStation current;
	engine_station(&current);
	QSettings settings;
	auto prefer = [&settings](const char* key, const char* engine, bool upper) {
		if (!settings.contains(QString::fromLatin1(key)))
			return QString::fromUtf8(engine);
		QString text = settings.value(QString::fromLatin1(key)).toString().trimmed();
		return upper ? text.toUpper() : text;
	};

	QDialog dialog(parent);
	dialog.setObjectName(QStringLiteral("stationDialog"));
	dialog.setWindowTitle(QStringLiteral("Station"));
	dialog.setMinimumWidth(480);
	auto* form = new QFormLayout(&dialog);
	auto* about = new QLabel(QStringLiteral(
		"Operator and station text fill macros such as <MYNAME>, <MYQTH>, <MYCALL>, and <MYLOC>. "
		"PSK Reporter uses the station call, a 6-character grid, and a short antenna. "
		"The Spot button starts and stops the reports."));
	about->setWordWrap(true);
	form->addRow(about);

	auto* name = new QLineEdit(prefer("station/name", current.name, false));
	name->setObjectName(QStringLiteral("stationName"));
	auto* oper = new QLineEdit(prefer("station/operCall", current.oper_call, true));
	oper->setObjectName(QStringLiteral("stationOper"));
	oper->setPlaceholderText(QStringLiteral("Leave empty when it matches the station call"));
	auto* qth = new QLineEdit(prefer("station/qth", current.qth, false));
	qth->setObjectName(QStringLiteral("stationQth"));
	auto* call = new QLineEdit(prefer("qrz/myCall", current.call, true));
	call->setObjectName(QStringLiteral("stationCall"));
	auto* grid = new QLineEdit(prefer("qrz/myGrid", current.grid, true));
	grid->setObjectName(QStringLiteral("stationGrid"));
	grid->setPlaceholderText(QStringLiteral("FN43cb"));
	auto* antenna = new QLineEdit(prefer("qrz/antenna", current.antenna, false));
	antenna->setObjectName(QStringLiteral("stationAntenna"));
	antenna->setPlaceholderText(QStringLiteral("dipole, vertical, EFHW"));
	auto* host = new QLineEdit(prefer("pskrep/host", current.psk_host, false));
	host->setObjectName(QStringLiteral("pskHost"));
	host->setPlaceholderText(QStringLiteral("report.pskreporter.info"));
	auto* port = new QLineEdit(prefer("pskrep/port", current.psk_port, false));
	port->setObjectName(QStringLiteral("pskPort"));
	port->setPlaceholderText(QStringLiteral("4739"));
	auto* qrg = new QCheckBox(QStringLiteral("Include the dial frequency in each report"));
	qrg->setObjectName(QStringLiteral("pskQrg"));
	if (settings.contains(QStringLiteral("pskrep/qrg")))
		qrg->setChecked(settings.value(QStringLiteral("pskrep/qrg")).toBool());
	else
		qrg->setChecked(true);
	auto* status = new QLabel;
	status->setObjectName(QStringLiteral("stationStatus"));
	status->setWordWrap(true);

	auto* operatorLabel = new QLabel(QStringLiteral("Operator"));
	operatorLabel->setObjectName(QStringLiteral("sectionLabel"));
	form->addRow(operatorLabel);
	form->addRow(QStringLiteral("Name"), name);
	form->addRow(QStringLiteral("Call, if different"), oper);
	form->addRow(QStringLiteral("QTH"), qth);
	auto* stationLabel = new QLabel(QStringLiteral("Station"));
	stationLabel->setObjectName(QStringLiteral("sectionLabel"));
	form->addRow(stationLabel);
	form->addRow(QStringLiteral("Call"), call);
	form->addRow(QStringLiteral("Grid"), grid);
	form->addRow(QStringLiteral("Antenna"), antenna);
	auto* reporterLabel = new QLabel(QStringLiteral("PSK Reporter"));
	reporterLabel->setObjectName(QStringLiteral("sectionLabel"));
	form->addRow(reporterLabel);
	form->addRow(QStringLiteral("Host"), host);
	form->addRow(QStringLiteral("Port"), port);
	form->addRow(qrg);
	form->addRow(status);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close);
	form->addRow(buttons);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
		const QString gridText = grid->text().trimmed().toUpper();
		if (!gridText.isEmpty() && !maidenhead(gridText)) {
			status->setText(QStringLiteral(
				"Enter a 6-character grid such as FN43cb, or leave the grid empty."));
			return;
		}
		const QString hostText = host->text().trimmed();
		if (hostText.isEmpty()) {
			status->setText(QStringLiteral("Enter the PSK Reporter host."));
			return;
		}
		bool portOk = false;
		const int portNum = port->text().trimmed().toInt(&portOk);
		if (!portOk || portNum < 1 || portNum > 65535) {
			status->setText(QStringLiteral("Enter a PSK Reporter port from 1 to 65535."));
			return;
		}
		QSettings saved;
		saved.setValue(QStringLiteral("station/name"), name->text().trimmed());
		saved.setValue(QStringLiteral("station/operCall"), oper->text().trimmed().toUpper());
		saved.setValue(QStringLiteral("station/qth"), qth->text().trimmed());
		saved.setValue(QStringLiteral("qrz/myCall"), call->text().trimmed().toUpper());
		saved.setValue(QStringLiteral("qrz/myGrid"), gridText);
		saved.setValue(QStringLiteral("qrz/antenna"), antenna->text().trimmed());
		saved.setValue(QStringLiteral("pskrep/host"), hostText);
		saved.setValue(QStringLiteral("pskrep/port"), QString::number(portNum));
		saved.setValue(QStringLiteral("pskrep/qrg"), qrg->isChecked());
		pushOperator();
		dialog.accept();
	});
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	dialog.exec();
}

void pushQso(QWidget* root)
{
	if (!engine_up() || !root)
		return;
	auto field = [root](const char* name) {
		if (auto* edit = root->findChild<QLineEdit*>(QString::fromLatin1(name)))
			return edit->text().toUtf8();
		return QByteArray();
	};
	const QByteArray call = field("qrzCall");
	const QByteArray name = field("qrzName");
	const QByteArray qth = field("qrzQth");
	const QByteArray grid = field("qrzGrid");
	const QByteArray sent = field("qrzRstOut");
	const QByteArray rcvd = field("qrzRstIn");
	engine_set_qso(call.constData(), name.constData(), qth.constData(),
		grid.constData(), sent.constData(), rcvd.constData());
}

void editQtMacro(QWidget* parent, int index)
{
	if (!engine_up()) {
		QMessageBox::information(parent, QStringLiteral("Macro"),
			QStringLiteral("The modem is not running yet."));
		return;
	}
	if (index < 0 || index >= 48)
		return;
	char name[128];
	char body[65536];
	name[0] = 0;
	body[0] = 0;
	engine_macro_name(index, name, (int)sizeof name);
	engine_macro_text(index, body, (int)sizeof body);

	QDialog dialog(parent);
	dialog.setObjectName(QStringLiteral("macroEditDialog"));
	dialog.setWindowTitle(QStringLiteral("Edit macro"));
	dialog.setMinimumSize(520, 360);
	auto* layout = new QVBoxLayout(&dialog);
	auto* about = new QLabel(QStringLiteral(
		"Set %1, button %2. Tags such as <MYCALL>, <CALL>, <NAME>, <QTH>, <LOC>, "
		"<RST>, <MYLOC>, <TX>, and <RX> are filled in when the macro runs.")
		.arg(index / 12 + 1)
		.arg(index % 12 + 1));
	about->setWordWrap(true);
	auto* nameEdit = new QLineEdit(QString::fromUtf8(name));
	nameEdit->setObjectName(QStringLiteral("macroEditName"));
	nameEdit->setPlaceholderText(QStringLiteral("Button label"));
	auto* textEdit = new QPlainTextEdit(QString::fromUtf8(body));
	textEdit->setObjectName(QStringLiteral("macroEditText"));
	textEdit->setPlaceholderText(QStringLiteral("Macro text"));
	textEdit->setMinimumHeight(160);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close);
	layout->addWidget(about);
	layout->addWidget(nameEdit);
	layout->addWidget(textEdit, 1);
	layout->addWidget(buttons);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
		const QByteArray label = nameEdit->text().toUtf8();
		const QByteArray text = textEdit->toPlainText().toUtf8();
		engine_macro_set(index, label.constData(), text.constData());
		// accept() destroys the Save button inside its own mouse release.
		// Closing on the next turn lets Qt drop the mouse grab first.
		QTimer::singleShot(0, &dialog, [&dialog] { dialog.accept(); });
	});
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	nameEdit->setFocus();
	dialog.exec();
}

class FreqReadout : public QWidget
{
public:
	explicit FreqReadout(QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setObjectName(QStringLiteral("freqReadout"));
		setAttribute(Qt::WA_StyledBackground, true);
		setMouseTracking(true);
		setFocusPolicy(Qt::NoFocus);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		setMinimumSize(260, 48);
		QFont readout = QFontDatabase::systemFont(QFontDatabase::FixedFont);
		readout.setPixelSize(28);
		readout.setWeight(QFont::DemiBold);
		setFont(readout);
		setToolTip(QStringLiteral(
			"Scroll a digit to tune. Double-click to enter a frequency."));
		setAccessibleDescription(readoutText());
	}

	void setHz(long long hz)
	{
		if (hz < 0)
			hz = 0;
		if (hz > kMaxHz)
			hz = kMaxHz;
		if (hz == m_hz)
			return;
		m_hz = hz;
		setAccessibleDescription(readoutText());
		update();
	}

	void setCommitHandler(std::function<void(long long)> commit)
	{
		m_commit = std::move(commit);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QStyleOption opt;
		opt.initFrom(this);
		QPainter painter(this);
		style()->drawPrimitive(QStyle::PE_Widget, &opt, &painter, this);
		const QString text = readoutText();
		const QFontMetrics fm(font());
		const QRect box = contentsRect();
		const int width = fm.horizontalAdvance(text);
		const int x0 = box.left() + (box.width() - width) / 2;
		const int baseline = box.top() + (box.height() + fm.ascent() - fm.descent()) / 2;
		const bool dark = palette().color(QPalette::Window).lightness() < 128;
		const QColor plain = dark ? QColor(142, 202, 255) : QColor(10, 102, 255);
		const QColor hot = dark ? QColor(210, 232, 255) : QColor(0, 62, 203);
		painter.setFont(font());
		int x = x0;
		for (int i = 0; i < text.size(); ++i) {
			const int w = fm.horizontalAdvance(text.at(i));
			if (i == m_hover) {
				QColor wash = hot;
				wash.setAlpha(48);
				painter.fillRect(QRect(x, box.top() + 2, w, qMax(1, box.height() - 4)), wash);
				painter.setPen(hot);
			} else {
				painter.setPen(plain);
			}
			painter.drawText(x, baseline, QString(text.at(i)));
			x += w;
		}
	}

	void mouseMoveEvent(QMouseEvent* event) override
	{
		const int index = digitAt(event->position().toPoint());
		setCursor(index >= 0 ? Qt::SizeVerCursor : Qt::ArrowCursor);
		if (index == m_hover)
			return;
		m_hover = index;
		update();
	}

	void leaveEvent(QEvent*) override
	{
		unsetCursor();
		if (m_hover < 0)
			return;
		m_hover = -1;
		update();
	}

	void wheelEvent(QWheelEvent* event) override
	{
		const int index = digitAt(event->position().toPoint());
		const long long place = placeHz(readoutText(), index);
		int steps = event->angleDelta().y() / 120;
		if (steps == 0 && event->angleDelta().y() != 0)
			steps = event->angleDelta().y() > 0 ? 1 : -1;
		if (steps == 0 && event->pixelDelta().y() != 0)
			steps = event->pixelDelta().y() > 0 ? 1 : -1;
		if (index < 0 || place <= 0 || steps == 0) {
			event->ignore();
			return;
		}
		if (steps > 8)
			steps = 8;
		if (steps < -8)
			steps = -8;
		long long next = m_hz + (long long)steps * place;
		if (next < 0)
			next = 0;
		if (next > kMaxHz)
			next = kMaxHz;
		event->accept();
		if (next == m_hz)
			return;
		setHz(next);
		if (m_commit)
			m_commit(next);
	}

	void mouseDoubleClickEvent(QMouseEvent* event) override
	{
		if (event->button() != Qt::LeftButton)
			return;
		QDialog dialog(window());
		dialog.setWindowTitle(QStringLiteral("Frequency"));
		dialog.setMinimumWidth(320);
		auto* layout = new QVBoxLayout(&dialog);
		auto* about = new QLabel(QStringLiteral("Dial frequency in kHz."));
		about->setWordWrap(true);
		auto* edit = new QLineEdit(QString::number(m_hz / 1000.0, 'f', 3));
		edit->setObjectName(QStringLiteral("freqEntry"));
		edit->selectAll();
		auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close);
		layout->addWidget(about);
		layout->addWidget(edit);
		layout->addWidget(buttons);
		long long parsed = m_hz;
		QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
			bool ok = false;
			const QString raw = edit->text().trimmed().remove(QLatin1Char(' ')).remove(QLatin1Char(','));
			const double khz = raw.toDouble(&ok);
			if (!ok || khz < 0.0 || khz > 10000000.0) {
				about->setText(QStringLiteral(
					"Enter the dial frequency in kHz, such as 14070.000."));
				return;
			}
			parsed = (long long)std::llround(khz * 1000.0);
			dialog.accept();
		});
		QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		edit->setFocus();
		if (dialog.exec() != QDialog::Accepted)
			return;
		setHz(parsed);
		if (m_commit)
			m_commit(m_hz);
	}

private:
	static constexpr long long kMaxHz = 10000000000LL;

	QString readoutText() const
	{
		return QString::asprintf("%09.3f", m_hz / 1000.0);
	}

	int digitAt(const QPoint& pos) const
	{
		const QString text = readoutText();
		const QFontMetrics fm(font());
		const QRect box = contentsRect();
		const int width = fm.horizontalAdvance(text);
		const int x0 = box.left() + (box.width() - width) / 2;
		const int index = textIndexAt(fm, text, pos.x() - x0);
		if (index < 0 || index >= text.size() || !text.at(index).isDigit())
			return -1;
		return index;
	}

	static long long placeHz(const QString& text, int index)
	{
		const int dot = text.indexOf(QLatin1Char('.'));
		if (dot < 0 || index < 0 || index >= text.size() || !text.at(index).isDigit())
			return 0;
		const int exp = index < dot
			? (dot - index - 1) + 3
			: 2 - (index - dot - 1);
		if (exp < 0)
			return 0;
		long long place = 1;
		for (int i = 0; i < exp; ++i)
			place *= 10;
		return place;
	}

	long long m_hz = 0;
	int m_hover = -1;
	std::function<void(long long)> m_commit;
};

class RxCallFilter : public QObject
{
public:
	std::function<void(const QString&)> onCall;
	using QObject::QObject;

	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (!onCall)
			return QObject::eventFilter(watched, event);
		auto* mouse = dynamic_cast<QMouseEvent*>(event);
		if (event->type() == QEvent::MouseButtonPress && mouse
			&& mouse->button() == Qt::LeftButton) {
			m_down = true;
			m_press = mouse->position().toPoint();
			return QObject::eventFilter(watched, event);
		}
		if (event->type() != QEvent::MouseButtonRelease || !mouse
			|| mouse->button() != Qt::LeftButton)
			return QObject::eventFilter(watched, event);
		if (!m_down)
			return QObject::eventFilter(watched, event);
		m_down = false;
		auto* edit = qobject_cast<QPlainTextEdit*>(watched->parent());
		if (!edit)
			return QObject::eventFilter(watched, event);
		const QPoint pos = mouse->position().toPoint();
		if ((pos - m_press).manhattanLength() > 4)
			return QObject::eventFilter(watched, event);
		const int cursor = edit->cursorForPosition(pos).position();
		const QString call = callAtText(edit->toPlainText(), cursor);
		if (!call.isEmpty())
			onCall(call);
		return QObject::eventFilter(watched, event);
	}

private:
	QPoint m_press;
	bool m_down = false;
};

// Categories stay on the button. Hover or click opens that list, and each
// category flies out to its modes (BPSK-31, BPSK-63, and the rest).
class ModemMenu : public QToolButton
{
public:
	struct Entry {
		QString category;
		QString id;
		QString label;
		QAction* action = nullptr;
	};

	explicit ModemMenu(QWidget* parent = nullptr)
		: QToolButton(parent)
	{
		setObjectName(QStringLiteral("modemList"));
		setPopupMode(QToolButton::InstantPopup);
		setToolButtonStyle(Qt::ToolButtonTextOnly);
		setFocusPolicy(Qt::NoFocus);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		setMinimumWidth(128);
		m_group = new QActionGroup(this);
		m_group->setExclusive(true);
		m_hover.setSingleShot(true);
		m_hover.setInterval(400);
		connect(&m_hover, &QTimer::timeout, this, [this] {
			if (!underMouse() || !menu() || menu()->isVisible())
				return;
			showMenu();
		});
		rebuild();
	}

	std::function<void(const QString& id, const QString& label)> onSelect;

	QString currentId() const { return m_id; }
	QString currentLabel() const { return m_label; }
	QString widestLabel() const { return m_widest; }
	bool menuOpen() const
	{
		const QMenu* root = menu();
		if (!root)
			return false;
		if (root->isVisible())
			return true;
		const QList<QMenu*> subs = root->findChildren<QMenu*>();
		for (const QMenu* sub : subs) {
			if (sub->isVisible())
				return true;
		}
		return false;
	}

	void setCurrentId(const QString& id)
	{
		if (id.isEmpty())
			return;
		if (!contains(id))
			addLoose(id);
		applyCurrent(id, false);
	}

	void advance()
	{
		if (m_entries.isEmpty())
			return;
		int index = 0;
		for (int i = 0; i < m_entries.size(); ++i) {
			if (m_entries.at(i).id == m_id) {
				index = (i + 1) % m_entries.size();
				break;
			}
		}
		applyCurrent(m_entries.at(index).id, true);
	}

protected:
	void enterEvent(QEnterEvent* event) override
	{
		QToolButton::enterEvent(event);
		if (!m_hover.isActive())
			m_hover.start();
	}

	void leaveEvent(QEvent* event) override
	{
		QToolButton::leaveEvent(event);
		m_hover.stop();
	}

private:
	void rebuild()
	{
		m_entries.clear();
		m_widest.clear();
		if (QMenu* old = menu()) {
			setMenu(nullptr);
			old->deleteLater();
		}
		auto* root = new QMenu(this);
		setMenu(root);

		const char* titles[] = {
			"BPSK", "Contestia", "CW", "DominoEX", "Hell", "MFSK",
			"MT63", "Olivia", "QPSK", "RTTY", "Thor",
		};
		const int count = engine_modem_count();
		for (const char* title : titles) {
			QMenu* sub = nullptr;
			const QString category = QString::fromUtf8(title);
			for (int i = 0; i < count; ++i) {
				char group[32];
				char id[64];
				char label[64];
				if (!engine_modem_group(i, group, (int)sizeof group))
					continue;
				if (category != QString::fromUtf8(group))
					continue;
				if (!engine_modem_name(i, id, (int)sizeof id))
					continue;
				if (!engine_modem_label(i, label, (int)sizeof label))
					continue;
				if (!sub)
					sub = root->addMenu(category);
				const QString ident = QString::fromUtf8(id);
				const QString shown = QString::fromUtf8(label);
				addEntry(sub, category, ident, shown.isEmpty() ? ident : shown);
			}
		}
		if (!m_id.isEmpty())
			applyCurrent(m_id, false);
		else if (!m_entries.isEmpty())
			applyCurrent(m_entries.at(0).id, false);
	}

	void addEntry(QMenu* menu, const QString& category, const QString& id, const QString& label)
	{
		auto* action = menu->addAction(label);
		action->setCheckable(true);
		m_group->addAction(action);
		Entry entry;
		entry.category = category;
		entry.id = id;
		entry.label = label;
		entry.action = action;
		m_entries.push_back(entry);
		if (label.size() > m_widest.size())
			m_widest = label;
		connect(action, &QAction::triggered, this, [this, id] {
			if (!m_applying)
				applyCurrent(id, true);
		});
	}

	void addLoose(const QString& id)
	{
		QMenu* root = menu();
		if (!root)
			return;
		QMenu* other = nullptr;
		const QList<QAction*> actions = root->actions();
		for (QAction* action : actions) {
			if (action->menu() && action->text() == QLatin1String("Other"))
				other = action->menu();
		}
		if (!other)
			other = root->addMenu(QStringLiteral("Other"));
		addEntry(other, QStringLiteral("Other"), id, id);
	}

	bool contains(const QString& id) const
	{
		for (const Entry& entry : m_entries) {
			if (entry.id == id)
				return true;
		}
		return false;
	}

	void applyCurrent(const QString& id, bool notify)
	{
		const Entry* found = nullptr;
		for (const Entry& entry : m_entries) {
			if (entry.id == id)
				found = &entry;
		}
		if (!found)
			return;
		m_id = found->id;
		m_label = found->label;
		m_applying = true;
		if (found->action)
			found->action->setChecked(true);
		m_applying = false;
		setText(found->category);
		setToolTip(found->label + QStringLiteral(
			"\nHover or click for modes in this group."));
		if (notify && onSelect)
			onSelect(found->id, found->label);
	}

	QActionGroup* m_group = nullptr;
	QTimer m_hover;
	QVector<Entry> m_entries;
	QString m_id;
	QString m_label;
	QString m_widest;
	bool m_applying = false;
};

// The scroll area takes the right-click before the viewport can show its own
// menu, so the standard menu was Copy and Paste with no Clear. This filter
// runs first and adds Clear.
class TextClearFilter : public QObject
{
public:
	TextClearFilter(QPlainTextEdit* edit, std::function<void()> clearBuffer, QObject* parent)
		: QObject(parent)
		, m_edit(edit)
		, m_clear(std::move(clearBuffer))
	{
	}

	bool eventFilter(QObject*, QEvent* event) override
	{
		if (event->type() != QEvent::ContextMenu || !m_edit)
			return false;
		const auto* menuEvent = static_cast<QContextMenuEvent*>(event);
		const QPoint pos = m_edit->viewport()->mapFromGlobal(menuEvent->globalPos());
		QMenu* menu = m_edit->createStandardContextMenu(pos);
		menu->addSeparator();
		QAction* clear = menu->addAction(QStringLiteral("Clear"));
		QObject::connect(clear, &QAction::triggered, m_edit, [this] {
			if (m_clear)
				m_clear();
			m_edit->moveCursor(QTextCursor::Start);
			m_edit->verticalScrollBar()->setValue(0);
			m_edit->horizontalScrollBar()->setValue(0);
		});
		menu->exec(menuEvent->globalPos());
		delete menu;
		return true;
	}

private:
	QPlainTextEdit* m_edit = nullptr;
	std::function<void()> m_clear;
};

static void installClearMenu(QPlainTextEdit* edit, std::function<void()> clearBuffer)
{
	edit->viewport()->installEventFilter(
		new TextClearFilter(edit, std::move(clearBuffer), edit));
}

void MainWindow::showEngineTxText()
{
	if (!m_tx || !engine_up())
		return;
	char tx[65536];
	engine_tx_text(tx, (int)sizeof tx);
	const QString next = QString::fromUtf8(tx);
	if (next == m_txSeen && m_tx->toPlainText() == next)
		return;
	const bool focused = m_tx->hasFocus();
	const bool applying = m_applying;
	m_applying = true;
	if (m_tx->toPlainText() != next) {
		m_tx->setPlainText(next);
		if (focused && !next.isEmpty()) {
			QTextCursor cursor = m_tx->textCursor();
			cursor.movePosition(QTextCursor::End);
			m_tx->setTextCursor(cursor);
		}
	}
	m_txSeen = next;
	m_applying = applying;
}

MainWindow::MainWindow(Theme* theme, QWidget* parent)
	: QMainWindow(parent)
	, m_theme(theme)
{
	setWindowTitle(QStringLiteral("TCIdigi"));
	resize(1180, 820);
	setMinimumSize(900, 620);

	auto* central = new QWidget(this);
	auto* root = new QVBoxLayout(central);
	root->setContentsMargins(12, 10, 12, 10);
	root->setSpacing(8);

	m_freq = new FreqReadout;
	m_freq->setCommitHandler([this](long long hz) {
		if (!engine_up()) {
			notConnected(QStringLiteral("VFO"));
			return;
		}
		engine_set_frequency(hz);
	});
	if (engine_up()) {
		EngineView opened;
		engine_view(&opened);
		m_freq->setHz(opened.frequency_hz);
	}

	// Same lists fldigi shows under the VFO for a TCI radio.
	auto* rigMode = new QComboBox;
	rigMode->setObjectName(QStringLiteral("rigMode"));
	rigMode->addItems({
		QStringLiteral("USB"), QStringLiteral("LSB"), QStringLiteral("DIGU"),
		QStringLiteral("DIGL"), QStringLiteral("CWU"), QStringLiteral("CWL"),
		QStringLiteral("AM"), QStringLiteral("SAM"), QStringLiteral("FM"),
		QStringLiteral("NFM"), QStringLiteral("DSB")
	});
	rigMode->setCurrentText(QStringLiteral("DIGU"));
	rigMode->setToolTip(QStringLiteral("Radio mode."));
	auto* rigFilter = new QComboBox;
	rigFilter->setObjectName(QStringLiteral("rigFilter"));
	rigFilter->addItems({
		QStringLiteral("0-5000"), QStringLiteral("80-3000"), QStringLiteral("80-2000"),
		QStringLiteral("950-2050"), QStringLiteral("1240-1760"), QStringLiteral("1370-1680"),
		QStringLiteral("1435-1565"), QStringLiteral("1468-1532")
	});
	rigFilter->setCurrentText(QStringLiteral("0-5000"));
	rigFilter->setToolTip(QStringLiteral("Filter width (Hz)."));
	connect(rigMode, &QComboBox::currentTextChanged, this, [this](const QString& name) {
		if (m_applying)
			return;
		if (!engine_up()) {
			notConnected(name);
			return;
		}
		engine_set_rig_mode(name.toUtf8().constData());
	});
	connect(rigFilter, &QComboBox::currentTextChanged, this, [this](const QString& name) {
		if (m_applying)
			return;
		if (!engine_up()) {
			notConnected(QStringLiteral("Filter"));
			return;
		}
		engine_set_rig_filter(name.toUtf8().constData());
	});

	m_mode = new ModemMenu;
	if (engine_up()) {
		EngineView opened;
		engine_view(&opened);
		m_mode->setCurrentId(QString::fromUtf8(opened.modem));
	}

	// Same cluster as fldigi's menu bar: Spot, RxID, TxID, TUNE.
	auto* spot = toggleButton(QStringLiteral("Spot"),
		QStringLiteral("Send received call signs to PSK Reporter. Click again to stop."), false);
	auto* rxId = toggleButton(QStringLiteral("RxID"),
		QStringLiteral("Listen for RSID across the waterfall and follow that mode and audio frequency."), false);
	rxId->setObjectName(QStringLiteral("rxId"));
	auto* macroTimer = stripButton(QStringLiteral("0"),
		QStringLiteral("Seconds until the macro repeats. Click to stop it and return to receive."));
	macroTimer->setObjectName(QStringLiteral("macroTimer"));
	macroTimer->setMinimumWidth(48);
	macroTimer->hide();
	connect(macroTimer, &QPushButton::clicked, this, [this, macroTimer] {
		if (!engine_up())
			return;
		macroTimer->hide();
		engine_abort();
	});
	auto* txId = toggleButton(QStringLiteral("TxID"),
		QStringLiteral("Send RSID at the start of transmit. CW, BPSK-31, and RTTY do not send it."), false);
	txId->setObjectName(QStringLiteral("txId"));
	auto* tune = stripButton(QStringLiteral("TUNE"),
		QStringLiteral("Send a tune tone. Click again to stop."));
	tune->setObjectName(QStringLiteral("tuneButton"));
	tune->setCheckable(true);
	for (QPushButton* button : {spot, rxId, txId, tune})
		button->setMinimumWidth(62);
	connect(spot, &QPushButton::toggled, this, [this, spot](bool on) {
		if (m_applying)
			return;
		if (!engine_up()) {
			notConnected(QStringLiteral("Spot"));
			spot->setChecked(false);
			return;
		}
		pushOperator();
		if (engine_set_spot(on ? 1 : 0))
			return;
		QSignalBlocker block(spot);
		spot->setChecked(false);
		QMessageBox::information(this, QStringLiteral("PSK Reporter"),
			QString::fromUtf8(engine_spot_error()));
	});
	connect(rxId, &QPushButton::toggled, this, [this](bool on) {
		if (m_applying)
			return;
		if (!engine_up()) {
			notConnected(QStringLiteral("RxID"));
			return;
		}
		engine_set_rxid(on ? 1 : 0);
	});
	connect(txId, &QPushButton::toggled, this, [this](bool on) {
		if (m_applying)
			return;
		if (!engine_up()) {
			notConnected(QStringLiteral("TxID"));
			return;
		}
		engine_set_txid(on ? 1 : 0);
	});
	connect(tune, &QPushButton::clicked, this, [this, tune] {
		if (!engine_up()) {
			notConnected(QStringLiteral("TUNE"));
			tune->setChecked(false);
			return;
		}
		EngineView view;
		engine_view(&view);
		engine_tune(view.tuning ? 0 : 1);
	});

	// TxID over RxID, and Tune over Spot on the far right.
	auto* ops = new QGridLayout;
	ops->setHorizontalSpacing(6);
	ops->setVerticalSpacing(4);
	ops->addWidget(txId, 0, 1);
	ops->addWidget(tune, 0, 2);
	ops->addWidget(rxId, 1, 1);
	ops->addWidget(spot, 1, 2);
	ops->setColumnStretch(0, 1);
	ops->setColumnStretch(1, 1);

	m_rx = new QPlainTextEdit;
	m_rx->setObjectName(QStringLiteral("rxText"));
	m_rx->setReadOnly(true);
	m_rx->setPlaceholderText(QStringLiteral("Received text"));
	m_rx->setToolTip(QStringLiteral(
		"Received text. Click a call sign to put it in the lookup box.\n"
		"Right-click and choose Clear to empty this pane."));
	m_rx->setMinimumHeight(64);
	m_rx->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	m_tx = new QPlainTextEdit;
	m_tx->setObjectName(QStringLiteral("txText"));
	m_tx->setPlaceholderText(QStringLiteral("Transmit text. Control-T transmits, Control-R receives."));
	m_tx->setToolTip(QStringLiteral(
		"Transmit text. With this box focused, Control-T transmits and Control-R returns to receive. "
		"Escape twice stops transmit immediately.\n"
		"Right-click and choose Clear to empty this pane."));
	m_tx->setMinimumHeight(64);
	m_tx->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	auto* hellPicture = new HellPicture;
	auto* rxPane = new RxPane(m_rx, hellPicture);
	installClearMenu(m_rx, [this] {
		if (engine_up())
			engine_clear_rx();
		m_rx->clear();
	});
	installClearMenu(m_tx, [this] {
		if (engine_up()) {
			EngineView view;
			engine_view(&view);
			// An empty buffer must not leave the radio keyed.
			if (view.transmitting || view.tuning)
				engine_transmit(0);
			engine_clear_tx();
		}
		const bool applying = m_applying;
		m_applying = true;
		m_tx->clear();
		m_txSeen.clear();
		m_applying = applying;
	});
	auto* macroHost = new QWidget;
	auto* macros = new QHBoxLayout(macroHost);
	macros->setContentsMargins(0, 0, 0, 0);
	macros->setSpacing(6);
	auto* macroRow = new MacroButtons([this](int index) {
		if (!engine_up()) {
			notConnected(QStringLiteral("Macro"));
			return;
		}
		pushOperator();
		pushQso(this);
		engine_macro(index);
		showEngineTxText();
	}, [this](int index) {
		if (!engine_up()) {
			notConnected(QStringLiteral("Macro"));
			return;
		}
		editQtMacro(this, index);
	});
	macros->addWidget(macroRow);
	macros->addStretch(1);

	auto* scopeHost = new QWidget;
	auto* scopeRow = new QHBoxLayout(scopeHost);
	scopeRow->setContentsMargins(0, 0, 0, 0);
	scopeRow->setSpacing(6);
	auto* waterfall = new WaterfallView;
	connect(waterfall, &WaterfallView::tuned, this, [this](int hz) {
		if (!engine_up()) {
			notConnected(QStringLiteral("Waterfall"));
			return;
		}
		const bool exact = (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
		engine_set_carrier(hz, exact ? 0 : 1);
	});
	auto* signal = new SignalColumn;
	scopeRow->addWidget(waterfall, 1);
	scopeRow->addWidget(signal);

	auto* browserPane = new BrowserPane([this](const QString& what) { notConnected(what); });
	auto* textRow = new TextRow(browserPane, new TextSplit(rxPane, m_tx), this);

	auto* browser = toggleButton(QStringLiteral("Browser"),
		QStringLiteral("Show the signal browser to the left of receive and transmit."), false);
	browser->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_mode->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	connect(browser, &QPushButton::toggled, textRow, [textRow](bool on) {
		textRow->setOpen(on);
	});
	textRow->setUserClosed([browser] {
		QSignalBlocker block(browser);
		browser->setChecked(false);
	});

	auto* head = new QGridLayout;
	head->setHorizontalSpacing(8);
	head->setVerticalSpacing(4);
	head->addWidget(m_freq, 0, 0, 1, 2);
	head->addWidget(rigMode, 1, 0);
	head->addWidget(rigFilter, 1, 1);
	head->addWidget(m_mode, 0, 2, Qt::AlignBottom);
	head->addWidget(browser, 1, 2);
	head->setColumnStretch(3, 1);
	head->addLayout(ops, 0, 4, 2, 1, Qt::AlignRight | Qt::AlignVCenter);
	root->addLayout(head);

	auto* qrz = new QrzClient(this);
	applyQrzAccount(qrz);
	auto* qrzRow = qrzLogRow(central, qrz, [this, qrz](const QString&) {
		if (editQrzAccount(this)) {
			applyQrzAccount(qrz);
			pushOperator();
		}
	});
	root->addWidget(qrzRow);
	auto* rxCalls = new RxCallFilter(m_rx);
	rxCalls->onCall = [qrzRow](const QString& call) { qrzApplyCall(qrzRow, call); };
	m_rx->viewport()->installEventFilter(rxCalls);
	browserPane->setCallHandler([qrzRow](const QString& call) {
		qrzApplyCall(qrzRow, call);
	});

	root->addWidget(new DisplaySplit(
		textRow,
		macroHost,
		scopeHost), 1);

	auto* wfRow = new QHBoxLayout;
	wfRow->setSpacing(6);
	auto* wfMode = stripButton(QStringLiteral("WF"),
		QStringLiteral("Waterfall, FFT, or signal."));
	auto* refLevel = counterBox(-80, 0, 0, 72,
		QStringLiteral("Upper signal level (dB)."));
	auto* ampSpan = counterBox(6, 90, 70, 72,
		QStringLiteral("Signal range (dB)."));
	auto* scale = stripButton(QStringLiteral("x1"),
		QStringLiteral("Waterfall scale."));
	auto* slewLeft = stripButton(QStringLiteral("<"),
		QStringLiteral("Slew display lower in frequency."));
	auto* center = stripButton(QStringLiteral("||"),
		QStringLiteral("Center display on the signal."));
	auto* slewRight = stripButton(QStringLiteral(">"),
		QStringLiteral("Slew display higher in frequency."));
	auto* dropRate = stripButton(QStringLiteral("NORM"),
		QStringLiteral("Waterfall drop speed."));
	auto* carrier = counterBox(16, 4000, 1500, 78,
		QStringLiteral("Cursor frequency (Hz)."));
	carrier->setObjectName(QStringLiteral("carrierHz"));
	auto* qsy = stripButton(QStringLiteral("QSY"),
		QStringLiteral("Center in the passband."));
	qsy->setEnabled(engine_up() != 0);
	auto* centerTune = stripButton(QStringLiteral("Ctr"),
		QStringLiteral("Center the tune at 1500 Hz."));
	centerTune->setObjectName(QStringLiteral("centerTune"));
	auto* store = stripButton(QStringLiteral("Store"),
		QStringLiteral("Store mode and frequency. Right click to recall. "
			"Recalling a different dial frequency moves the radio."));
	store->setObjectName(QStringLiteral("storeButton"));
	store->setContextMenuPolicy(Qt::CustomContextMenu);
	auto* lock = toggleButton(QStringLiteral("Lk"),
		QStringLiteral("Lock transmit frequency."), false);
	auto* reverse = toggleButton(QStringLiteral("Rv"),
		QStringLiteral("Reverse."), false);
	auto* trx = stripButton(QStringLiteral("T/R"),
		QStringLiteral("Click to transmit. Click again to receive."));
	trx->setObjectName(QStringLiteral("trxButton"));
	trx->setCheckable(true);

	connect(wfMode, &QPushButton::clicked, this, [this, wfMode] {
		static const QStringList names = {
			QStringLiteral("WF"), QStringLiteral("FFT"), QStringLiteral("SIG")
		};
		int index = names.indexOf(wfMode->text());
		index = (index + 1) % names.size();
		wfMode->setText(names.at(index));
		if (!engine_up()) {
			notConnected(QStringLiteral("Waterfall"));
			return;
		}
		engine_wf_mode(index);
	});
	connect(scale, &QPushButton::clicked, this, [this, scale] {
		static const QStringList names = {
			QStringLiteral("x1"), QStringLiteral("x2"), QStringLiteral("x4")
		};
		static const int mags[] = {1, 2, 3};
		int index = names.indexOf(scale->text());
		index = (index + 1) % names.size();
		scale->setText(names.at(index));
		if (!engine_up()) {
			notConnected(QStringLiteral("Waterfall scale"));
			return;
		}
		engine_wf_mag(mags[index]);
	});
	connect(slewLeft, &QPushButton::clicked, this, [this] {
		if (!engine_up()) {
			notConnected(QStringLiteral("Slew"));
			return;
		}
		engine_wf_slew(-100);
	});
	connect(center, &QPushButton::clicked, this, [this] {
		if (!engine_up()) {
			notConnected(QStringLiteral("Center"));
			return;
		}
		engine_wf_center();
	});
	connect(slewRight, &QPushButton::clicked, this, [this] {
		if (!engine_up()) {
			notConnected(QStringLiteral("Slew"));
			return;
		}
		engine_wf_slew(100);
	});
	connect(dropRate, &QPushButton::clicked, this, [this, dropRate] {
		static const QStringList names = {
			QStringLiteral("NORM"), QStringLiteral("FAST"),
			QStringLiteral("PAUSE"), QStringLiteral("SLOW")
		};
		static const int speeds[] = {2, 1, 0, 4};
		int index = names.indexOf(dropRate->text());
		index = (index + 1) % names.size();
		dropRate->setText(names.at(index));
		if (!engine_up()) {
			notConnected(QStringLiteral("Waterfall speed"));
			return;
		}
		engine_wf_speed(speeds[index]);
	});
	connect(refLevel, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
		if (m_applying)
			return;
		if (!engine_up()) {
			notConnected(QStringLiteral("Ref level"));
			return;
		}
		engine_wf_ref(value);
	});
	connect(ampSpan, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
		if (m_applying)
			return;
		if (!engine_up()) {
			notConnected(QStringLiteral("Range"));
			return;
		}
		engine_wf_span(value);
	});
	connect(carrier, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
		if (m_applying)
			return;
		if (!engine_up()) {
			notConnected(QStringLiteral("Carrier"));
			return;
		}
		engine_set_carrier(value, 0);
	});
	connect(qsy, &QPushButton::clicked, this, [this] {
		if (!engine_up())
			return;
		engine_qsy();
	});
	connect(centerTune, &QPushButton::clicked, this, [this] {
		if (!engine_up()) {
			notConnected(QStringLiteral("Center"));
			return;
		}
		engine_set_carrier(1500, 0);
	});
	connect(store, &QPushButton::clicked, this, [this] {
		if (!engine_up()) {
			notConnected(QStringLiteral("Store"));
			return;
		}
		char note[200];
		engine_store(note, (int)sizeof note);
		holdStatus(QString::fromUtf8(note));
	});
	connect(store, &QPushButton::customContextMenuRequested, this, [this, store](const QPoint& pos) {
		if (!engine_up()) {
			notConnected(QStringLiteral("Store"));
			return;
		}
		QMenu menu(store);
		const int count = engine_store_count();
		if (count < 1) {
			QAction* empty = menu.addAction(QStringLiteral("Nothing stored"));
			empty->setEnabled(false);
		}
		for (int i = 0; i < count; ++i) {
			char label[160];
			engine_store_label(i, label, (int)sizeof label);
			menu.addAction(QString::fromUtf8(label), this, [this, i] {
				engine_store_select(i);
			});
		}
		if (count > 0) {
			menu.addSeparator();
			menu.addAction(QStringLiteral("Clear"), this, [this] {
				engine_store_clear();
			});
		}
		menu.exec(store->mapToGlobal(pos));
	});
	connect(lock, &QPushButton::toggled, this, [this](bool on) {
		if (m_applying)
			return;
		if (!engine_up()) {
			notConnected(QStringLiteral("Lock"));
			return;
		}
		engine_set_lock(on ? 1 : 0);
	});
	connect(reverse, &QPushButton::toggled, this, [this](bool on) {
		if (m_applying)
			return;
		if (!engine_up()) {
			notConnected(QStringLiteral("Reverse"));
			return;
		}
		engine_set_reverse(on ? 1 : 0);
	});
	connect(trx, &QPushButton::clicked, this, [this, trx] {
		if (!engine_up()) {
			notConnected(QStringLiteral("T/R"));
			trx->setChecked(false);
			return;
		}
		EngineView view;
		engine_view(&view);
		const bool on = view.transmitting || view.tuning;
		if (!on)
			engine_set_tx_text(m_tx->toPlainText().toUtf8().constData());
		engine_transmit(on ? 0 : 1);
	});

	auto* afc = toggleButton(QStringLiteral("AFC"),
		QStringLiteral("Track a locked signal. A waterfall click also searches nearby on PSK."), true);
	auto* sql = toggleButton(QStringLiteral("SQL"),
		QStringLiteral("Squelch."), true);
	connect(afc, &QPushButton::toggled, this, [this](bool on) {
		if (m_applying)
			return;
		if (!engine_up()) {
			notConnected(QStringLiteral("AFC"));
			return;
		}
		engine_set_afc(on ? 1 : 0);
	});
	connect(sql, &QPushButton::toggled, this, [this](bool on) {
		if (m_applying)
			return;
		if (!engine_up()) {
			notConnected(QStringLiteral("SQL"));
			return;
		}
		engine_set_sql(on ? 1 : 0);
	});

	wfRow->addWidget(wfMode);
	wfRow->addWidget(refLevel);
	wfRow->addWidget(ampSpan);
	wfRow->addWidget(scale);
	wfRow->addWidget(slewLeft);
	wfRow->addWidget(center);
	wfRow->addWidget(slewRight);
	wfRow->addWidget(dropRate);
	wfRow->addWidget(carrier);
	wfRow->addWidget(qsy);
	wfRow->addWidget(centerTune);
	wfRow->addWidget(store);
	wfRow->addWidget(lock);
	wfRow->addWidget(reverse);
	wfRow->addStretch(1);
	wfRow->addWidget(afc);
	wfRow->addWidget(sql);
	wfRow->addWidget(trx);
	root->addLayout(wfRow);

	auto* statusRow = new QHBoxLayout;
	statusRow->setSpacing(6);
	statusRow->setAlignment(Qt::AlignBottom);
	m_modeStatus = stripButton(m_mode->currentLabel(),
		QStringLiteral("Left click: next mode."));
	{
		QFontMetrics metrics(m_modeStatus->font());
		const int widest = metrics.horizontalAdvance(m_mode->widestLabel());
		m_modeStatus->setMinimumWidth(qMax(108, widest + 28));
	}
	connect(m_modeStatus, &QPushButton::clicked, this, [this] {
		if (!engine_up() || m_mode->currentId().isEmpty()) {
			notConnected(m_mode->currentLabel());
			return;
		}
		m_mode->advance();
	});

	auto* modemStatus = statusField(96);
	modemStatus->setToolTip(QStringLiteral("Modem status."));
	auto* signalReport = statusField(96);
	signalReport->setToolTip(QStringLiteral("Signal report."));
	auto* reportStack = new QWidget;
	auto* reportCol = new QVBoxLayout(reportStack);
	reportCol->setContentsMargins(0, 0, 0, 0);
	reportCol->setSpacing(4);
	reportCol->addWidget(signalReport);
	reportCol->addWidget(modemStatus);
	reportStack->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

	m_status = statusField(160);
	m_status->setMinimumWidth(80);
	m_status->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	m_status->setText(engine_up()
		? QStringLiteral("Receiving")
		: QStringLiteral("Modem not connected"));

	auto* vu = new LevelMeter;

	auto* statusStack = new QWidget;
	auto* statusCol = new QVBoxLayout(statusStack);
	statusCol->setContentsMargins(0, 0, 0, 0);
	statusCol->setSpacing(4);
	statusCol->addWidget(vu);
	statusCol->addWidget(m_status);
	statusStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

	auto* levels = new QWidget;
	auto* levelCol = new QVBoxLayout(levels);
	levelCol->setContentsMargins(0, 0, 0, 0);
	levelCol->setSpacing(4);

	m_rxLevel = new QDoubleSpinBox;
	m_rxLevel->setRange(-30.0, 0.0);
	m_rxLevel->setSingleStep(1.0);
	m_rxLevel->setDecimals(1);
	m_rxLevel->setPrefix(QStringLiteral("Rx "));
	m_rxLevel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	m_rxLevel->setToolTip(QStringLiteral(
		"TCI receive attenuator (dB). Lowers a hot fixed receiver output. "
		"0 dB leaves the level unchanged. Shown while TCI audio is in use."));
	{
		QSettings settings;
		double rxDb = settings.value(QStringLiteral("tciRxLevel"), 0.0).toDouble();
		if (rxDb > 0.0)
			rxDb = 0.0;
		if (rxDb < -30.0)
			rxDb = -30.0;
		m_rxLevel->setValue(rxDb);
	}
	connect(m_rxLevel, qOverload<double>(&QDoubleSpinBox::valueChanged),
		this, [this](double value) {
			if (m_applying)
				return;
			QSettings settings;
			settings.setValue(QStringLiteral("tciRxLevel"), value);
			if (engine_up())
				engine_set_rx_db(value);
		});

	auto* txLevel = new QDoubleSpinBox;
	txLevel->setRange(-30.0, 0.0);
	txLevel->setSingleStep(1.0);
	txLevel->setDecimals(1);
	txLevel->setPrefix(QStringLiteral("Tx "));
	txLevel->setValue(-6.0);
	txLevel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	txLevel->setToolTip(QStringLiteral(
		"Tx level attenuator (dB)."));
	connect(txLevel, qOverload<double>(&QDoubleSpinBox::valueChanged),
		this, [this](double value) {
			if (m_applying || !engine_up())
				return;
			engine_set_tx_db(value);
		});
	levelCol->addWidget(m_rxLevel);
	levelCol->addWidget(txLevel);

	signalReport->ensurePolished();
	modemStatus->ensurePolished();
	vu->ensurePolished();
	m_status->ensurePolished();
	m_rxLevel->ensurePolished();
	txLevel->ensurePolished();
	const QFontMetrics attenMetrics(m_rxLevel->font());
	const int attenW = attenMetrics.horizontalAdvance(QStringLiteral("Tx -30.0")) + 22;
	m_rxLevel->setFixedWidth(attenW);
	txLevel->setFixedWidth(attenW);
	const int meterH = qMax(vu->sizeHint().height(), m_rxLevel->sizeHint().height());
	const int barH = qMax(m_status->sizeHint().height(), txLevel->sizeHint().height());
	vu->setFixedHeight(meterH);
	m_status->setFixedHeight(barH);
	signalReport->setFixedHeight(meterH);
	modemStatus->setFixedHeight(barH);
	m_rxLevel->setFixedHeight(meterH);
	txLevel->setFixedHeight(barH);

	statusRow->addWidget(m_modeStatus);
	statusRow->addWidget(reportStack);
	statusRow->addWidget(statusStack, 1);
	statusRow->addWidget(levels);
	root->addLayout(statusRow);
	if (engine_up()) {
		EngineView opened;
		engine_view(&opened);
		setTciAudio(opened.tci_audio != 0);
	} else {
		setTciAudio(true);
	}

	setCentralWidget(central);
	buildMenus();
	{
		QSettings settings;
		if (settings.contains(QStringLiteral("textFont/family"))) {
			QFont font(settings.value(QStringLiteral("textFont/family")).toString());
			const int points = settings.value(QStringLiteral("textFont/pointSize"), 13).toInt();
			font.setPointSize(qBound(8, points, 72));
			applyTextFont(font);
		}
	}
	auto* logMenu = new QMenu(QStringLiteral("Log"), this);
	logMenu->addAction(QStringLiteral("QRZ account…"), this, [this, qrz] {
		if (editQrzAccount(this)) {
			applyQrzAccount(qrz);
			pushOperator();
		}
	});
	const QList<QAction*> bar = menuBar()->actions();
	if (bar.isEmpty())
		menuBar()->addMenu(logMenu);
	else
		menuBar()->insertMenu(bar.last(), logMenu);

	central->ensurePolished();
	rigFilter->ensurePolished();
	{
		QFontMetrics filterMetrics(rigFilter->font());
		int filterText = 0;
		for (int i = 0; i < rigFilter->count(); ++i)
			filterText = qMax(filterText, filterMetrics.horizontalAdvance(rigFilter->itemText(i)));
		// Stylesheet padding is 8px each side and the arrow well is 22px.
		rigFilter->setMinimumWidth(filterText + 8 + 8 + 22 + 4);
	}
	macroRow->applyWidths();
	const int need = root->totalMinimumSize().width();
	if (need > minimumWidth())
		setMinimumWidth(need);
	if (width() < minimumWidth())
		resize(minimumWidth(), height());

	if (engine_up()) {
		pushOperator();
		browserPane->setPickHandler([](int row) { engine_browser_pick(row); });
		browserPane->setClearHandler(
			[](int row) { engine_browser_clear_line(row); },
			[] { engine_browser_clear(); });
		connect(signal->squelch(), &QSlider::valueChanged, this, [this](int value) {
			if (m_applying)
				return;
			engine_set_squelch(value);
		});
		connect(browserPane->viewerSquelch(), &QSlider::valueChanged, this, [this](int tenths) {
			if (m_applying)
				return;
			engine_set_viewer_sql(tenths / 10.0);
		});
		connect(m_tx, &QPlainTextEdit::textChanged, this, [this] {
			if (m_applying)
				return;
			engine_set_tx_text(m_tx->toPlainText().toUtf8().constData());
			m_txSeen = m_tx->toPlainText();
		});
		auto* keys = new TxKeyFilter(m_tx);
		keys->transmit = [this] {
			if (m_applying || !engine_up())
				return;
			engine_set_tx_text(m_tx->toPlainText().toUtf8().constData());
			EngineView view;
			engine_view(&view);
			if (!view.transmitting && !view.tuning)
				engine_transmit(1);
		};
		keys->receive = [this] {
			if (m_applying || !engine_up())
				return;
			engine_transmit(0);
		};
		keys->abortTransmit = [this] {
			if (m_applying || !engine_up())
				return false;
			EngineView view;
			engine_view(&view);
			if (!view.transmitting && !view.tuning && view.macro_timer <= 0)
				return false;
			engine_abort();
			return true;
		};
		keys->functionMacro = [this, macroRow](int slot) {
			if (m_applying || !engine_up())
				return;
			macroRow->trigger(slot);
		};
		keys->box = m_tx;
		keys->viewport = m_tx->viewport();
		keys->window = this;
		qApp->installEventFilter(keys);
	}

	m_mode->onSelect = [this](const QString& id, const QString& label) {
		if (m_modeStatus && !label.isEmpty())
			m_modeStatus->setText(label);
		if (m_applying || id.isEmpty())
			return;
		if (!engine_up()) {
			notConnected(label);
			return;
		}
		engine_set_modem(id.toUtf8().constData());
	};
	connect(m_theme, &Theme::modeChanged, this, [this](Theme::Mode) {
		syncAppearance();
	});
	syncAppearance();

	if (engine_up()) {
		pushOperator();
		EngineView opened;
		engine_view(&opened);
		if (opened.spot)
			engine_set_spot(1);
		auto* timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, [=] {
			engine_tick();
			EngineView view;
			engine_view(&view);
			m_applying = true;

			m_freq->setHz(view.frequency_hz);
			auto choose = [](QComboBox* box, const QString& text) {
				if (!box || (box->view() && box->view()->isVisible()))
					return;
				// No-rig fldigi stores two spaces in the bandwidth box.
				const QString trimmed = text.trimmed();
				if (trimmed.isEmpty())
					return;
				if (box->findText(trimmed) < 0)
					box->addItem(trimmed);
				if (box->currentText() != trimmed)
					box->setCurrentText(trimmed);
			};
			choose(rigMode, QString::fromUtf8(view.rig_mode));
			choose(rigFilter, QString::fromUtf8(view.rig_filter));
			if (m_mode && !m_mode->menuOpen()) {
				const QString id = QString::fromUtf8(view.modem);
				if (!id.isEmpty() && id != m_mode->currentId())
					m_mode->setCurrentId(id);
				if (m_modeStatus && !m_mode->currentLabel().isEmpty()
					&& m_modeStatus->text() != m_mode->currentLabel())
					m_modeStatus->setText(m_mode->currentLabel());
			}

			auto mark = [](QPushButton* button, bool on) {
				if (button && button->isChecked() != on)
					button->setChecked(on);
			};
			mark(spot, view.spot);
			mark(rxId, view.rx_id);
			mark(txId, view.tx_id);
			if (macroTimer) {
				const bool counting = view.macro_timer > 0;
				if (counting) {
					if (!macroTimer->isVisible()) {
						ops->addWidget(macroTimer, 0, 0);
						macroTimer->setVisible(true);
					}
					const QString digits = QString::number(view.macro_timer);
					if (macroTimer->text() != digits)
						macroTimer->setText(digits);
				} else if (macroTimer->isVisible() || ops->indexOf(macroTimer) >= 0) {
					ops->removeWidget(macroTimer);
					macroTimer->setVisible(false);
				}
			}
			if (afc) {
				const bool capable = view.afc_ok != 0;
				if (afc->isEnabled() != capable)
					afc->setEnabled(capable);
				const QString tip = capable
					? QStringLiteral("Track a locked signal. A waterfall click also searches nearby on PSK. Shift-click tunes exactly.")
					: QStringLiteral("This mode has no automatic frequency control.");
				if (afc->toolTip() != tip)
					afc->setToolTip(tip);
				// An incapable mode shows the button off and does not clear the saved flag.
				mark(afc, capable && view.afc);
			}
			mark(sql, view.sql);
			mark(lock, view.locked);
			mark(reverse, view.reverse);
			mark(tune, view.tuning);
			const bool trxOn = view.transmitting || view.tuning;
			if (trx->isChecked() != trxOn)
				trx->setChecked(trxOn);

			if (!m_rxLevel->hasFocus() && qAbs(m_rxLevel->value() - view.rx_db) > 0.05)
				m_rxLevel->setValue(view.rx_db);
			if (!txLevel->hasFocus() && qAbs(txLevel->value() - view.tx_db) > 0.05)
				txLevel->setValue(view.tx_db);
			setTciAudio(view.tci_audio != 0);
			signal->setLevel(view.level);
			vu->setLevel(view.level);
			if (!signal->squelch()->isSliderDown() && signal->squelch()->value() != view.squelch)
				signal->squelch()->setValue(view.squelch);
			QSlider* viewerSql = browserPane->viewerSquelch();
			const bool sqlUsed = view.viewer_sql_ok != 0;
			if (viewerSql->isEnabled() != sqlUsed)
				viewerSql->setEnabled(sqlUsed);
			const int sqlLo = qRound(view.viewer_sql_lo * 10.0);
			const int sqlHi = qRound(view.viewer_sql_hi * 10.0);
			if (viewerSql->minimum() != sqlLo || viewerSql->maximum() != sqlHi)
				viewerSql->setRange(sqlLo, sqlHi);
			const int viewerTenths = qRound(view.viewer_sql * 10.0);
			if (!viewerSql->isSliderDown() && viewerSql->value() != viewerTenths)
				viewerSql->setValue(viewerTenths);

			if (!m_statusTimer || !m_statusTimer->isActive())
				m_status->setText(QString::fromUtf8(view.status));
			modemStatus->setText(QString::fromUtf8(view.modem_status));
			signalReport->setText(QString::fromUtf8(view.signal_report));
			if (!carrier->hasFocus() && carrier->value() != view.carrier_hz)
				carrier->setValue(view.carrier_hz);

			static const QStringList wfNames = {
				QStringLiteral("WF"), QStringLiteral("FFT"), QStringLiteral("SIG")
			};
			if (view.wf_mode >= 0 && view.wf_mode < wfNames.size())
				wfMode->setText(wfNames.at(view.wf_mode));
			static const QStringList magNames = {
				QString(), QStringLiteral("x1"), QStringLiteral("x2"), QStringLiteral("x4")
			};
			if (view.wf_mag >= 1 && view.wf_mag < magNames.size())
				scale->setText(magNames.at(view.wf_mag));
			// fldigi turns the magnification button off while the scope is showing.
			if (scale->isEnabled() != (view.wf_mode != 2))
				scale->setEnabled(view.wf_mode != 2);
			const char* speedName = "NORM";
			if (view.wf_speed == 0)
				speedName = "PAUSE";
			else if (view.wf_speed == 1)
				speedName = "FAST";
			else if (view.wf_speed == 4)
				speedName = "SLOW";
			dropRate->setText(QString::fromLatin1(speedName));

			char chunk[8192];
			const int got = engine_rx_delta(chunk, (int)sizeof chunk);
			if (got < 0) {
				m_rx->clear();
			} else if (got > 0) {
				QScrollBar* bar = m_rx->verticalScrollBar();
				const bool follow = bar->value() >= bar->maximum() - 4;
				m_rx->moveCursor(QTextCursor::End);
				m_rx->insertPlainText(QString::fromUtf8(chunk, got));
				if (follow)
					bar->setValue(bar->maximum());
			}
			const bool picture = engine_rx_is_picture() != 0;
			rxPane->setPicture(picture);
			if (picture) {
				const int paneW = hellPicture->width();
				const int paneH = hellPicture->height();
				if (paneW > 16 && paneH > 16)
					engine_rx_picture_size(paneW, paneH);
				static QByteArray gray;
				int imageW = 0;
				int imageH = 0;
				int bytes = engine_rx_picture(
					reinterpret_cast<unsigned char*>(gray.data()), gray.size(),
					&imageW, &imageH);
				if (bytes < 0 && imageW > 0 && imageH > 0) {
					gray.resize(imageW * imageH);
					bytes = engine_rx_picture(
						reinterpret_cast<unsigned char*>(gray.data()), gray.size(),
						&imageW, &imageH);
				}
				if (bytes > 0 && imageW > 0 && imageH > 0) {
					const QImage frame(
						reinterpret_cast<const uchar*>(gray.constData()),
						imageW, imageH, imageW, QImage::Format_Grayscale8);
					hellPicture->setFrame(frame.copy());
				}
			}
			// A macro writes the modem buffer while this box still has focus.
			// Show that text. Typed edits update m_txSeen themselves.
			showEngineTxText();

			static int ticks = 0;
			++ticks;
			if ((ticks % 25) == 1) {
				for (int i = 0; i < 12; ++i) {
					char name[64];
					engine_macro_name(macroRow->bank() * 12 + i, name, (int)sizeof name);
					macroRow->setLabel(i, QString::fromUtf8(name));
				}
			}
			if ((ticks % 5) == 1) {
				for (int row = 0; row < 30; ++row) {
					char line[4600];
					engine_browser_line(row, line, (int)sizeof line);
					browserPane->setChannelText(row, QString::fromUtf8(line));
				}
			}

			static QByteArray pixels;
			if (pixels.size() < 4096 * 1024 * 4)
				pixels.resize(4096 * 1024 * 4);
			int imageW = 0;
			int imageH = 0;
			const int bytes = engine_waterfall(
				reinterpret_cast<unsigned char*>(pixels.data()), pixels.size(), &imageW, &imageH);
			if (bytes > 0 && imageW > 0 && imageH > 0) {
				const QImage frame(
					reinterpret_cast<const uchar*>(pixels.constData()),
					imageW, imageH, imageW * 4, QImage::Format_RGBX8888);
				waterfall->setFrame(frame.copy(), view.hz_low, view.hz_high,
					view.carrier_hz, view.bandwidth_hz, view.wf_mode);
			}
			m_applying = false;
		});
		timer->start(40);
	}

	// No saved operator fields means this configuration has never had them.
	// Ask after the window is shown. Close without Save asks again next time.
	{
		QSettings settings;
		const bool haveOperator =
			settings.contains(QStringLiteral("qrz/myCall"))
			|| settings.contains(QStringLiteral("station/name"))
			|| settings.contains(QStringLiteral("station/qth"))
			|| settings.contains(QStringLiteral("station/operCall"))
			|| settings.contains(QStringLiteral("qrz/myGrid"))
			|| settings.contains(QStringLiteral("qrz/antenna"));
		if (!haveOperator) {
			QTimer::singleShot(0, this, [this] {
				editStation(this);
			});
		}
	}
}

void MainWindow::showEvent(QShowEvent* event)
{
	QMainWindow::showEvent(event);
	if (!centralWidget() || !centralWidget()->layout())
		return;
	const int need = centralWidget()->layout()->totalMinimumSize().width();
	if (need > minimumWidth())
		setMinimumWidth(need);
	if (width() < need)
		resize(need, height());
}

void MainWindow::buildMenus()
{
	auto* fileMenu = menuBar()->addMenu(QStringLiteral("File"));
	auto* quit = fileMenu->addAction(QStringLiteral("Quit"));
	quit->setShortcut(QKeySequence::Quit);
	connect(quit, &QAction::triggered, this, &QWidget::close);

	auto* viewMenu = menuBar()->addMenu(QStringLiteral("View"));
	auto* appearanceMenu = viewMenu->addMenu(QStringLiteral("Appearance"));
	auto* group = new QActionGroup(this);
	group->setExclusive(true);
	const QStringList labels = {
		QStringLiteral("System"), QStringLiteral("Light"), QStringLiteral("Dark")
	};
	for (int i = 0; i < 3; ++i) {
		QAction* action = appearanceMenu->addAction(labels.at(i));
		action->setCheckable(true);
		group->addAction(action);
		m_appearanceActions[i] = action;
		connect(action, &QAction::triggered, this, [this, i] {
			if (m_theme)
				m_theme->setMode(static_cast<Theme::Mode>(i), true);
		});
	}

	auto* configureMenu = menuBar()->addMenu(QStringLiteral("Configure"));
	configureMenu->addAction(QStringLiteral("Station…"), this, [this] {
		editStation(this);
	});
	configureMenu->addAction(QStringLiteral("TCI…"), this, [this] {
		editTci(this);
	});
	configureMenu->addAction(QStringLiteral("Text font…"), this, [this] {
		chooseTextFont();
	});

	auto* helpMenu = menuBar()->addMenu(QStringLiteral("Help"));
	helpMenu->addAction(QStringLiteral("About"), this, [this] {
		QMessageBox::about(this, QString::fromUtf8(TCIDIGI_NAME),
			QString::fromUtf8(
				TCIDIGI_NAME " " TCIDIGI_VERSION " is a Qt interface for fldigi.\n\n"
				"Appearance follows the system light or dark setting, "
				"or a pinned Light or Dark choice. "
				"The older FLTK scheme list is not part of this interface.\n\n"
				"Based on fldigi. GPL-3.0-or-later."));
	});
}

void MainWindow::syncAppearance()
{
	if (!m_theme)
		return;
	const int index = static_cast<int>(m_theme->mode());
	for (int i = 0; i < 3; ++i) {
		if (m_appearanceActions[i])
			m_appearanceActions[i]->setChecked(i == index);
	}
}

void MainWindow::setTciAudio(bool on)
{
	if (m_rxLevel)
		m_rxLevel->setVisible(on);
}

void MainWindow::holdStatus(const QString& text)
{
	if (!m_status)
		return;
	m_status->setText(text);
	if (!m_statusTimer) {
		m_statusTimer = new QTimer(this);
		m_statusTimer->setSingleShot(true);
		connect(m_statusTimer, &QTimer::timeout, this, [this] {
			if (!engine_up() && m_status)
				m_status->setText(QStringLiteral("Modem not connected"));
		});
	}
	m_statusTimer->start(2500);
}

void MainWindow::notConnected(const QString& what)
{
	holdStatus(what + QStringLiteral(" is not connected in this window."));
	if (m_statusTimer)
		m_statusTimer->start(4000);
}

void MainWindow::applyTextFont(const QFont& font)
{
	QFont sized = font;
	int points = sized.pointSize();
	if (points < 1)
		points = 13;
	points = qBound(8, points, 72);
	sized.setPointSize(points);
	QString family = sized.family();
	family.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
	family.replace(QLatin1Char('"'), QStringLiteral("\\\""));
	const QString rule = QStringLiteral("font-family: \"%1\"; font-size: %2pt;")
		.arg(family, QString::number(points));
	const auto paint = [&](QWidget* widget) {
		if (!widget || widget->objectName().isEmpty())
			return;
		// The window stylesheet sets every widget to 13px. An id rule on the
		// widget itself is what actually changes the size. The document font
		// is what the receive and transmit text draws with.
		widget->setStyleSheet(QStringLiteral("#%1 { %2 }").arg(widget->objectName(), rule));
		widget->setFont(sized);
		if (auto* edit = qobject_cast<QPlainTextEdit*>(widget)) {
			edit->document()->setDefaultFont(sized);
			QTextCursor cursor(edit->document());
			cursor.select(QTextCursor::Document);
			QTextCharFormat format;
			format.setFont(sized);
			cursor.mergeCharFormat(format);
		}
		widget->update();
	};
	paint(m_rx);
	paint(m_tx);
	paint(findChild<QListWidget*>(QStringLiteral("pskBrowser")));
}

void MainWindow::chooseTextFont()
{
	struct Snap {
		QWidget* widget = nullptr;
		QString styleSheet;
		QFont widgetFont;
		QFont documentFont;
		bool text = false;
	};
	QList<Snap> snaps;
	const auto capture = [&](QWidget* widget) {
		if (!widget)
			return;
		Snap snap;
		snap.widget = widget;
		snap.styleSheet = widget->styleSheet();
		snap.widgetFont = widget->font();
		if (auto* edit = qobject_cast<QPlainTextEdit*>(widget)) {
			snap.text = true;
			snap.documentFont = edit->document()->defaultFont();
		}
		snaps.append(snap);
	};
	capture(m_rx);
	capture(m_tx);
	capture(findChild<QListWidget*>(QStringLiteral("pskBrowser")));
	const auto restore = [&] {
		for (const Snap& snap : snaps) {
			if (!snap.widget)
				continue;
			snap.widget->setStyleSheet(snap.styleSheet);
			snap.widget->setFont(snap.widgetFont);
			if (!snap.text)
				continue;
			auto* edit = qobject_cast<QPlainTextEdit*>(snap.widget);
			if (!edit)
				continue;
			edit->document()->setDefaultFont(snap.documentFont);
			QTextCursor cursor(edit->document());
			cursor.select(QTextCursor::Document);
			QTextCharFormat format;
			format.setFont(snap.documentFont);
			cursor.mergeCharFormat(format);
			snap.widget->update();
		}
	};

	QFont current = m_rx ? m_rx->font()
		: QFontDatabase::systemFont(QFontDatabase::FixedFont);
	if (current.pointSize() < 1)
		current.setPointSize(13);

	// The macOS font panel has no Save button and returns without applying.
	QFontDialog dialog(current, this);
	dialog.setWindowTitle(QStringLiteral("Text font"));
	dialog.setOption(QFontDialog::DontUseNativeDialog, true);
	const auto labelSave = [&dialog] {
		if (auto* box = dialog.findChild<QDialogButtonBox*>()) {
			if (QPushButton* save = box->button(QDialogButtonBox::Ok))
				save->setText(QStringLiteral("Save"));
		}
	};
	labelSave();
	connect(&dialog, &QFontDialog::currentFontChanged, this, [this](const QFont& font) {
		applyTextFont(font);
	});
	QTimer::singleShot(0, &dialog, labelSave);
	if (dialog.exec() != QDialog::Accepted) {
		restore();
		return;
	}
	QFont chosen = dialog.selectedFont();
	int points = chosen.pointSize();
	if (points < 1)
		points = 13;
	points = qBound(8, points, 72);
	chosen.setPointSize(points);
	applyTextFont(chosen);
	QSettings settings;
	settings.setValue(QStringLiteral("textFont/family"), chosen.family());
	settings.setValue(QStringLiteral("textFont/pointSize"), points);
}

#include "mainwindow.moc"
