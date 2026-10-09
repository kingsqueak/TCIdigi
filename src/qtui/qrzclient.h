// ----------------------------------------------------------------------------
// qrzclient.h -- QRZ callsign lookup and QRZ logbook insert
//
// Copyright (C) 2026
//
// This file is part of fldigi. GPL-3.0-or-later.
//
// Lookup uses the QRZ XML Data subscription (username and password).
// Logging uses the QRZ XML API key from My Logbook, Settings. That key is
// the QRZ Logbook API key. Nothing is written to a local fldigi log.
// ----------------------------------------------------------------------------

#ifndef FLDIGI_QTUI_QRZCLIENT_H
#define FLDIGI_QTUI_QRZCLIENT_H

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

struct QrzStation {
	QString call;
	QString name;
	QString qth;
	QString grid;
	QString country;
};

struct QrzQso {
	QString call;
	QString name;
	QString qth;
	QString grid;
	QString rstSent;
	QString rstRcvd;
	QString modem;
	QString rigMode;
	long long dialHz = 0;
	int carrierHz = 0;
	bool reverse = false;
	QString myCall;
	QString myGrid;
	QString timeOn;  // HHMMSS UTC, empty means now
	QString timeOff; // HHMMSS UTC, empty means now
	QString dateOn;  // YYYYMMDD UTC, empty means today
	QString comment; // ADIF COMMENT, empty means omit
};

class QrzClient : public QObject
{
	Q_OBJECT
public:
	explicit QrzClient(QObject* parent = nullptr);

	void setAccount(const QString& user, const QString& password, const QString& logKey);
	bool hasLogin() const;
	bool hasLogKey() const;

	void lookup(const QString& call);
	void insert(const QrzQso& qso);
	void testLogbook();

signals:
	void lookupReady(const QrzStation& station);
	// True when this call is already in the QRZ logbook. Sent after lookup.
	void workedBefore(const QString& call, bool worked);
	void logged(const QString& logId);
	void tested(const QString& detail);
	void failed(const QString& message);
	void busyChanged(bool busy);

private:
	void login();
	void fetchCall();
	void postLog(const QByteArray& adif);
	void postStatus();
	void postWorked(const QString& call);
	void finish(QNetworkReply* reply);
	void fail(const QString& message);

	QNetworkAccessManager* m_net = nullptr;
	QNetworkReply* m_reply = nullptr;
	QString m_user;
	QString m_password;
	QString m_logKey;
	QString m_session;
	QString m_call;
	QrzQso m_qso;
	enum class Job { None, Login, Fetch, Insert, Status, Worked } m_job = Job::None;
	int m_loginRetries = 0;
	bool m_busy = false;
};

#endif
