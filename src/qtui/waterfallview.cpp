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

WaterfallView::WaterfallView(QWidget* parent)
	: QWidget(parent)
{
	setAutoFillBackground(false);
	setMinimumHeight(96);
	setMouseTracking(true);
	setToolTip(QStringLiteral(
		"Click to tune the modem. On PSK it searches nearby for a signal. "
		"Shift-click tunes to that exact spot. Each row stays one pixel tall."));
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
	if (event->button() != Qt::LeftButton)
		return;
	const QRect plot = plotRect();
	const QPoint pos = event->position().toPoint();
	if (!plot.contains(pos))
		return;
	const int hz = hzAt(pos.x());
	m_carrier = hz;
	m_hoverHz = hz;
	update();
	emit tuned(hz);
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
	const QRect plot = plotRect();
	const int x = event->position().toPoint().x();
	if (x < plot.left() || x > plot.right()) {
		if (m_hoverHz >= 0) {
			m_hoverHz = -1;
			unsetCursor();
			update();
		}
		return;
	}
	const QPoint pos = event->position().toPoint();
	if (plot.contains(pos))
		setCursor(Qt::PointingHandCursor);
	else
		unsetCursor();
	const int hz = hzAt(x);
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
	if (m_hoverHz >= hzLow && m_hoverHz <= hzHigh)
		drawSpan(m_hoverHz, QColor(255, 255, 255), QColor(255, 255, 255));
}
