// ----------------------------------------------------------------------------
// eqslclient.h -- eQSL.cc upload of a logged contact
//
// Copyright (C) 2026
//
// This file is part of fldigi. GPL-3.0-or-later.
//
// The upload is the one fldigi's logger makes: a single ADIF record with
// the eQSL user ID and password, sent to importADIF.cfm. The QSL message
// is the default message with {CALL}, {NAME} and {MODE} filled in.
// ----------------------------------------------------------------------------

#ifndef FLDIGI_QTUI_EQSLCLIENT_H
#define FLDIGI_QTUI_EQSLCLIENT_H

#include "qrzclient.h"

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

class EqslClient : public QObject
{
	Q_OBJECT
public:
	explicit EqslClient(QObject* parent = nullptr);

	void setAccount(const QString& user, const QString& password,
		const QString& nickname, const QString& message);
	bool hasLogin() const;
	bool busy() const { return m_reply != nullptr; }

	void upload(const QrzQso& qso);

signals:
	// detail is eQSL's result line, such as "1 out of 1 records added".
	void sent(const QString& detail);
	void failed(const QString& message);
	void busyChanged(bool busy);

private:
	void finish(QNetworkReply* reply);

	QNetworkAccessManager* m_net = nullptr;
	QNetworkReply* m_reply = nullptr;
	QString m_user;
	QString m_password;
	QString m_nickname;
	QString m_message;
};

#endif
