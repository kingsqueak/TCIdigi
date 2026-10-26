// ----------------------------------------------------------------------------
// helpwindow.cpp -- TCIdigi's built-in help
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

#include "helpwindow.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace {

// The pages in reading order, with the titles the contents list shows.
struct HelpPage {
	const char* file;
	const char* title;
};
const HelpPage kPages[] = {
	{ "index.html", "TCIdigi Help" },
	{ "getting-started.html", "Getting started" },
	{ "main-window.html", "The main window" },
	{ "receive-transmit.html", "Receiving and transmitting" },
	{ "modes-settings.html", "Modes and settings" },
	{ "sigid.html", "Signal identification (SigID)" },
	{ "waterfall.html", "The waterfall" },
	{ "macros.html", "Macros" },
	{ "logging.html", "Lookup and logging" },
	{ "nbems.html", "flmsg, flamp and flarq" },
	{ "shortcuts.html", "Keyboard shortcuts" },
	{ "troubleshooting.html", "Troubleshooting" },
};

class HelpWindow : public QWidget
{
public:
	explicit HelpWindow(QWidget* parent)
		: QWidget(parent, Qt::Window)
	{
		setObjectName(QStringLiteral("helpWindow"));
		setWindowTitle(QStringLiteral("TCIdigi Help"));
		resize(900, 640);

		auto* column = new QVBoxLayout(this);
		auto* nav = new QHBoxLayout;
		auto* back = new QPushButton(QStringLiteral("Back"));
		auto* forward = new QPushButton(QStringLiteral("Forward"));
		auto* home = new QPushButton(QStringLiteral("Contents"));
		nav->addWidget(back);
		nav->addWidget(forward);
		nav->addWidget(home);
		nav->addStretch(1);
		column->addLayout(nav);

		auto* split = new QSplitter;
		m_list = new QListWidget;
		m_list->setObjectName(QStringLiteral("helpPages"));
		for (const HelpPage& page : kPages)
			m_list->addItem(QString::fromUtf8(page.title));
		m_browser = new QTextBrowser;
		m_browser->setObjectName(QStringLiteral("helpBrowser"));
		m_browser->setSearchPaths({ QStringLiteral(":/help") });
		// Links to other help pages stay here; web links go to the browser.
		m_browser->setOpenLinks(false);
		split->addWidget(m_list);
		split->addWidget(m_browser);
		split->setStretchFactor(1, 1);
		split->setSizes({ 220, 680 });
		column->addWidget(split, 1);

		connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
			if (row >= 0 && row < (int)(sizeof kPages / sizeof kPages[0]))
				open(QString::fromUtf8(kPages[row].file));
		});
		connect(m_browser, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
			if (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https")
			    || url.scheme() == QStringLiteral("mailto")) {
				QDesktopServices::openUrl(url);
				return;
			}
			m_browser->setSource(url);
		});
		connect(m_browser, &QTextBrowser::sourceChanged, this, [this](const QUrl& url) {
			// Keep the contents list on the page shown, without reloading it.
			const QString file = url.fileName();
			for (int i = 0; i < m_list->count(); ++i)
				if (file == QString::fromUtf8(kPages[i].file) && m_list->currentRow() != i) {
					const QSignalBlocker block(m_list);
					m_list->setCurrentRow(i);
				}
		});
		connect(back, &QPushButton::clicked, m_browser, &QTextBrowser::backward);
		connect(forward, &QPushButton::clicked, m_browser, &QTextBrowser::forward);
		connect(home, &QPushButton::clicked, this, [this] { open(QStringLiteral("index.html")); });
		connect(m_browser, &QTextBrowser::backwardAvailable, back, &QWidget::setEnabled);
		connect(m_browser, &QTextBrowser::forwardAvailable, forward, &QWidget::setEnabled);
		back->setEnabled(false);
		forward->setEnabled(false);
	}

	void open(const QString& page)
	{
		const QUrl url(QStringLiteral("qrc:/help/") + page);
		if (m_browser->source() != url)
			m_browser->setSource(url);
	}

private:
	QListWidget* m_list = nullptr;
	QTextBrowser* m_browser = nullptr;
};

QPointer<HelpWindow> g_help;

} // namespace

void showHelp(QWidget* parent, const QString& page)
{
	if (!g_help)
		g_help = new HelpWindow(parent);
	g_help->open(page.isEmpty() ? QStringLiteral("index.html") : page);
	g_help->show();
	g_help->raise();
	g_help->activateWindow();
}
