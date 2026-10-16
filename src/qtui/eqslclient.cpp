// ----------------------------------------------------------------------------
// eqslclient.cpp -- eQSL.cc upload of a logged contact
//
// Copyright (C) 2026
//
// This file is part of fldigi. GPL-3.0-or-later.
//
// Follows submit_eQSL in src/logger/logger.cxx.
// ----------------------------------------------------------------------------

#include "eqslclient.h"
#include "tcidigi_version.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

using namespace qsoadif;

namespace {

const char* kImport = "https://www.eqsl.cc/qslcard/importADIF.cfm";

// The default message with {CALL}, {NAME} and {MODE} filled in, as fldigi
// does. {MODE} is MODE/SUBMODE when there is a submode.
QString qslMessage(QString message, const QrzQso& qso, const QString& mode, const QString& submode)
{
	message.replace(QLatin1String("{CALL}"), qso.call.trimmed().toUpper());
	message.replace(QLatin1String("{NAME}"), qso.name.trimmed());
	message.replace(QLatin1String("{MODE}"),
		submode.isEmpty() ? mode : mode + QLatin1Char('/') + submode);
	return message.trimmed();
}

// eQSL answers with an HTML page. The lines that matter start with
// "Result:", "Error:", "Warning:" or "Caution:".
QStringList replyLines(const QByteArray& body)
{
	QString text = QString::fromUtf8(body);
	text.replace(QRegularExpression(QStringLiteral("<[^>]*>")), QStringLiteral("\n"));
	QStringList lines;
	for (const QString& raw : text.split(QLatin1Char('\n'))) {
		const QString line = raw.simplified();
		if (line.startsWith(QLatin1String("Result:"))
			|| line.startsWith(QLatin1String("Error:"))
			|| line.startsWith(QLatin1String("Warning:"))
			|| line.startsWith(QLatin1String("Caution:")))
			lines.append(line);
	}
	return lines;
}

} // namespace

EqslClient::EqslClient(QObject* parent)
	: QObject(parent)
	, m_net(new QNetworkAccessManager(this))
{
}

void EqslClient::setAccount(const QString& user, const QString& password,
	const QString& nickname, const QString& message)
{
	m_user = user.trimmed().toUpper();
	m_password = password;
	m_nickname = nickname.trimmed();
	m_message = message;
}

bool EqslClient::hasLogin() const
{
	return !m_user.isEmpty() && !m_password.isEmpty();
}

void EqslClient::upload(const QrzQso& qso)
{
	if (m_reply)
		return;
	if (!hasLogin()) {
		emit failed(QStringLiteral("Set the eQSL user ID and password in Log, eQSL account."));
		return;
	}
	const QString call = qso.call.trimmed().toUpper();
	if (call.isEmpty()) {
		emit failed(QStringLiteral("Enter a call sign."));
		return;
	}
	const long long hz = signalHz(qso);
	if (hz <= 0) {
		emit failed(QStringLiteral("No frequency from the radio."));
		return;
	}
	const QDateTime now = QDateTime::currentDateTimeUtc();
	const QString date = qso.dateOn.isEmpty() ? now.toString(QStringLiteral("yyyyMMdd")) : qso.dateOn;
	// eQSL matches on HHMM.
	const QString time = (qso.timeOn.isEmpty() ? now.toString(QStringLiteral("HHmmss")) : qso.timeOn).left(4);
	QString mode;
	QString submode;
	adifMode(qso.modem, &mode, &submode);

	QByteArray data = "upload ";
	data += adif("ADIF_VER", QStringLiteral("3.1.4"));
	data += adif("EQSL_USER", m_user);
	data += adif("EQSL_PSWD", m_password);
	data += adif("PROGRAMID", QStringLiteral(TCIDIGI_NAME));
	data += "<EOH>";
	data += adif("APP_EQSL_QTH_NICKNAME", m_nickname);
	data += adif("CALL", call);
	data += adif("MODE", mode);
	data += adif("SUBMODE", submode);
	data += adif("FREQ", QString::number((double)hz / 1.0e6, 'f', 4));
	data += adif("BAND", bandName(hz));
	data += adif("QSO_DATE", date);
	data += adif("TIME_ON", time);
	data += adif("RST_SENT", qso.rstSent);
	data += adif("QSLMSG", qslMessage(m_message, qso, mode, submode));
	data += "<EOR>";

	QUrl url(QString::fromLatin1(kImport));
	url.setQuery(QStringLiteral("ADIFdata=")
		+ QString::fromLatin1(QUrl::toPercentEncoding(QString::fromUtf8(data))), QUrl::StrictMode);
	QNetworkRequest request(url);
	request.setTransferTimeout(20000);
	m_reply = m_net->get(request);
	emit busyChanged(true);
	connect(m_reply, &QNetworkReply::finished, this, [this] { finish(m_reply); });
}

void EqslClient::finish(QNetworkReply* reply)
{
	if (!reply)
		return;
	const QByteArray body = reply->readAll();
	const bool netFail = reply->error() != QNetworkReply::NoError;
	const QString netError = reply->errorString();
	reply->deleteLater();
	if (m_reply == reply)
		m_reply = nullptr;
	emit busyChanged(false);

	if (netFail) {
		emit failed(QStringLiteral("Cannot reach eQSL: ") + netError);
		return;
	}
	const QStringList lines = replyLines(body);
	QString result;
	QString problem;
	for (const QString& line : lines) {
		if (line.startsWith(QLatin1String("Result:")) && result.isEmpty())
			result = line.mid(7).trimmed();
		else if (problem.isEmpty())
			problem = line;
	}
	if (problem.startsWith(QLatin1String("Error:"))) {
		emit failed(QStringLiteral("eQSL ") + problem);
		return;
	}
	if (problem.contains(QLatin1String("Duplicate"), Qt::CaseInsensitive)) {
		emit failed(QStringLiteral("eQSL already has this contact."));
		return;
	}
	if (result.isEmpty() || result.startsWith(QLatin1String("0 "))) {
		emit failed(problem.isEmpty()
			? QStringLiteral("eQSL did not take the contact.")
			: QStringLiteral("eQSL ") + problem);
		return;
	}
	emit sent(result);
}
