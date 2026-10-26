// ----------------------------------------------------------------------------
// waterfallview.cpp -- scale stand-in until the modem waterfall moves over
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

#include "waterfallview.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

// fldigi's Ctrl is the Control key. Qt reports that key as Meta on macOS
// and as Control on other systems. Command is Meta there, and is not Ctrl.
static bool controlDown(Qt::KeyboardModifiers mods)
{
#if defined(Q_OS_MACOS)
	return (mods & Qt::MetaModifier) != 0;
#else
	return (mods & Qt::ControlModifier) != 0;
#endif
}

WaterfallView::WaterfallView(QWidget* parent)
	: QWidget(parent)
{
	setAutoFillBackground(false);
	setMinimumHeight(96);
	setMouseTracking(true);
	// Control-click is a history replay. A context menu would take that click.
	setContextMenuPolicy(Qt::PreventContextMenu);
	setToolTip(QStringLiteral(
		"Click to tune the modem. On PSK it searches nearby for a signal. "
		"Shift-click tunes to that exact spot. "
		"Drag the frequency scale to move the radio dial, when that is enabled. "
		"The wheel on the scale steps the dial. "
		"Right-click listens there until you release. "
		"Control-click replays the recent audio. "
		"Option-Control toggles a notch marker. "
		"The middle button toggles AFC. "
		"Each row stays one pixel tall."));
}

void WaterfallView::setFrame(const QImage& image, int hzLow, int hzHigh, int carrierHz, int bandwidthHz, int mode)
{
	const bool modeChanged = mode != m_mode;
	m_mode = mode;
	m_frame = image;
	m_hzLow = hzLow;
	m_hzHigh = hzHigh > hzLow ? hzHigh : hzLow + 1;
	m_carrier = carrierHz;
	if (bandwidthHz > 0)
		m_bandwidth = bandwidthHz;
	// FFT and the scope replace the scrolling picture. Coming back to the
	// waterfall starts a fresh history so a spectrum row is not prepended.
	if (m_mode != 0)
		m_history = QImage();
	else if (modeChanged)
		m_history = QImage();
	if (m_mode == 0)
		remember(image);
	update();
}

void WaterfallView::setTxLock(bool locked, int txHz)
{
	if (locked == m_locked && txHz == m_txHz)
		return;
	m_locked = locked;
	m_txHz = txHz;
	update();
}

void WaterfallView::setSigidLabels(const QVector<SigidTab>& labels)
{
	if (labels.size() == m_sigidLabels.size()) {
		bool same = true;
		for (int i = 0; i < labels.size(); ++i) {
			const SigidTab& a = labels.at(i);
			const SigidTab& b = m_sigidLabels.at(i);
			if (a.mode != b.mode || a.lo != b.lo || a.hi != b.hi
				|| a.center != b.center || a.text != b.text) {
				same = false;
				break;
			}
		}
		if (same)
			return;
	}
	m_sigidLabels = labels;
	update();
}

// The hidden waterfall is a short strip. Stretching this pane used to scale
// those rows. Keep each spectrum one pixel tall and scroll the older rows down.
void WaterfallView::remember(const QImage& image)
{
	if (image.isNull() || image.width() < 1 || image.height() < 1)
		return;
	if (m_history.isNull() || m_history.width() != image.width()
		|| m_history.format() != image.format()) {
		m_history = image;
		return;
	}
	const int width = image.width();
	const QImage top = image.copy(0, 0, width, 1);
	const QImage previous = m_history.copy(0, 0, width, 1);
	if (top == previous)
		return;
	constexpr int cap = 2048;
	const int rows = qMin(cap, m_history.height() + 1);
	QImage next(width, rows, image.format());
	next.fill(0);
	QPainter historyPainter(&next);
	historyPainter.drawImage(QPoint(0, 0), top);
	historyPainter.drawImage(QPoint(0, 1), m_history, QRect(0, 0, width, rows - 1));
	m_history = next;
}

QRect WaterfallView::plotRect() const
{
	const QRect area = rect();
	int scaleH = 0;
	// The scope is a time display. The frequency scale does not apply.
	if (m_mode != 2) {
		QFont scaleFont = font();
		scaleFont.setPointSize(10);
		scaleH = QFontMetrics(scaleFont).height() + 8;
	}
	return QRect(1, 1, qMax(1, area.width() - 2), qMax(1, area.height() - scaleH - 2));
}

