// ----------------------------------------------------------------------------
// waterfallview.h -- scale stand-in until the modem waterfall moves over
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

#ifndef FLDIGI_QTUI_WATERFALLVIEW_H
#define FLDIGI_QTUI_WATERFALLVIEW_H

#include <QImage>
#include <QString>
#include <QVector>
#include <QWidget>

class QMouseEvent;
class QPainter;

class WaterfallView : public QWidget
{
	Q_OBJECT
public:
	explicit WaterfallView(QWidget* parent = nullptr);

	QSize sizeHint() const override { return QSize(800, 180); }
	QSize minimumSizeHint() const override { return QSize(320, 96); }
	// mode 0 scrolls the waterfall. mode 1 draws an FFT of the newest row.
	// mode 2 shows the time-domain scope image stretched to the plot.
	void setFrame(const QImage& image, int hzLow, int hzHigh, int carrierHz, int bandwidthHz, int mode);
	// Lk holds transmit at txHz. The view marks it while locked.
	void setTxLock(bool locked, int txHz);

	// A signal SigID has named. center is where the tab sits.
	struct SigidTab {
		int mode = 0;
		int lo = 0;
		int hi = 0;
		int center = 0;
		QString text;
	};
	// Repaints only when the list differs from the one already shown.
	void setSigidLabels(const QVector<SigidTab>& labels);

signals:
	void tuned(int hz);
	// A left click on a SigID tab. hz is that label's center.
	void sigidClicked(int mode, int hz);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	QRect plotRect() const;
	int hzAt(int x) const;
	int sigidTabAt(const QPoint& pos) const;
	void remember(const QImage& image);
	void paintSpectrum(QPainter& painter, const QRect& plot) const;

	// A tab painted last frame, kept so a click can find it.
	struct SigidHit {
		QRect rect;
		SigidTab label;
	};

	QImage m_frame;
	QImage m_history;
	int m_mode = 0;
	int m_hzLow = 0;
	int m_hzHigh = 3000;
	int m_carrier = 1500;
	int m_bandwidth = 31;
	int m_hoverHz = -1;
	bool m_locked = false;
	int m_txHz = 0;
	// Where the TX tab was painted. Empty when it is not showing. A click
	// there tunes, since that tab is drawn on top of a SigID name.
	QRect m_txTab;
	QVector<SigidTab> m_sigidLabels;
	QVector<SigidHit> m_sigidHits;
};

#endif
