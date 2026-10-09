// ----------------------------------------------------------------------------
// qrzclient.cpp -- QRZ callsign lookup and QRZ logbook insert
//
// Copyright (C) 2026
//
// This file is part of fldigi. GPL-3.0-or-later.
// ----------------------------------------------------------------------------

#include "qrzclient.h"
#include "tcidigi_version.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QXmlStreamReader>

namespace {

const char* kXml = "https://xmldata.qrz.com/xml/current/";
const char* kLog = "https://logbook.qrz.com/api";

QString localName(const QXmlStreamReader& xml)
{
	return xml.name().toString();
}

struct XmlFields {
	QString key;
	QString error;
	QString call;
	QString fname;
	QString name;
	QString addr2;
	QString state;
	QString country;
	QString grid;
};

XmlFields readXml(const QByteArray& body)
{
	XmlFields out;
	QXmlStreamReader xml(body);
	QStringList stack;
	while (!xml.atEnd()) {
		const auto token = xml.readNext();
		if (token == QXmlStreamReader::StartElement) {
			stack.append(localName(xml));
		} else if (token == QXmlStreamReader::EndElement) {
			if (!stack.isEmpty())
				stack.removeLast();
		} else if (token == QXmlStreamReader::Characters && !xml.isWhitespace() && stack.size() >= 2) {
			const QString leaf = stack.last();
			const QString parent = stack.at(stack.size() - 2);
			const QString text = xml.text().toString().trimmed();
			if (parent == QLatin1String("Session")) {
				if (leaf == QLatin1String("Key"))
					out.key = text;
				else if (leaf == QLatin1String("Error"))
					out.error = text;
			} else if (parent == QLatin1String("Callsign")) {
				if (leaf == QLatin1String("call"))
					out.call = text;
				else if (leaf == QLatin1String("fname"))
					out.fname = text;
				else if (leaf == QLatin1String("name"))
					out.name = text;
				else if (leaf == QLatin1String("addr2"))
					out.addr2 = text;
				else if (leaf == QLatin1String("state"))
					out.state = text;
				else if (leaf == QLatin1String("country"))
					out.country = text;
				else if (leaf == QLatin1String("grid"))
					out.grid = text;
			}
		}
	}
	return out;
}

QString formValue(const QByteArray& body, const char* key)
{
	const QString text = QString::fromUtf8(body);
	const QString prefix = QString::fromLatin1(key) + QLatin1Char('=');
	for (const QString& part : text.split(QLatin1Char('&'))) {
		if (part.startsWith(prefix, Qt::CaseInsensitive))
			return QUrl::fromPercentEncoding(part.mid(prefix.size()).toUtf8()).trimmed();
	}
	return {};
}

QString normalizeCall(const QString& raw)
{
	QString out;
	for (const QChar ch : raw) {
		if (ch.isLetterOrNumber() || ch == QLatin1Char('/'))
			out.append(ch.toUpper());
	}
	while (out.contains(QLatin1Char('/'))) {
		const int slash = out.lastIndexOf(QLatin1Char('/'));
		if ((slash + 1) * 2 < out.size())
			out = out.mid(slash + 1);
		else
			out.chop(out.size() - slash);
	}
	return out;
}

QByteArray adif(const char* name, const QString& value)
{
	const QByteArray bytes = value.trimmed().toUtf8();
	if (bytes.isEmpty())
		return {};
	return QByteArray("<") + name + ":" + QByteArray::number(bytes.size()) + ">" + bytes;
}

QString bandName(long long hz)
{
	const int khz = (int)(hz / 1000);
	struct Item { int lo; int hi; const char* name; };
	static const Item items[] = {
		{1800, 2000, "160m"}, {3500, 4000, "80m"}, {5330, 5407, "60m"},
		{7000, 7300, "40m"}, {10100, 10150, "30m"}, {14000, 14350, "20m"},
		{18068, 18168, "17m"}, {21000, 21450, "15m"}, {24890, 24990, "12m"},
		{28000, 29700, "10m"}, {50000, 54000, "6m"}, {144000, 148000, "2m"},
		{222000, 225000, "1.25m"}, {420000, 450000, "70cm"}
	};
	for (const Item& item : items) {
		if (khz >= item.lo && khz <= item.hi)
			return QString::fromLatin1(item.name);
	}
	return {};
}

void adifMode(const QString& modem, QString* mode, QString* submode)
{
	const QString name = modem.trimmed().toUpper();
	*mode = name;
	*submode = {};
	if (name.contains(QLatin1String("PSK"))) {
		*mode = QStringLiteral("PSK");
		*submode = name;
		if (submode->startsWith(QLatin1String("BPSK")))
			*submode = QStringLiteral("PSK") + submode->mid(4);
	} else if (name.startsWith(QLatin1String("OLIVIA"))) {
		*mode = QStringLiteral("OLIVIA");
	} else if (name.startsWith(QLatin1String("CONTESTIA"))) {
		*mode = QStringLiteral("CONTESTIA");
	} else if (name.startsWith(QLatin1String("MFSK"))) {
		*mode = QStringLiteral("MFSK");
		*submode = name;
	} else if (name.startsWith(QLatin1String("RTTY"))) {
		*mode = QStringLiteral("RTTY");
	} else if (name.startsWith(QLatin1String("CW"))) {
		*mode = QStringLiteral("CW");
	} else if (name.startsWith(QLatin1String("DOMINO"))) {
		*mode = QStringLiteral("DOMINO");
	} else if (name.startsWith(QLatin1String("THOR"))) {
		*mode = QStringLiteral("THOR");
	} else if (name.startsWith(QLatin1String("MT63")) || name.startsWith(QLatin1String("MT-63"))) {
		*mode = QStringLiteral("MT63");
	} else if (name.contains(QLatin1String("HELL"))) {
		*mode = QStringLiteral("HELL");
	}
}

long long signalHz(const QrzQso& qso)
{
	const QString mode = qso.rigMode.trimmed().toUpper();
	bool lower = mode == QLatin1String("LSB") || mode == QLatin1String("DIGL")
		|| mode == QLatin1String("CWL");
	if (qso.reverse)
		lower = !lower;
	const long long audio = qso.carrierHz > 0 ? qso.carrierHz : 0;
	const long long hz = lower ? qso.dialHz - audio : qso.dialHz + audio;
	return hz > 0 ? hz : 0;
}

QByteArray buildAdif(const QrzQso& qso, QString* error)
{
	const QString call = normalizeCall(qso.call);
	if (call.isEmpty()) {
		*error = QStringLiteral("Enter a call sign.");
		return {};
	}
	if (normalizeCall(qso.myCall).isEmpty()) {
		*error = QStringLiteral("Enter your station call in Log, QRZ account.");
		return {};
	}
	const long long hz = signalHz(qso);
	if (hz <= 0) {
		*error = QStringLiteral("No frequency from the radio.");
		return {};
	}
	const QDateTime now = QDateTime::currentDateTimeUtc();
	const QString date = qso.dateOn.isEmpty() ? now.toString(QStringLiteral("yyyyMMdd")) : qso.dateOn;
	const QString timeOn = qso.timeOn.isEmpty() ? now.toString(QStringLiteral("HHmmss")) : qso.timeOn;
	const QString timeOff = qso.timeOff.isEmpty() ? now.toString(QStringLiteral("HHmmss")) : qso.timeOff;
	QString mode;
	QString submode;
	adifMode(qso.modem, &mode, &submode);
	const double mhz = (double)hz / 1.0e6;

	QByteArray record;
	record += adif("CALL", call);
	record += adif("QSO_DATE", date);
	record += adif("TIME_ON", timeOn);
	record += adif("TIME_OFF", timeOff);
	record += adif("BAND", bandName(hz));
	record += adif("FREQ", QString::number(mhz, 'f', 6));
	record += adif("MODE", mode);
	record += adif("SUBMODE", submode);
	record += adif("RST_SENT", qso.rstSent);
	record += adif("RST_RCVD", qso.rstRcvd);
	record += adif("NAME", qso.name);
	record += adif("QTH", qso.qth);
	record += adif("GRIDSQUARE", qso.grid);
	record += adif("STATION_CALLSIGN", normalizeCall(qso.myCall));
	record += adif("MY_GRIDSQUARE", qso.myGrid.trimmed().toUpper());
	record += adif("COMMENT", qso.comment);
	record += "<EOR>";
	return record;
}

bool sessionRejected(const QString& error)
{
	const QString text = error.toLower();
	return text.contains(QLatin1String("session"))
		|| text.contains(QLatin1String("password"))
		|| text.contains(QLatin1String("username"));
}

} // namespace