void WaterfallView::paintSpectrum(QPainter& painter, const QRect& plot) const
{
	painter.fillRect(plot, QColor(0, 0, 0));
	if (m_frame.isNull() || m_frame.format() != QImage::Format_RGBX8888 || m_frame.height() < 1)
		return;
	const int srcW = m_frame.width();
	const int plotW = qMax(1, plot.width());
	const uchar* scan = m_frame.constScanLine(0);
	if (!scan || srcW < 1)
		return;
	painter.setPen(QColor(48, 48, 48));
	painter.drawLine(plot.left(), plot.bottom(), plot.right(), plot.bottom());
	for (int x = 0; x < plotW; ++x) {
		int x0 = (int)((long long)x * srcW / plotW);
		int x1 = (int)((long long)(x + 1) * srcW / plotW);
		if (x1 <= x0)
			x1 = x0 + 1;
		if (x0 < 0)
			x0 = 0;
		if (x1 > srcW)
			x1 = srcW;
		int bestI = 0;
		int bestR = 0;
		int bestG = 0;
		int bestB = 0;
		for (int sx = x0; sx < x1; ++sx) {
			const uchar* px = scan + sx * 4;
			if (px[3] >= bestI) {
				bestI = px[3];
				bestR = px[0];
				bestG = px[1];
				bestB = px[2];
			}
		}
		// The waterfall stores intensity as 200 * sqrt(level/256).
		double t = bestI / 200.0;
		if (t < 0.0)
			t = 0.0;
		if (t > 1.0)
			t = 1.0;
		int rise = (int)(t * t * (plot.height() - 1));
		if (rise < 1)
			continue;
		painter.fillRect(plot.left() + x, plot.bottom() - rise, 1, rise, QColor(bestR, bestG, bestB));
	}
}

int WaterfallView::sigidTabAt(const QPoint& pos) const
{
	// The TX tab is painted over a name. Those pixels still tune.
	if (m_txTab.contains(pos))
		return -1;
	// Later tabs are painted on top, so they win a shared pixel.
	for (int i = m_sigidHits.size() - 1; i >= 0; --i) {
		if (m_sigidHits.at(i).rect.contains(pos))
			return i;
	}
	return -1;
}

bool WaterfallView::onScale(const QPoint& pos) const
{
	if (m_mode == 2)
		return false;
	const QRect plot = plotRect();
	if (pos.x() < plot.left() || pos.x() > plot.right())
		return false;
	return pos.y() > plot.bottom();
}

int WaterfallView::hzAt(int x) const
{
	const bool live = !m_frame.isNull();
	const int hzLow = live ? m_hzLow : 0;
	const int hzHigh = live ? m_hzHigh : 3000;
	const int hzSpan = qMax(1, hzHigh - hzLow);
	const QRect plot = plotRect();
	const int width = qMax(1, plot.width() - 1);
	int dx = x - plot.left();
	if (dx < 0)
		dx = 0;
	if (dx > width)
		dx = width;
	return hzLow + (int)((long long)dx * hzSpan / width);
}

void WaterfallView::mousePressEvent(QMouseEvent* event)
{
	if (m_mode == 2)
		return;
	// While the right button holds a listen, other buttons wait: its
	// release must still put the saved carrier back.
	if (m_rightDown && event->button() != Qt::RightButton)
		return;
	const QPoint pos = event->position().toPoint();
	const int hz = hzAt(pos.x());
	m_leftArmed = false;
	m_plotPress = false;
	m_scaleDrag = false;
	m_rightDown = false;
	m_sigidPress = false;

	if (event->button() == Qt::MiddleButton) {
		emit afcToggled();
		return;
	}
	if (event->button() == Qt::RightButton) {
		m_rightDown = true;
		m_rightHz = hz;
		emit rightClicked(0, hz);
		return;
	}
	if (event->button() != Qt::LeftButton)
		return;
	// A tab sits on the plot. That click goes to the named signal, and the
	// drag and release stay with it.
	const int tab = sigidTabAt(pos);
	if (tab >= 0) {
		m_sigidPress = true;
		emit sigidClicked(m_sigidHits.at(tab).label.mode, m_sigidHits.at(tab).label.center);
		return;
	}
	const bool over = onScale(pos) || plotRect().contains(pos);
	if (!over)
		return;
	m_leftArmed = true;
	// Option+Control toggles the notch. fldigi stamps that dashed column
	// into the image this view already paints, so the click is not a tune.
	const Qt::KeyboardModifiers mods = event->modifiers();
	if ((mods & Qt::AltModifier) && controlDown(mods)) {
		emit notchToggled(hz);
		return;
	}
	// The scale owns the press even with Control held. The engine moves the
	// dial when that setting is on, and otherwise treats it as a history click.
	if (onScale(pos)) {
		m_scaleDrag = true;
		m_dragHz = hz;
		setCursor(Qt::SizeHorCursor);
		emit scaleDragged(0, hz);
		return;
	}
	if (controlDown(mods)) {
		emit ctrlLeftClicked(hz);
		return;
	}
	m_plotPress = true;
	m_carrier = hz;
	m_hoverHz = hz;
	update();
	emit tuned(hz);
}

