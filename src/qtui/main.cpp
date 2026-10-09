// ----------------------------------------------------------------------------
// main.cpp -- Qt shell entry point
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

#include "engine.h"
#include "mainwindow.h"
#include "theme.h"
#include "tcidigi_version.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QSettings>
#include <QTimer>

#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

namespace {

void usage()
{
	std::fputs(
		"TCIdigi — Qt interface for fldigi\n"
		"\n"
		"  --appearance MODE   system, light, or dark for this run only\n"
		"  --exit-after N      quit after N seconds\n"
		"  --config-dir DIR    configuration directory (default ~/.tcidigi)\n"
		"  --help              show this help\n"
		"\n"
		"Other arguments are passed to the fldigi engine.\n"
		"A config directory containing \"fldigi-bak\" is refused.\n"
		"The default directory is ~/.tcidigi. This program does not use ~/.fldigi.\n",
		stdout);
}

bool contains_backup(const std::string& path)
{
	std::string lower = path;
	for (char& ch : lower) {
		if (ch >= 'A' && ch <= 'Z')
			ch = static_cast<char>(ch - 'A' + 'a');
	}
	return lower.find("fldigi-bak") != std::string::npos;
}

QString clean_dir(const QString& path)
{
	return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

} // namespace

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);
	QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/TCIdigi.png")));
	// The Dock name is the bundle name TCIdigi. Organization is the
	// configuration directory's own name so QSettings writes tcidigi.ini
	// directly inside that directory. It is set once the directory is known.
	QApplication::setApplicationDisplayName(QStringLiteral("TCIdigi"));
	QCoreApplication::setApplicationVersion(QString::fromUtf8(TCIDIGI_VERSION));
	QSettings::setDefaultFormat(QSettings::IniFormat);

	std::string appearance;
	int exit_after = 0;
	bool saw_config = false;
	QString requested;
	static std::vector<std::string> storage;
	storage.clear();
	storage.emplace_back(argc > 0 && argv[0] ? argv[0] : "TCIdigi");

	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i] ? argv[i] : "";
		if (arg == "--help" || arg == "-h") {
			usage();
			return 0;
		}
		if (arg == "--appearance" && i + 1 < argc) {
			appearance = argv[++i];
			continue;
		}
		if (arg.compare(0, 13, "--appearance=") == 0) {
			appearance = arg.substr(13);
			continue;
		}
		if (arg == "--exit-after" && i + 1 < argc) {
			exit_after = std::atoi(argv[++i]);
			continue;
		}
		if (arg.compare(0, 13, "--exit-after=") == 0) {
			exit_after = std::atoi(arg.c_str() + 13);
			continue;
		}
		if (arg == "--config-dir" && i + 1 < argc) {
			requested = QString::fromStdString(argv[++i]);
			if (contains_backup(requested.toStdString())) {
				std::fputs("refusing a config directory that contains fldigi-bak\n", stderr);
				return 2;
			}
			saw_config = true;
			continue;
		}
		if (arg.compare(0, 13, "--config-dir=") == 0) {
			requested = QString::fromStdString(arg.substr(13));
			if (contains_backup(requested.toStdString())) {
				std::fputs("refusing a config directory that contains fldigi-bak\n", stderr);
				return 2;
			}
			saw_config = true;
			continue;
		}
		if (arg == "--home-dir" && i + 1 < argc) {
			const std::string dir = argv[++i];
			if (contains_backup(dir)) {
				std::fputs("refusing a home directory that contains fldigi-bak\n", stderr);
				return 2;
			}
			storage.emplace_back("--home-dir");
			storage.emplace_back(dir);
			continue;
		}
		if (contains_backup(arg)) {
			std::fputs("refusing an argument that contains fldigi-bak\n", stderr);
			return 2;
		}
		storage.emplace_back(arg);
	}

	const QString homeConfig = clean_dir(QDir::homePath() + QStringLiteral("/.tcidigi"));
	const QString configDir = saw_config ? clean_dir(requested) : homeConfig;
	if (configDir.isEmpty() || contains_backup(configDir.toStdString())) {
		std::fputs("no safe default config directory\n", stderr);
		return 2;
	}
	if (!QDir().mkpath(configDir)) {
		std::fputs("could not create the configuration directory\n", stderr);
		return 2;
	}
	// Parent + directory name puts tcidigi.ini directly in the config
	// directory. A throwaway --config-dir therefore keeps its own ini.
	const QFileInfo configInfo(configDir);
	QCoreApplication::setOrganizationName(configInfo.fileName());
	QCoreApplication::setApplicationName(QStringLiteral("tcidigi"));
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, configInfo.absolutePath());
	// A missing directory was just created. It stays empty. Operator
	// details are asked for by the station dialog. Nothing is read from
	// ~/.fldigi or from an older fldigi preferences file.

	storage.emplace_back("--config-dir");
	storage.emplace_back(configDir.toStdString());

	std::vector<char*> forwarded;
	forwarded.reserve(storage.size() + 1);
	for (std::string& item : storage)
		forwarded.push_back(item.data());
	forwarded.push_back(nullptr);

	if (!engine_start(static_cast<int>(forwarded.size()) - 1, forwarded.data()))
		std::fputs("fldigi engine did not start; the window will stay disconnected\n", stderr);

	Theme theme;
	if (!appearance.empty())
		theme.setMode(Theme::fromString(QString::fromStdString(appearance)), false);

	MainWindow window(&theme);
	window.show();

	if (exit_after > 0)
		QTimer::singleShot(exit_after * 1000, &app, &QCoreApplication::quit);

	return app.exec();
}