QrzClient::QrzClient(QObject* parent)
	: QObject(parent)
	, m_net(new QNetworkAccessManager(this))
{
}

void QrzClient::setAccount(const QString& user, const QString& password, const QString& logKey)
{
	const QString nextUser = user.trimmed();
	if (nextUser.compare(m_user, Qt::CaseInsensitive) != 0 || password != m_password)
		m_session.clear();
	m_user = nextUser;
	m_password = password;
	m_logKey = logKey.trimmed();
}

bool QrzClient::hasLogin() const
{
	return !m_user.isEmpty() && !m_password.isEmpty();
}

bool QrzClient::hasLogKey() const
{
	return !m_logKey.isEmpty();
}

void QrzClient::fail(const QString& message)
{
	m_job = Job::None;
	m_busy = false;
	emit busyChanged(false);
	emit failed(message);
}

void QrzClient::lookup(const QString& call)
{
	if (m_busy)
		return;
	m_call = normalizeCall(call);
	if (m_call.isEmpty()) {
		emit failed(QStringLiteral("Enter a call sign."));
		return;
	}
	if (!hasLogin()) {
		emit failed(QStringLiteral("Set the QRZ username and password in Log, QRZ account."));
		return;
	}
	m_loginRetries = 0;
	m_busy = true;
	emit busyChanged(true);
	if (m_session.isEmpty())
		login();
	else
		fetchCall();
}