void WaterfallView::mouseDoubleClickEvent(QMouseEvent* event)
{
	if (m_mode == 2 || event->button() != Qt::LeftButton)
		return;
	const QPoint pos = event->position().toPoint();
	// The second press does not tune. Release drops a frequency mark
	// instead of the click text. A tab still owns that release.
	if (sigidTabAt(pos) >= 0) {
		m_sigidPress = true;
		m_leftArmed = false;
		return;
	}
	if (!onScale(pos) && !plotRect().contains(pos))
		return;
	m_double = true;
	m_leftArmed = true;
}

void WaterfallView::mouseReleaseEvent(QMouseEvent* event)
{
	if (m_mode == 2)
		return;
	if (event->button() == Qt::RightButton && m_rightDown) {
		m_rightDown = false;
		emit rightClicked(2, hzAt(event->position().toPoint().x()));
		return;
	}
	if (event->button() != Qt::LeftButton)
		return;
	const bool sigid = m_sigidPress;
	const bool wasScale = m_scaleDrag;
	m_sigidPress = false;
	m_scaleDrag = false;
	m_plotPress = false;
	const bool armed = m_leftArmed;
	m_leftArmed = false;
	if (wasScale)
		unsetCursor();
	if (sigid || !armed)
		return;
	const int clicks = m_double ? 1 : 0;
	m_double = false;
	emit leftReleased(clicks);
}

void WaterfallView::wheelEvent(QWheelEvent* event)
{
	if (m_mode == 2) {
		event->ignore();
		return;
	}
	// angleDelta is 120 per notch, positive for wheel up; high-resolution
	// mice send parts of a notch, which add up. A trackpad sends pixels,
	// with an angle of only a unit or two that would never reach a notch,
	// so its pixels are used instead. One event is capped so a flick
	// cannot walk the dial across the band.
	const QPoint pixels = event->pixelDelta();
	int dy = 0;
	if (!pixels.isNull())
		dy = (pixels.y() != 0 ? pixels.y() : pixels.x()) * 3;
	else {
		const QPoint angle = event->angleDelta();
		dy = angle.y() != 0 ? angle.y() : angle.x();
	}
	m_wheelAcc += dy;
	int stepsUp = 0;
	while (m_wheelAcc >= 120 && stepsUp < 30) {
		stepsUp += 1;
		m_wheelAcc -= 120;
	}
	while (m_wheelAcc <= -120 && stepsUp > -30) {
		stepsUp -= 1;
		m_wheelAcc += 120;
	}
	if (stepsUp >= 30 || stepsUp <= -30)
		m_wheelAcc = 0;
	event->accept();
	if (stepsUp == 0)
		return;
	const QPoint pos = event->position().toPoint();
	emit wheeled(onScale(pos) ? 1 : 0, -stepsUp, hzAt(pos.x()));
}

void WaterfallView::mouseMoveEvent(QMouseEvent* event)
{
	if (m_mode == 2) {
		if (m_hoverHz >= 0) {
			m_hoverHz = -1;
			unsetCursor();
			update();
		}
		return;
	}
	const QPoint pos = event->position().toPoint();
	const int hz = hzAt(pos.x());
	const bool left = (event->buttons() & Qt::LeftButton) != 0;
	if (m_sigidPress)
		return;
	if (m_scaleDrag && left) {
		setCursor(Qt::SizeHorCursor);
		const int delta = hz - m_dragHz;
		if (delta != 0) {
			m_dragHz = hz;
			emit scaleDragged(delta, hz);
		}
	} else if (m_plotPress && left && !controlDown(event->modifiers())) {
		if (hz != m_carrier) {
			m_carrier = hz;
			emit tuned(hz);
			update();
		}
	}
	if (m_rightDown && (event->buttons() & Qt::RightButton) && hz != m_rightHz) {
		m_rightHz = hz;
		emit rightClicked(1, hz);
	}
	const bool onTab = sigidTabAt(pos) >= 0;
	const QRect plot = plotRect();
	const int x = pos.x();
	if (!onTab && (x < plot.left() || x > plot.right())) {
		// Drop the hand from a name once the pointer leaves the plot.
		// A scale drag keeps the sideways cursor until the button is up.
		if (!m_scaleDrag)
			unsetCursor();
		if (m_hoverHz >= 0) {
			m_hoverHz = -1;
			update();
		}
		return;
	}
	// A name is clickable even where it hangs into the scale. Leaving it
	// returns to the tune cursor, or the arrow once the pointer is off the plot.
	if (!m_scaleDrag) {
		if (onTab || plot.contains(pos))
			setCursor(Qt::PointingHandCursor);
		else
			unsetCursor();
	}
	if (hz != m_hoverHz) {
		m_hoverHz = hz;
		update();
	}
}

