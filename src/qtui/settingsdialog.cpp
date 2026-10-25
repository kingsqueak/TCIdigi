// ----------------------------------------------------------------------------
// settingsdialog.cpp -- fldigi settings pages in the Qt shell
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

#include "settingsdialog.h"

#include "engine.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <cmath>
#include <functional>
#include <memory>
#include <utility>

namespace {

// A control on a page and how to apply it: Save calls apply on each, in
// order, and apply sends the value only when it changed.
struct Control {
	std::function<void()> apply;
};

QString itemLabel(const SettingItem& item, const EnginePref& pref)
{
	if (item.label)
		return QString::fromUtf8(item.label);
	return QString::fromUtf8(pref.label);
}

QString itemTip(const SettingItem& item, const EnginePref& pref)
{
	return QString::fromUtf8(item.tip ? item.tip : pref.tip);
}

// Decimal places one number needs: 3 -> 0, 2.5 -> 1, 0.01 -> 2.
int placesOf(double v)
{
	for (int places = 0; places < 4; ++places) {
		const double scaled = v * std::pow(10.0, places);
		if (std::fabs(scaled - std::round(scaled)) < 1e-6)
			return places;
	}
	return 4;
}

// Decimal places a setting needs: enough for its step and its limits. A
// valuator without a step is continuous (FLTK's 0..1 sliders): 2 places.
int decimalsFor(const EnginePref& pref)
{
	if (pref.step <= 0)
		return 2;
	int places = placesOf(pref.step);
	if (pref.lo > -1e8)
		places = std::max(places, placesOf(pref.lo));
	if (pref.hi < 1e8)
		places = std::max(places, placesOf(pref.hi));
	return places;
}

// Applies a "key=value" rule from SettingItem::needs: is the control
// named key on the page set that way?
bool needsMet(QWidget* page, const QString& rule)
{
	const int eq = rule.indexOf(QLatin1Char('='));
	if (eq < 0)
		return true;
	const QString key = rule.left(eq);
	const QString want = rule.mid(eq + 1);
	if (auto* box = page->findChild<QAbstractButton*>(key))
		return box->isChecked() == (want == QStringLiteral("1"));
	if (auto* combo = page->findChild<QComboBox*>(key)) {
		if (want == QStringLiteral("last"))
			return combo->currentIndex() == combo->count() - 1;
		return combo->currentText().compare(want, Qt::CaseInsensitive) == 0;
	}
	return true;
}

// The modes of one mode set, ticked in a list with All and None.
void chooseModes(QWidget* parent, const QString& title, QString& flags)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(title);
	dialog.setMinimumSize(360, 480);
	auto* column = new QVBoxLayout(&dialog);
	auto* list = new QListWidget;
	const int count = flags.size();
	for (int i = 0; i < count; ++i) {
		char label[128] = { 0 };
		engine_mode_set_label(i, label, (int)sizeof label);
		auto* row = new QListWidgetItem(QString::fromUtf8(label), list);
		row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
		row->setCheckState(i < flags.size() && flags.at(i) == QLatin1Char('1') ? Qt::Checked : Qt::Unchecked);
	}
	column->addWidget(list, 1);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto* all = buttons->addButton(QStringLiteral("All"), QDialogButtonBox::ActionRole);
	auto* none = buttons->addButton(QStringLiteral("None"), QDialogButtonBox::ActionRole);
	const auto tickAll = [list](Qt::CheckState state) {
		for (int i = 0; i < list->count(); ++i)
			list->item(i)->setCheckState(state);
	};
	QObject::connect(all, &QPushButton::clicked, &dialog, [tickAll] { tickAll(Qt::Checked); });
	QObject::connect(none, &QPushButton::clicked, &dialog, [tickAll] { tickAll(Qt::Unchecked); });
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	column->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return;
	QString next;
	for (int i = 0; i < list->count(); ++i)
		next += list->item(i)->checkState() == Qt::Checked ? QLatin1Char('1') : QLatin1Char('0');
	flags = next;
}

QString modeCountText(const QString& flags)
{
	return QStringLiteral("%1 of %2 modes…").arg(flags.count(QLatin1Char('1'))).arg(flags.size());
}