void QrzClient::insert(const QrzQso& qso)
{
	if (m_busy)
		return;
	if (!hasLogKey()) {
		emit failed(QStringLiteral("Set the QRZ XML API key in Log, QRZ account."));
		return;
	}
	QString error;
	const QByteArray record = buildAdif(qso, &error);
	if (record.isEmpty()) {
		emit failed(error);
		return;
	}
	m_qso = qso;
	m_busy = true;
	emit busyChanged(true);
	postLog(record);
}

void QrzClient::testLogbook()
{
	if (m_busy)
		return;
	if (!hasLogKey()) {
		emit failed(QStringLiteral("Enter the QRZ XML API key."));
		return;
	}
	m_busy = true;
	emit busyChanged(true);
	postStatus();
}

void QrzClient::login()
{
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("username"), m_user);
	query.addQueryItem(QStringLiteral("password"), m_password);
	query.addQueryItem(QStringLiteral("agent"),
		QString::fromUtf8(TCIDIGI_NAME "-" TCIDIGI_VERSION));
	QUrl url(QString::fromLatin1(kXml));
	url.setQuery(query);
	QNetworkRequest request(url);
	request.setTransferTimeout(15000);
	m_job = Job::Login;
	m_reply = m_net->get(request);
	connect(m_reply, &QNetworkReply::finished, this, [this] { finish(m_reply); });
}

void QrzClient::fetchCall()
{
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("s"), m_session);
	query.addQueryItem(QStringLiteral("callsign"), m_call);
	QUrl url(QString::fromLatin1(kXml));
	url.setQuery(query);
	QNetworkRequest request(url);
	request.setTransferTimeout(15000);
	m_job = Job::Fetch;
	m_reply = m_net->get(request);
	connect(m_reply, &QNetworkReply::finished, this, [this] { finish(m_reply); });
}

void QrzClient::postLog(const QByteArray& adifRecord)
{
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("KEY"), m_logKey);
	query.addQueryItem(QStringLiteral("ACTION"), QStringLiteral("INSERT"));
	query.addQueryItem(QStringLiteral("ADIF"), QString::fromUtf8(adifRecord));
	QNetworkRequest request(QUrl(QString::fromLatin1(kLog)));
	request.setHeader(QNetworkRequest::ContentTypeHeader,
		QStringLiteral("application/x-www-form-urlencoded"));
	request.setTransferTimeout(20000);
	m_job = Job::Insert;
	m_reply = m_net->post(request, query.query(QUrl::FullyEncoded).toUtf8());
	connect(m_reply, &QNetworkReply::finished, this, [this] { finish(m_reply); });
}