void WaterfallView::leaveEvent(QEvent*)
{
	unsetCursor();
	if (m_hoverHz >= 0) {
		m_hoverHz = -1;
		update();
	}
}

void WaterfallView::paintEvent(QPaintEvent*)
{
	// Rebuilt below. Cleared first so the scope, which returns early, has none.
	m_sigidHits.clear();
	m_txTab = QRect();
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, false);
	const QRect area = rect();
	const QPalette pal = palette();
	const QColor base = pal.color(QPalette::Base);
	const QColor mid = pal.color(QPalette::Mid);
	const QColor text = pal.color(QPalette::PlaceholderText);
	const QColor mark = pal.color(QPalette::Highlight);

	// Square top sits under the drag bar. Round only the bottom corners.
	const QRectF box = QRectF(area).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal radius = 8;
	QPainterPath frame;
	frame.moveTo(box.left(), box.top());
	frame.lineTo(box.right(), box.top());
	frame.lineTo(box.right(), box.bottom() - radius);
	frame.arcTo(QRectF(box.right() - 2 * radius, box.bottom() - 2 * radius, 2 * radius, 2 * radius), 0, -90);
	frame.lineTo(box.left() + radius, box.bottom());
	frame.arcTo(QRectF(box.left(), box.bottom() - 2 * radius, 2 * radius, 2 * radius), 270, -90);
	frame.closeSubpath();
	p.fillPath(frame, base);
	p.strokePath(frame, QPen(mid));

	QFont scaleFont = font();
	scaleFont.setPointSize(10);
	p.setFont(scaleFont);
	const QFontMetrics fm(scaleFont);

	// The spectrogram and the scale share this rectangle, so a tick lands
	// on the same pixel column as the bin it names. The scope has no scale.
	const QRect plot = plotRect();
	const bool live = !m_frame.isNull();
	const int hzLow = live ? m_hzLow : 0;
	const int hzHigh = live ? m_hzHigh : 3000;
	const int hzSpan = qMax(1, hzHigh - hzLow);

	if (m_mode == 1) {
		paintSpectrum(p, plot);
	} else if (m_mode == 2 && live) {
		p.drawImage(plot, m_frame);
	} else if (live && !m_history.isNull()) {
		const int drawH = qMin(m_history.height(), plot.height());
		p.drawImage(
			QRect(plot.left(), plot.top(), plot.width(), drawH),
			m_history,
			QRect(0, 0, m_history.width(), drawH));
	}

	if (m_mode == 2)
		return;

	const int scaleY = plot.bottom() + 1;
	p.setPen(mid);
	p.drawLine(plot.left(), scaleY, plot.right(), scaleY);

	auto xOf = [&](int hz) {
		const long long num = (long long)(hz - hzLow) * (plot.width() - 1);
		return plot.left() + (int)(num / hzSpan);
	};

	// Marks at 0 and the odd hundreds: 100, 300, 500, 700, ...
	auto oddHundred = [](int hz) {
		int mod = hz % 200;
		if (mod < 0)
			mod += 200;
		return mod == 100;
	};
	auto drawTick = [&](int hz) {
		p.setPen(mid);
		const int x = xOf(hz);
		p.drawLine(x, scaleY, x, scaleY + 5);
	};
	drawTick(hzLow);
	int cursor = hzLow - ((hzLow % 100) + 100) % 100;
	if (cursor <= hzLow)
		cursor += 100;
	for (int hz = cursor; hz < hzHigh; hz += 100) {
		if (hz == 0 || oddHundred(hz))
			drawTick(hz);
	}
	drawTick(hzHigh);

	auto labelAt = [&](int hz, int align, int avoidLeft) {
		const QString label = QString::number(hz);
		const int tw = fm.horizontalAdvance(label);
		const int x = xOf(hz);
		int tx = x - tw / 2;
		if (align < 0)
			tx = plot.left();
		else if (align > 0)
			tx = plot.right() - tw + 1;
		tx = qBound(plot.left(), tx, qMax(plot.left(), plot.right() - tw + 1));
		if (align == 0 && tx < avoidLeft)
			return avoidLeft;
		p.setPen(text);
		p.drawText(tx, area.height() - 4, label);
		return tx + tw + 6;
	};
	int used = labelAt(hzLow, -1, plot.left());
	const int lastTw = fm.horizontalAdvance(QString::number(hzHigh));
	const int lastLeft = plot.right() - lastTw;
	for (int hz = cursor; hz < hzHigh; hz += 100) {
		if (hz != 0 && !oddHundred(hz))
			continue;
		if (xOf(hz) + fm.horizontalAdvance(QString::number(hz)) / 2 > lastLeft - 4)
			break;
		used = labelAt(hz, 0, used);
	}
	labelAt(hzHigh, 1, used);

	// Tuned signal: 1px center, and 1px bandwidth edges. Hover is the same
	// three lines in white, with no fill between them.
	const int carrier = live ? m_carrier : 1500;
	auto vline = [&](int hz, const QColor& color) {
		if (hz < hzLow || hz > hzHigh)
			return;
		const int x = xOf(hz);
		// A 1px pen centered on an integer x straddles two columns. Shift
		// onto the pixel center so the stroke is one column.
		QPen pen(color, 1, Qt::SolidLine, Qt::FlatCap);
		p.setPen(pen);
		const qreal col = x + 0.5;
		p.drawLine(QPointF(col, plot.top()), QPointF(col, plot.bottom()));
	};
	auto drawSpan = [&](int centerHz, const QColor& edge, const QColor& center) {
		const int half = m_bandwidth / 2;
		if (half > 0) {
			vline(centerHz - half, edge);
			vline(centerHz + half, edge);
		}
		vline(centerHz, center);
	};
	drawSpan(carrier, QColor(255, 0, 0), mark);
	// Named signals. Lines first, so a span does not strike through a name.
	// A name that would cover one already drawn drops a row, and is left
	// off after three. The TX tab is painted next and stays on top.
	const QColor sigidTeal(0, 170, 190);
	const int tabH = fm.height() + 2;
	for (const SigidTab& label : m_sigidLabels) {
		if (label.center < hzLow || label.center > hzHigh)
			continue;
		int x0 = xOf(qMin(label.lo, label.hi));
		int x1 = xOf(qMax(label.lo, label.hi));
		x0 = qBound(plot.left(), x0, plot.right());
		x1 = qBound(plot.left(), x1, plot.right());
		if (x1 >= x0)
			p.fillRect(x0, plot.top(), x1 - x0 + 1, 2, sigidTeal);
	}
	QVector<QRect> stacked;
	for (const SigidTab& label : m_sigidLabels) {
		if (label.center < hzLow || label.center > hzHigh)
			continue;
		const int tw = fm.horizontalAdvance(label.text) + 8;
		int tx = xOf(label.center) - tw / 2;
		tx = qBound(plot.left(), tx, qMax(plot.left(), plot.right() - tw + 1));
		QRect tab;
		bool fits = false;
		for (int row = 0; row < 3; ++row) {
			tab = QRect(tx, plot.top() + row * tabH, tw, tabH);
			bool overlap = false;
			for (const QRect& have : stacked) {
				if (have.intersects(tab)) {
					overlap = true;
					break;
				}
			}
			if (!overlap) {
				fits = true;
				break;
			}
		}
		if (!fits)
			continue;
		stacked.append(tab);
		p.fillRect(tab, sigidTeal);
		p.setPen(QColor(0, 0, 0));
		p.drawText(tab, Qt::AlignCenter, label.text);
		SigidHit hit;
		hit.rect = tab;
		hit.label = label;
		m_sigidHits.append(hit);
	}
	// Lk: amber span where transmit is held, and a TX tab at the top so the
	// lock still shows when receive sits on the same spot.
	if (m_locked && live && m_txHz >= hzLow && m_txHz <= hzHigh) {
		const QColor amber(255, 176, 0);
		if (m_txHz != carrier)
			drawSpan(m_txHz, amber, amber);
		const QString tag = QStringLiteral("TX");
		const int tw = fm.horizontalAdvance(tag) + 8;
		const int th = fm.height() + 2;
		int tx = xOf(m_txHz) - tw / 2;
		tx = qBound(plot.left(), tx, qMax(plot.left(), plot.right() - tw + 1));
		const QRect tab(tx, plot.top(), tw, th);
		m_txTab = tab;
		p.fillRect(tab, amber);
		p.setPen(QColor(0, 0, 0));
		p.drawText(tab, Qt::AlignCenter, tag);
	}
	if (m_hoverHz >= hzLow && m_hoverHz <= hzHigh)
		drawSpan(m_hoverHz, QColor(255, 255, 255), QColor(255, 255, 255));
}