// One page: a scrolling form of its items. Adds what Save must do to
// controls.
QWidget* buildPage(const SettingPage& page, std::vector<Control>& controls)
{
	auto* inner = new QWidget;
	auto* form = new QFormLayout(inner);
	form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
	QButtonGroup* radios = nullptr;
	bool oneOf = false;
	std::vector<std::pair<QWidget*, QString>> rules;
	// The check boxes of the current AtMostOne run.
	std::shared_ptr<std::vector<QCheckBox*>> atMostOne;

	for (const SettingItem& item : page.items) {
		if (item.kind != SettingItem::Pref) {
			radios = nullptr;
			oneOf = false;
			atMostOne.reset();
		}
		if (item.kind == SettingItem::OneOf) {
			oneOf = true;
			continue;
		}
		if (item.kind == SettingItem::AtMostOne) {
			atMostOne = std::make_shared<std::vector<QCheckBox*>>();
			continue;
		}
		if (item.kind == SettingItem::EndGroup)
			continue;
		if (item.kind == SettingItem::Heading) {
			auto* heading = new QLabel(QString::fromUtf8(item.label));
			QFont bold = heading->font();
			bold.setBold(true);
			heading->setFont(bold);
			heading->setContentsMargins(0, form->rowCount() ? 10 : 0, 0, 2);
			form->addRow(heading);
			continue;
		}
		if (item.kind == SettingItem::Note) {
			auto* note = new QLabel(QString::fromUtf8(item.label));
			note->setWordWrap(true);
			note->setForegroundRole(QPalette::PlaceholderText);
			form->addRow(note);
			continue;
		}
		if (item.kind == SettingItem::ModeSet) {
			const QString name = QString::fromUtf8(item.key);
			char raw[1024] = { 0 };
			if (!engine_mode_set(item.key, raw, (int)sizeof raw))
				continue;
			auto flags = std::make_shared<QString>(QString::fromLatin1(raw));
			const QString start = *flags;
			auto* button = new QPushButton(modeCountText(*flags));
			if (item.tip)
				button->setToolTip(QString::fromUtf8(item.tip));
			const QString title = QString::fromUtf8(item.label);
			QObject::connect(button, &QPushButton::clicked, button, [button, flags, title] {
				chooseModes(button->window(), title, *flags);
				button->setText(modeCountText(*flags));
			});
			form->addRow(title, button);
			const QByteArray key = name.toUtf8();
			controls.push_back({ [flags, start, key] {
				if (*flags != start)
					engine_set_mode_set(key.constData(), flags->toLatin1().constData());
			} });
			continue;
		}

		EnginePref pref;
		if (!engine_pref(item.key, &pref)) {
			qWarning("settings: no widget %s", item.key);
			continue;
		}
		const QString label = itemLabel(item, pref);
		const QString tip = itemTip(item, pref);
		const QByteArray key(item.key);
		QWidget* field = nullptr;

		// A group of check boxes ends at the first setting of another kind.
		if (pref.kind != ENGINE_PREF_CHECK) {
			oneOf = false;
			atMostOne.reset();
		}
		// Check boxes fldigi keeps to one of are radio buttons here.
		if (oneOf && pref.kind == ENGINE_PREF_CHECK)
			pref.kind = ENGINE_PREF_RADIO;
		if (pref.kind != ENGINE_PREF_RADIO)
			radios = nullptr;
		switch (pref.kind) {
		case ENGINE_PREF_CHECK: {
			auto* box = new QCheckBox(label);
			box->setChecked(pref.value != 0);
			const bool start = box->isChecked();
			controls.push_back({ [box, start, key] {
				if (box->isChecked() != start)
					engine_set_pref(key.constData(), box->isChecked() ? 1 : 0, nullptr);
			} });
			if (atMostOne) {
				atMostOne->push_back(box);
				QObject::connect(box, &QCheckBox::toggled, box, [box, group = atMostOne](bool on) {
					if (!on)
						return;
					for (QCheckBox* other : *group)
						if (other != box)
							other->setChecked(false);
				});
			}
			field = box;
			form->addRow(box);
			break;
		}
		case ENGINE_PREF_RADIO: {
			// Radio buttons in a row are one choice.
			if (!radios) {
				radios = new QButtonGroup(inner);
				radios->setExclusive(true);
			}
			auto* button = new QRadioButton(label);
			radios->addButton(button);
			button->setChecked(pref.value != 0);
			const bool start = button->isChecked();
			controls.push_back({ [button, start, key] {
				if (button->isChecked() && !start)
					engine_set_pref(key.constData(), 1, nullptr);
			} });
			field = button;
			form->addRow(button);
			break;
		}
		case ENGINE_PREF_NUMBER: {
			const QString suffix = item.suffix ? QString::fromUtf8(item.suffix) : QString();
			const int places = decimalsFor(pref);
			if (places == 0) {
				auto* spin = new QSpinBox;
				spin->setRange((int)std::max(pref.lo, -1e9), (int)std::min(pref.hi, 1e9));
				spin->setSingleStep(std::max(1, (int)std::lround(pref.step)));
				spin->setSuffix(suffix);
				spin->setValue((int)std::lround(pref.value));
				const int start = spin->value();
				controls.push_back({ [spin, start, key] {
					if (spin->value() != start)
						engine_set_pref(key.constData(), spin->value(), nullptr);
				} });
				field = spin;
			} else {
				auto* spin = new QDoubleSpinBox;
				spin->setDecimals(places);
				spin->setRange(pref.lo, pref.hi);
				spin->setSingleStep(pref.step > 0 ? pref.step : std::pow(10.0, -places));
				spin->setSuffix(suffix);
				spin->setValue(pref.value);
				const double start = spin->value();
				controls.push_back({ [spin, start, key] {
					if (spin->value() != start)
						engine_set_pref(key.constData(), spin->value(), nullptr);
				} });
				field = spin;
			}
			form->addRow(label, field);
			break;
		}
		case ENGINE_PREF_CHOICE: {
			auto* combo = new QComboBox;
			const QStringList items = QString::fromUtf8(pref.choices).split(QLatin1Char('\n'));
			for (int i = 0; i + 1 < items.size() || (i < items.size() && !items.at(i).isEmpty()); ++i)
				combo->addItem(items.at(i));
			combo->setCurrentIndex((int)pref.value);
			const int start = combo->currentIndex();
			controls.push_back({ [combo, start, key] {
				if (combo->currentIndex() != start)
					engine_set_pref(key.constData(), combo->currentIndex(), nullptr);
			} });
			field = combo;
			form->addRow(label, field);
			break;
		}
		case ENGINE_PREF_TEXT: {
			auto* edit = new QLineEdit(QString::fromUtf8(pref.text));
			edit->setMinimumWidth(260);
			const QString start = edit->text();
			controls.push_back({ [edit, start, key] {
				if (edit->text() != start)
					engine_set_pref(key.constData(), 0, edit->text().toUtf8().constData());
			} });
			field = edit;
			form->addRow(label, field);
			break;
		}
		default:
			break;
		}
		if (field) {
			field->setObjectName(QString::fromUtf8(item.key));
			if (!tip.isEmpty())
				field->setToolTip(tip);
			field->setEnabled(pref.active != 0);
			if (item.needs)
				rules.push_back({ field, QString::fromUtf8(item.needs) });
		}
	}

	// Settings that depend on another follow it while the page is open,
	// as on fldigi's page.
	for (const auto& rule : rules) {
		QWidget* target = rule.first;
		const QString key = rule.second.left(rule.second.indexOf(QLatin1Char('=')));
		const auto update = [inner, target, rule] { target->setEnabled(needsMet(inner, rule.second)); };
		update();
		if (auto* box = inner->findChild<QAbstractButton*>(key))
			QObject::connect(box, &QAbstractButton::toggled, target, update);
		else if (auto* combo = inner->findChild<QComboBox*>(key))
			QObject::connect(combo, &QComboBox::currentIndexChanged, target, update);
	}

	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidget(inner);
	return scroll;
}

} // namespace

void editSettings(QWidget* parent, const QString& title, const std::vector<SettingPage>& pages)
{
	if (!engine_up()) {
		QMessageBox::information(parent, title, QStringLiteral("The modem is not running yet."));
		return;
	}
	QDialog dialog(parent);
	dialog.setObjectName(QStringLiteral("settingsDialog"));
	dialog.setWindowTitle(title);
	dialog.resize(760, 560);
	auto* column = new QVBoxLayout(&dialog);
	auto* row = new QHBoxLayout;
	auto* list = new QListWidget;
	list->setObjectName(QStringLiteral("settingsPages"));
	list->setMaximumWidth(200);
	auto* stack = new QStackedWidget;
	std::vector<Control> controls;
	for (const SettingPage& page : pages) {
		list->addItem(QString::fromUtf8(page.title));
		stack->addWidget(buildPage(page, controls));
	}
	QObject::connect(list, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
	list->setCurrentRow(0);
	row->addWidget(list);
	row->addWidget(stack, 1);
	column->addLayout(row, 1);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close);
	column->addWidget(buttons);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
		for (const Control& control : controls)
			control.apply();
		engine_save_prefs();
		dialog.accept();
	});
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	dialog.exec();
}