void QrzClient::postWorked(const QString& call)
{
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("KEY"), m_logKey);
	query.addQueryItem(QStringLiteral("ACTION"), QStringLiteral("FETCH"));
	query.addQueryItem(QStringLiteral("OPTION"),
		QStringLiteral("CALL:%1,MAX:0").arg(call));
	QNetworkRequest request(QUrl(QString::fromLatin1(kLog)));
	request.setHeader(QNetworkRequest::ContentTypeHeader,
		QStringLiteral("application/x-www-form-urlencoded"));
	request.setTransferTimeout(15000);
	m_job = Job::Worked;
	m_reply = m_net->post(request, query.query(QUrl::FullyEncoded).toUtf8());
	connect(m_reply, &QNetworkReply::finished, this, [this] { finish(m_reply); });
}

void QrzClient::postStatus()
{
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("KEY"), m_logKey);
	query.addQueryItem(QStringLiteral("ACTION"), QStringLiteral("STATUS"));
	QNetworkRequest request(QUrl(QString::fromLatin1(kLog)));
	request.setHeader(QNetworkRequest::ContentTypeHeader,
		QStringLiteral("application/x-www-form-urlencoded"));
	request.setTransferTimeout(15000);
	m_job = Job::Status;
	m_reply = m_net->post(request, query.query(QUrl::FullyEncoded).toUtf8());
	connect(m_reply, &QNetworkReply::finished, this, [this] { finish(m_reply); });
}

void QrzClient::finish(QNetworkReply* reply)
{
	if (!reply)
		return;
	const Job job = m_job;
	const QByteArray body = reply->readAll();
	const bool netFail = reply->error() != QNetworkReply::NoError;
	const QString netError = reply->errorString();
	reply->deleteLater();
	if (m_reply == reply)
		m_reply = nullptr;

	if (netFail) {
		if (job == Job::Worked) {
			m_job = Job::None;
			m_busy = false;
			emit busyChanged(false);
			emit workedBefore(m_call, false);
			return;
		}
		if (job == Job::Fetch && m_loginRetries < 1) {
			m_session.clear();
			m_loginRetries++;
			login();
			return;
		}
		fail(netError);
		return;
	}

	if (job == Job::Login || job == Job::Fetch) {
		const XmlFields xml = readXml(body);
		if (job == Job::Login) {
			if (xml.key.isEmpty()) {
				fail(xml.error.isEmpty()
					? QStringLiteral("QRZ did not return a session.")
					: xml.error);
				return;
			}
			m_session = xml.key;
			fetchCall();
			return;
		}
		if (!xml.call.isEmpty()) {
			QrzStation station;
			station.call = xml.call.toUpper();
			station.name = xml.fname.trimmed();
			if (!xml.name.trimmed().isEmpty()) {
				if (!station.name.isEmpty())
					station.name += QLatin1Char(' ');
				station.name += xml.name.trimmed();
			}
			station.qth = xml.addr2.trimmed();
			if (!xml.state.trimmed().isEmpty()) {
				if (!station.qth.isEmpty())
					station.qth += QStringLiteral(", ");
				station.qth += xml.state.trimmed();
			}
			station.grid = xml.grid.trimmed().toUpper();
			station.country = xml.country.trimmed();
			emit lookupReady(station);
			if (hasLogKey()) {
				postWorked(station.call);
				return;
			}
			m_job = Job::None;
			m_busy = false;
			emit busyChanged(false);
			return;
		}
		if (sessionRejected(xml.error) && m_loginRetries < 1) {
			m_session.clear();
			m_loginRetries++;
			login();
			return;
		}
		fail(xml.error.isEmpty() ? QStringLiteral("Call sign not found on QRZ.") : xml.error);
		return;
	}

	if (job == Job::Worked) {
		const int count = formValue(body, "COUNT").toInt();
		m_job = Job::None;
		m_busy = false;
		emit busyChanged(false);
		emit workedBefore(m_call, count > 0);
		return;
	}

	const QString result = formValue(body, "RESULT");
	if (result.compare(QLatin1String("OK"), Qt::CaseInsensitive) != 0) {
		const QString reason = formValue(body, "REASON");
		fail(reason.isEmpty() ? QStringLiteral("QRZ rejected the request.") : reason);
		return;
	}
	m_job = Job::None;
	m_busy = false;
	emit busyChanged(false);
	if (job == Job::Insert) {
		emit logged(formValue(body, "LOGID"));
		return;
	}
	const QString count = formValue(body, "COUNT");
	QString detail = QStringLiteral("QRZ XML API key accepted.");
	if (!count.isEmpty())
		detail += QStringLiteral(" ") + count + QStringLiteral(" records.");
	emit tested(detail);
}
