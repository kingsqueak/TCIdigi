// ----------------------------------------------------------------------------
// logger.cxx Remote Log Interface for fldigi
//
// Copyright 2006-2009 W1HKJ, Dave Freese
//
// This file is part of fldigi.
//
// Fldigi is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Fldigi is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with fldigi.  If not, see <http://www.gnu.org/licenses/>.
// ----------------------------------------------------------------------------

#include <config.h>

#include <sys/types.h>
#include <sys/stat.h>
#if !defined(__WOE32__) && !defined(__APPLE__)
#  include <sys/ipc.h>
#  include <sys/msg.h>
#endif
#include <errno.h>
#include <string>
#include <fstream>
#include <vector>

#include <FL/Fl_Preferences.H>

#include "icons.h"
#include "logger.h"
#include "lgbook.h"
#include "main.h"
#include "modem.h"
#include "waterfall.h"
#include "fl_digi.h"
#include "trx.h"
#include "debug.h"
#include "macros.h"
#include "status.h"
#include "spot.h"
#include "adif_io.h"
#include "date.h"
#include "configuration.h"
#include "flmisc.h"
#include "strutil.h"
#include "qrunner.h"

#include "logsupport.h"
#include "lookupcall.h"
#include "fd_logger.h"

#include "socket.h"

#include "n3fjp_logger.h"

#include "dx_cluster.h"

#include "network.h"

#include <FL/fl_ask.H>

//======================================================================
const char *logmode;

static std::string log_msg;
static std::string errmsg;
static std::string notes;
//======================================================================

std::string comma2period(std::string in)
{
	for (size_t n = 0; n < in.length(); n++)
		if (in[n] == ',') in[n] = '.';
	return in;
}

//======================================================================
// LoTW
static std::string lotw_fname;
static std::string lotw_logfname;
static std::string lotw_send_fname;
static std::string lotw_log_fname;
static int tracefile_timeout = 0;

std::string str_lotw;
static std::string adif;

void writeADIF () {
// open the adif file
	FILE *adiFile;
	std::string fname;

    fname = TempDir;
    fname.append("log.adif");
	adiFile = fl_fopen (fname.c_str(), "a");
	if (adiFile) {
// write the current record to the file
		fprintf(adiFile,"%s<EOR>\n", adif.c_str());
		fclose (adiFile);
	}
}

void putadif(int num, const char *s, std::string &str = adif)
{
	char tempstr[100];
	int slen = strlen(s);
	int n = snprintf(tempstr, sizeof(tempstr), "<%s:%d>", fields[num].name, slen);
	if (n == -1) {
		LOG_PERROR("snprintf");
		return;
	}
	str.append(tempstr).append(s);
}

std::vector<int> lotw_recs_sent;

void clear_lotw_recs_sent()
{
	lotw_recs_sent.clear();
}

void restore_lotwsdates()
{
extern int editNbr;
	if (lotw_recs_sent.empty()) return;

	int recnbr;
	cQsoRec *rec;
	for (size_t n = 0; n < lotw_recs_sent.size(); n++) {
		recnbr = lotw_recs_sent[n];
		rec = qsodb.getRec(recnbr);
		rec->putField(LOTWSDATE, "");
		if (recnbr == editNbr) {
			inpLOTWsentdate_log->value("");
			inpLOTWsentdate_log->redraw();
		}
	}
	clear_lotw_recs_sent();
}

static notify_dialog *lotw_alert_window = 0;

void check_lotw_log(void *)
{
	FILE * logfile = fl_fopen(lotw_log_fname.c_str(), "rb");

	if (!logfile) {
		tracefile_timeout--;
		if (!tracefile_timeout) {
			LOG_ERROR(_("NO tqsl log file in %d seconds!"), progdefaults.tracefile_timeout);
			restore_lotwsdates();
			clear_lotw_recs_sent();
			return;
		}
		Fl::repeat_timeout(1.0, check_lotw_log);
		return;
	}

	std::string trace_text;
	fseek(logfile, 0, SEEK_END);
	size_t logfile_size = ftell(logfile);
	rewind(logfile);

	if (logfile_size == 0) {
		tracefile_timeout--;
		if (!tracefile_timeout) {
			LOG_ERROR(_("Tqsl log file empty! waited %d seconds!"), progdefaults.tracefile_timeout);
			restore_lotwsdates();
			clear_lotw_recs_sent();
			return;
		}
		Fl::repeat_timeout(1.0, check_lotw_log);
		return;
	}

	int ch;
	for (size_t n = 0; n < logfile_size; n++) {
		ch = fgetc(logfile);
		if (ch >= 0x20 && ch <= 0x7F)
			trace_text += char(ch);
		else
			trace_text.append(ascii3[ch & 0xFF]);
	}
	fclose(logfile);

	size_t p1 = trace_text.find("UploadFile returns 0");
	size_t p2 = trace_text.find("Final Status: Success");

	if ((p1 == std::string::npos) && (p2 == std::string::npos)) {
		tracefile_timeout--;
		if (!tracefile_timeout) {
			LOG_ERROR("%s", "TQSL trace file failed!");
			if (progdefaults.lotw_quiet_mode) {
				std::string alert;
				alert.assign(_("LoTW upload error!"));
				alert.append(_("\nView LoTW trace log:\n"));
				alert.append(lotw_log_fname);
				if (!lotw_alert_window) lotw_alert_window = new notify_dialog;
				lotw_alert_window->notify(alert.c_str(), 15.0);
				REQ(show_notifier, lotw_alert_window);
			}
			restore_lotwsdates();
			clear_lotw_recs_sent();
			return;
		}
		Fl::repeat_timeout(1.0, check_lotw_log);
		return;
	}

	if (progdefaults.lotw_quiet_mode && progdefaults.lotw_show_delivery) {
		if (!lotw_alert_window) lotw_alert_window = new notify_dialog;
		std::string alert;
		alert.assign(_("LoTW upload OK"));
		if (p2 != std::string::npos) {
			alert.append("\n").append(trace_text.substr(p2));
			p1 = alert.find("<CR>");
			if (p1 != std::string::npos) alert.erase(p1);
		}
		lotw_alert_window->notify(alert.c_str(), 5.0);
		REQ(show_notifier, lotw_alert_window);
LOG_INFO("%s", alert.c_str());
	}
	if (progdefaults.xml_logbook)
		xml_update_lotw();
	clear_lotw_recs_sent();
	return;
}

void send_to_lotw(void *)
{
	if (progdefaults.lotw_pathname.empty())
		return;
	if (str_lotw.empty()) return;

	lotw_send_fname = LoTWDir;
	lotw_send_fname.append("fldigi_lotw_").append(zdate());
	lotw_send_fname.append("_").append(ztime());
	lotw_send_fname.append(".adi");

	std::ofstream outfile(lotw_send_fname.c_str());
	outfile << str_lotw;
	outfile.close();

	str_lotw.clear();

	lotw_log_fname = LoTWDir;
	lotw_log_fname.append("lotw_trace.txt"); // LoTW trace file
	rotate_log(lotw_log_fname);
	remove(lotw_log_fname.c_str());

	std::string pstring;
	pstring.assign("\"").append(progdefaults.lotw_pathname).append("\"");
	pstring.append(" -d -u -a compliant");

	if (progdefaults.submit_lotw_password)
		pstring.append(" -p \"").append(progdefaults.lotw_pwd).append("\"");

	if (!progdefaults.lotw_location.empty())
		pstring.append(" -l \"").append(progdefaults.lotw_location).append("\"");

	pstring.append(" -t \"").append(lotw_log_fname).append("\"");

	pstring.append(" \"").append(lotw_send_fname).append("\"");

	if (progdefaults.lotw_quiet_mode)
		pstring.append(" -q");

	LOG_DEBUG("LoTW command std::string: %s", pstring.c_str());

	start_process(pstring);

	tracefile_timeout = progdefaults.tracefile_timeout;
	Fl::add_timeout(0, check_lotw_log);
}

std::string lotw_rec(cQsoRec &rec)
{
	std::string temp;
	std::string strrec;

	putadif(CALL, rec.getField(CALL), strrec);

	putadif(ADIF_MODE, adif2export(rec.getField(ADIF_MODE)).c_str(), strrec);

	std::string sm = adif2submode(rec.getField(ADIF_MODE));
	if (!sm.empty())
	putadif(SUBMODE, sm.c_str(), strrec);

	temp = rec.getField(FREQ);
	temp.resize(7);
	putadif(FREQ, comma2period(temp).c_str(), strrec);

	putadif(QSO_DATE, rec.getField(QSO_DATE), strrec);

	putadif(BAND, rec.getField(BAND), strrec);

	temp = rec.getField(TIME_ON);
	if (temp.length() > 4) temp.erase(4);
	putadif(TIME_ON, temp.c_str(), strrec);

	strrec.append("<EOR>\n");

	return strrec;
}

void submit_lotw(cQsoRec &rec)
{
	std::string adifrec = lotw_rec(rec);

	if (adifrec.empty()) return;

	if (progdefaults.submit_lotw) {
		if (str_lotw.empty())
			str_lotw = "Fldigi LoTW upload file\n<ADIF_VER:5>2.2.7\n<EOH>\n";
		str_lotw.append(adifrec);
		Fl::awake(send_to_lotw);
	}
}

void submit_ADIF(cQsoRec &rec)
{
	adif.erase();

	putadif(QSO_DATE, rec.getField(QSO_DATE));
	putadif(TIME_ON, rec.getField(TIME_ON));
	putadif(QSO_DATE_OFF, rec.getField(QSO_DATE_OFF));
	putadif(TIME_OFF, rec.getField(TIME_OFF));
	putadif(CALL, rec.getField(CALL));
	putadif(FREQ, comma2period(rec.getField(FREQ)).c_str());
	putadif(ADIF_MODE, adif2export(rec.getField(ADIF_MODE)).c_str());

	std::string sm = adif2submode(rec.getField(ADIF_MODE));
	if (!sm.empty())
		putadif(SUBMODE, sm.c_str());

	putadif(RST_SENT, rec.getField(RST_SENT));
	putadif(RST_RCVD, rec.getField(RST_RCVD));
	putadif(TX_PWR, rec.getField(TX_PWR));
	putadif(NAME, rec.getField(NAME));
	putadif(QTH, rec.getField(QTH));
	putadif(STATE, rec.getField(STATE));
	putadif(VE_PROV, rec.getField(VE_PROV));
	putadif(COUNTRY, rec.getField(COUNTRY));
	putadif(GRIDSQUARE, rec.getField(GRIDSQUARE));
	putadif(STX, rec.getField(STX));
	putadif(SRX, rec.getField(SRX));
	putadif(XCHG1, rec.getField(XCHG1));
	putadif(MYXCHG, rec.getField(MYXCHG));
	putadif(TEN_TEN, rec.getField(TEN_TEN));
	notes = rec.getField(NOTES);
	for (size_t i = 0; i < notes.length(); i++)
	    if (notes[i] == '\n') notes[i] = ';';
	putadif(NOTES, notes.c_str());
// these fields will always be blank unless they are added to the main
// QSO log area.
	putadif(IOTA, rec.getField(IOTA));
	putadif(DXCC, rec.getField(DXCC));
	putadif(QSL_VIA, rec.getField(QSL_VIA));
	putadif(QSLRDATE, rec.getField(QSLRDATE));
	putadif(QSLSDATE, rec.getField(QSLSDATE));
	putadif(EQSLRDATE, rec.getField(EQSLRDATE));
	putadif(EQSLSDATE, rec.getField(EQSLSDATE));
	putadif(LOTWRDATE, rec.getField(LOTWRDATE));
	putadif(LOTWSDATE, rec.getField(LOTWSDATE));

	writeADIF();
}

//---------------------------------------------------------------------
// the following IPC message is compatible with xlog remote data spec.
//---------------------------------------------------------------------

#if !defined(__WOE32__) && !defined(__APPLE__)

#define addtomsg(x, y)	log_msg = log_msg + (x) + (y) + LOG_MSEPARATOR

static void send_IPC_log(cQsoRec &rec)
{
	msgtype msgbuf;
	const char   LOG_MSEPARATOR[2] = {1,0};
	int msqid, len;

	int mm, dd, yyyy;
	char szdate[9];
	char sztime[5];
	strncpy(szdate, rec.getField(QSO_DATE), 8);
	szdate[8] = 0;
	sscanf(&szdate[6], "%d", &dd); szdate[6] = 0;
	sscanf(&szdate[4], "%d", &mm); szdate[4] = 0;
	sscanf(szdate, "%d", &yyyy);
	Date logdate(mm, dd, yyyy);

	log_msg.clear();
	log_msg = std::string("program:") + PACKAGE_NAME + std::string(" v ") + PACKAGE_VERSION + LOG_MSEPARATOR;
	addtomsg("version:",	LOG_MVERSION);
	addtomsg("date:",		logdate.szDate(5));
	memset(sztime, 0, sizeof(sztime) / sizeof(char));
	strncpy(sztime, rec.getField(TIME_ON), (sizeof(sztime) / sizeof(char)) - 1);
	addtomsg("TIME:",		sztime);
	memset(sztime, 0, 5);
	strncpy(sztime, rec.getField(TIME_OFF), 4);
	addtomsg("endtime:",            sztime);
	addtomsg("call:",		rec.getField(CALL));
	addtomsg("mhz:",		rec.getField(FREQ));
	addtomsg("mode:",		adif2export(rec.getField(ADIF_MODE)));

	std::string sm = adif2submode(rec.getField(ADIF_MODE));
	if (!sm.empty())
		addtomsg("submode:",	sm.c_str());

	addtomsg("tx:",			rec.getField(RST_SENT));
	addtomsg("rx:",			rec.getField(RST_RCVD));
	addtomsg("name:",		rec.getField(NAME));
	addtomsg("qth:",		rec.getField(QTH));
	addtomsg("state:",		rec.getField(STATE));
	addtomsg("province:",	        rec.getField(VE_PROV));
	addtomsg("country:",	        rec.getField(COUNTRY));
	addtomsg("locator:",	        rec.getField(GRIDSQUARE));
	addtomsg("serialout:",	        rec.getField(STX));
	addtomsg("serialin:",	        rec.getField(SRX));
	addtomsg("free1:",		rec.getField(XCHG1));
	notes = rec.getField(NOTES);
	for (size_t i = 0; i < notes.length(); i++)
	    if (notes[i] == '\n') notes[i] = ';';
	addtomsg("notes:",		notes.c_str());
	addtomsg("power:",		rec.getField(TX_PWR));

	strncpy(msgbuf.mtext, log_msg.c_str(), sizeof(msgbuf.mtext));
	msgbuf.mtext[sizeof(msgbuf.mtext) - 1] = '\0';

	if ((msqid = msgget(LOG_MKEY, 0666 | IPC_CREAT)) == -1) {
		LOG_PERROR("msgget");
		return;
	}
	msgbuf.mtype = LOG_MTYPE;
	len = strlen(msgbuf.mtext) + 1;
	if (msgsnd(msqid, &msgbuf, len, IPC_NOWAIT) < 0)
		LOG_PERROR("msgsnd");
}

#endif

void submit_record(cQsoRec &rec)
{
#if !defined(__WOE32__) && !defined(__APPLE__)
	send_IPC_log(rec);
#endif
	submit_ADIF(rec);
	if (progdefaults.submit_lotw)
		submit_lotw(rec);
	if (progdefaults.eqsl_when_logged)
		submit_eQSL(rec, "");
	if (progdefaults.EnCloudlog)
		submit_cloudlog(rec);
	if (progdefaults.enable_udp_logging)
		post_udp_log(rec);
	n3fjp_add_record(rec);
}

//---------------------------------------------------------------------

extern void xml_add_record();

void submit_log(void)
{
	if (progdefaults.spot_when_logged) {
		if (!dxcluster_viewer->visible()) dxcluster_viewer->show();
		send_DXcluster_spot();
	}

	if (progdefaults.pskrep_log)
		spot_log(
			inpCall->value(),
			inpLoc->value());

	logmode = mode_info[active_modem->get_mode()].adif_name;

	if (progdefaults.xml_logbook && qsodb.isdirty()) {
		xml_add_record();
		qsodb.isdirty(0);
	} else
		AddRecord();

	if (FD_logged_on) FD_add_record();

//	if (progdefaults.eqsl_when_logged)
//		makeEQSL("");

}

//======================================================================
// thread to support sending log entry to eQSL
//======================================================================

pthread_t* EQSLthread = 0;
pthread_mutex_t EQSLmutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t EQSLcond = PTHREAD_COND_INITIALIZER;

static void *EQSL_loop(void *args);
static void EQSL_init(void);

void EQSL_close(void);
void EQSL_send();

static std::string EQSL_url = "";
static std::string SEND_url = "";
static std::string EQSL_xmlpage = "";

static bool EQSLEXIT = false;

static notify_dialog *eqsl_alert_window = 0;

void update_eQSL_fields(void *)
{
	std::string call;
	std::string date;
	std::string mode = "MODE";

	size_t p = EQSL_url.find("<CALL:");
	if (p == std::string::npos) return;
	p = EQSL_url.find(">", p);
	if (p == std::string::npos) return;
	p++;
	size_t p2 = EQSL_url.find("<", p);
	if (p2 == std::string::npos) return;
	call = EQSL_url.substr(p, p2 - p);
	p = EQSL_url.find("<QSO_DATE:");
	if (p == std::string::npos) return;
	p = EQSL_url.find(">", p);
	if (p == std::string::npos) return;
	p++;
	p2 = EQSL_url.find("<", p);
	if (p2 == std::string::npos) return;
	date = EQSL_url.substr(p, p2 - p);
	p = EQSL_url.find("<MODE:");
	if (p != std::string::npos) {
		p = EQSL_url.find(">", p);
		if (p != std::string::npos) {
			p2 = EQSL_url.find("<", ++p);
			if (p2 != std::string::npos)
				mode = EQSL_url.substr(p, p2 - p);
		}
	}
	p = EQSL_url.find("<SUBMODE:");
	if (p != std::string::npos) {
		p = EQSL_url.find(">", p);
		if (p != std::string::npos) {
			p2 = EQSL_url.find("<", ++p);
			if (p2 != std::string::npos) {
				std::string submode = EQSL_url.substr(p, p2 - p);
				if (!submode.empty()) mode = submode;
			}
		}
	}
	inpEQSLsentdate_log->value(date.c_str());

	if (progdefaults.xml_logbook)
		xml_update_eqsl();
	else
		updateRecord();

	std::string qsl_logged = "eQSL logged: ";
	qsl_logged.append(call).append(" on ").append(mode);

	if (progdefaults.eqsl_show_delivery) {
		if (!eqsl_alert_window) eqsl_alert_window = new notify_dialog;
		eqsl_alert_window->notify(qsl_logged.c_str(), 5.0);
		REQ(show_notifier, eqsl_alert_window);
	}

	LOG_INFO("%s", qsl_logged.c_str());
}

static void cannot_connect_to_eqsl(void *)
{
	std::string msg = "Cannot connect to www.eQSL.cc";

	if (!eqsl_alert_window) eqsl_alert_window = new notify_dialog;
	eqsl_alert_window->notify(msg.c_str(), 5.0);
	REQ(show_notifier, eqsl_alert_window);
}

static void eqsl_error(void *)
{
	size_t p = EQSL_xmlpage.find("Error:");
	size_t p2 = EQSL_xmlpage.find('\n', p);
	std::string errstr = "eQSL error:";
	errstr.append("testing 1.2.3");
	errstr.append(EQSL_xmlpage.substr(p, p2 - p - 1));
	errstr.append("\n").append(EQSL_url.substr(0,40));

	if (!eqsl_alert_window) eqsl_alert_window = new notify_dialog;
	eqsl_alert_window->notify(errstr.c_str(), 5.0);
	REQ(show_notifier, eqsl_alert_window);
	LOG_ERROR("%s", errstr.c_str());
}

static void *EQSL_loop(void *args)
{
	SET_THREAD_ID(EQSL_TID);

	SET_THREAD_CANCEL();

	for (;;) {
		TEST_THREAD_CANCEL();
		pthread_mutex_lock(&EQSLmutex);
		pthread_cond_wait(&EQSLcond, &EQSLmutex);
		pthread_mutex_unlock(&EQSLmutex);

		if (EQSLEXIT)
			return NULL;

		size_t p;
		if (get_http(SEND_url, EQSL_xmlpage, 5.0) <= 0) {
			REQ(cannot_connect_to_eqsl, (void *)0);
		}
		else if ((p = EQSL_xmlpage.find("Error:")) != std::string::npos) {
			REQ(eqsl_error, (void *)0);
		} else {
			REQ(update_eQSL_fields, (void *)0);
		}

	}
	return NULL;
}

void EQSL_close(void)
{
	ENSURE_THREAD(FLMAIN_TID);

	if (!EQSLthread)
		return;

	CANCEL_THREAD(*EQSLthread);

	pthread_mutex_lock(&EQSLmutex);
	EQSLEXIT = true;
	pthread_cond_signal(&EQSLcond);
	pthread_mutex_unlock(&EQSLmutex);

	pthread_join(*EQSLthread, NULL);
	delete EQSLthread;
	EQSLthread = 0;
}

static void EQSL_init(void)
{
	ENSURE_THREAD(FLMAIN_TID);

	if (EQSLthread)
		return;
	EQSLthread = new pthread_t;
	EQSLEXIT = false;
	if (pthread_create(EQSLthread, NULL, EQSL_loop, NULL) != 0) {
		LOG_PERROR("pthread_create");
		return;
	}
	MilliSleep(10);
}

void submit_eQSL(cQsoRec &rec, std::string msg)
{
	std::string eQSL_data;
	std::string tempstr;
	char sztemp[100];

	eQSL_data = "upload <adIF_ver:4>4.0 ";
	snprintf(sztemp, sizeof(sztemp),"<EQSL_USER:%d>%s<EQSL_PSWD:%d>%s",
		static_cast<int>(progdefaults.eqsl_id.length()),
		ucasestr(progdefaults.eqsl_id).c_str(),
		static_cast<int>(progdefaults.eqsl_pwd.length()),
		progdefaults.eqsl_pwd.c_str());
	eQSL_data.append(sztemp);
	eQSL_data.append("<PROGRAMID:6>FLDIGI<EOH>");
	if (!progdefaults.eqsl_nick.empty()) {
		snprintf(sztemp, sizeof(sztemp), "<APP_EQSL_QTH_NICKNAME:%d>%s",
			static_cast<int>(progdefaults.eqsl_nick.length()),
			progdefaults.eqsl_nick.c_str());
		eQSL_data.append(sztemp);
	}

	putadif(CALL, rec.getField(CALL), eQSL_data);
	putadif(ADIF_MODE, adif2export(rec.getField(ADIF_MODE)).c_str(), eQSL_data);

	std::string sm = adif2submode(rec.getField(ADIF_MODE));
	if (!sm.empty())
		putadif(SUBMODE, sm.c_str(), eQSL_data);

	tempstr = rec.getField(FREQ);
	tempstr.resize(7);
	putadif(FREQ, comma2period(tempstr).c_str(), eQSL_data);
	putadif(BAND, rec.getField(BAND), eQSL_data);
	putadif(QSO_DATE, sDate_on.c_str(), eQSL_data);
	tempstr = rec.getField(TIME_ON);
	if (tempstr.length() > 4) tempstr.erase(4);
	putadif(TIME_ON, tempstr.c_str(), eQSL_data);
	putadif(RST_SENT, rec.getField(RST_SENT), eQSL_data);

	size_t p = 0;
	if (msg.empty()) msg = progdefaults.eqsl_default_message;
	if (!msg.empty()) {
// replace message tags {CALL}, {NAME}, {MODE}
		while ((p = msg.find("{CALL}")) != std::string::npos)
			msg.replace(p, 6, inpCall->value());
		while ((p = msg.find("{NAME}")) != std::string::npos)
			msg.replace(p, 6, inpName->value());
		while ((p = msg.find("{MODE}")) != std::string::npos) {
			tempstr.assign(mode_info[active_modem->get_mode()].export_mode);
			sm = mode_info[active_modem->get_mode()].export_submode;
			if (!sm.empty())
				tempstr.append("/").append(sm);
			msg.replace(p, 6, tempstr);
		}
		snprintf(sztemp, sizeof(sztemp), "<QSLMSG:%d>%s",
			static_cast<int>(msg.length()),
			msg.c_str());
		eQSL_data.append(sztemp);
	}
	eQSL_data.append("<EOR>\n");

	std::string eQSL_header = progdefaults.eqsl_www_url;

	EQSL_url.assign(eQSL_header).append(eQSL_data);

	tempstr.clear();
	for (size_t n = 0; n < eQSL_data.length(); n++) {
		if (eQSL_data[n] == ' ' || eQSL_data[n] == '\n') tempstr.append("%20");
		else if (eQSL_data[n] == '<') tempstr.append("%3C");
		else if (eQSL_data[n] == '>') tempstr.append("%3E");
		else if (eQSL_data[n] == '_') tempstr.append("%5F");
		else if (eQSL_data[n] == ':') tempstr.append("%3A");
		else if (eQSL_data[n] > ' ' && eQSL_data[n] <= '}')
			tempstr += eQSL_data[n];
	}

	eQSL_data = eQSL_header;
	eQSL_data.append(tempstr);

	sendEQSL(eQSL_data.c_str());

}

void sendEQSL(const char *url)
{
	ENSURE_THREAD(FLMAIN_TID);

	if (!EQSLthread)
		EQSL_init();

	pthread_mutex_lock(&EQSLmutex);
	SEND_url = url;
	pthread_cond_signal(&EQSLcond);
	pthread_mutex_unlock(&EQSLmutex);
}

// this function may be called from several places including macro
// expansion and execution

void makeEQSL(const char *message)
{
	cQsoRec *rec;
	if (qsodb.nbrRecs() <= 0) return;

	bool rev = progStatus.logbook_reverse;

	qsodb.reverse = false;
	qsodb.SortByDate(progdefaults.sort_date_time_off);

	rec = qsodb.getRec(qsodb.nbrRecs() - 1); // last record
	submit_eQSL(*rec, message);

	extern sorttype lastsort;
	switch (lastsort) {
		case SORTCALL :
			cQsoDb::reverse = rev;
			qsodb.SortByCall();
			break;
		case SORTDATE :
			cQsoDb::reverse = rev;
			qsodb.SortByDate(progdefaults.sort_date_time_off);
			break;
		case SORTFREQ :
			cQsoDb::reverse = rev;
			qsodb.SortByFreq();
			break;
		case SORTMODE :
			cQsoDb::reverse = rev;
			qsodb.SortByMode();
			break;
		default: break;
	}

}

//----------------------------------------------------------------------
// Cloud Log export
//----------------------------------------------------------------------

EXPORT_LOG_FIELDS cloud_fields;

void submit_cloudlog(cQsoRec &rec)
{
	std::string cloudlog_data;
	std::string tempstr;

	cloudlog_data.clear();

	if (cloud_fields.call)
		putadif(CALL, rec.getField(CALL), cloudlog_data);

	if (cloud_fields.mode) {
		putadif(ADIF_MODE, adif2export(rec.getField(ADIF_MODE)).c_str(), cloudlog_data);

		std::string sm = adif2submode(rec.getField(ADIF_MODE));
		if (!sm.empty())
			putadif(SUBMODE, sm.c_str(), cloudlog_data);
	}

	if (cloud_fields.freq)
		putadif(FREQ, comma2period(rec.getField(FREQ)).c_str(), cloudlog_data);

	if (cloud_fields.band)
		putadif(BAND, rec.getField(BAND), cloudlog_data);

	if (cloud_fields.date_on)
		putadif(QSO_DATE, sDate_on.c_str(), cloudlog_data);

	if (cloud_fields.time_on) {
		tempstr = rec.getField(TIME_ON);
		if (tempstr.length() > 4) tempstr.erase(4);
		putadif(TIME_ON, tempstr.c_str(), cloudlog_data);
	}

	if (cloud_fields.date_off)
		putadif(QSO_DATE_OFF, rec.getField(QSO_DATE_OFF), cloudlog_data);

	if (cloud_fields.time_off) {
		tempstr = rec.getField(TIME_ON);
		if (tempstr.length() > 4) tempstr.erase(4);
		putadif(TIME_OFF, tempstr.c_str(), cloudlog_data);
	}

	if (cloud_fields.name)
		putadif(NAME, rec.getField(NAME), cloudlog_data);

	if (cloud_fields.qth)
		putadif(QTH, rec.getField(QTH), cloudlog_data);

	if (cloud_fields.state)
		putadif(STATE, rec.getField(STATE), cloudlog_data);

	if (cloud_fields.province)
		putadif(VE_PROV, rec.getField(VE_PROV), cloudlog_data);

	if (cloud_fields.country)
		putadif(COUNTRY, rec.getField(COUNTRY), cloudlog_data);

	if (cloud_fields.rst_sent)
		putadif(RST_SENT, rec.getField(RST_SENT), cloudlog_data);

	if (cloud_fields.rst_rcvd)
		putadif(RST_RCVD, rec.getField(RST_RCVD), cloudlog_data);

	if (cloud_fields.tx_pwr)
		putadif(TX_PWR, rec.getField(TX_PWR), cloudlog_data);

	if (cloud_fields.county)
		putadif(CNTY, rec.getField(CNTY), cloudlog_data);

	if (cloud_fields.dxcc)
		putadif( DXCC, rec.getField(DXCC), cloudlog_data);

	if (cloud_fields.cqz)
		putadif(CQZ, rec.getField(CQZ), cloudlog_data);

	if (cloud_fields.iota)
		putadif(IOTA, rec.getField(IOTA), cloudlog_data);

	if (cloud_fields.continent)
		putadif(CONT, rec.getField(CONT), cloudlog_data);

	if (cloud_fields.ituz)
		putadif(ITUZ, rec.getField(ITUZ), cloudlog_data);

	if (cloud_fields.gridsquare)
		putadif(GRIDSQUARE, rec.getField(GRIDSQUARE), cloudlog_data);

	if (cloud_fields.qsl_rcvd)
		putadif(QSLRDATE, rec.getField(QSLRDATE), cloudlog_data);

	if (cloud_fields.qsl_sent)
		putadif(QSLSDATE, rec.getField(QSLSDATE), cloudlog_data);

	if (cloud_fields.eqsl_rcvd)
		putadif(EQSLRDATE, rec.getField(EQSLRDATE), cloudlog_data);

	if (cloud_fields.eqsl_sent)
		putadif(EQSLSDATE, rec.getField(EQSLSDATE), cloudlog_data);

	if (cloud_fields.lotw_rcvd)
		putadif(LOTWRDATE, rec.getField(LOTWRDATE), cloudlog_data);

	if (cloud_fields.lotw_sent)
		putadif(LOTWSDATE, rec.getField(LOTWSDATE), cloudlog_data);

	if (cloud_fields.qsl_via)
		putadif(QSL_VIA, rec.getField(QSL_VIA), cloudlog_data);

	if (cloud_fields.notes) {
		std::string temp = rec.getField(NOTES);
		for (size_t n = 0; n < temp.length(); n++)
			if (temp[n] == '\n') temp[n] = ';';
		putadif(NOTES, temp.c_str(), cloudlog_data);
	}

	if (cloud_fields.srx)
		putadif(SRX, rec.getField(SRX), cloudlog_data);

	if (cloud_fields.stx)
		putadif(STX, rec.getField(STX), cloudlog_data);

	if (cloud_fields.xchg1)
		putadif(XCHG1, rec.getField(XCHG1), cloudlog_data);

	if (cloud_fields.myxchg)
		putadif(MYXCHG, rec.getField(MYXCHG), cloudlog_data);

	if (cloud_fields.arrl_class)
		putadif(CLASS, rec.getField(CLASS), cloudlog_data);

	if (cloud_fields.arrl_sect)
		putadif(ARRL_SECT, rec.getField(ARRL_SECT), cloudlog_data);

	if (cloud_fields.op_call)
		putadif(OP_CALL, rec.getField(OP_CALL), cloudlog_data);

	if (cloud_fields.sta_call)
		putadif(STA_CALL, rec.getField(STA_CALL), cloudlog_data);

	if (cloud_fields.mygrid)
		putadif(MY_GRID, rec.getField(MY_GRID), cloudlog_data);

	if (cloud_fields.mycity)
		putadif(MY_CITY, rec.getField(MY_CITY), cloudlog_data);

	if (cloud_fields.check)
		putadif(CHECK, rec.getField(CHECK), cloudlog_data);

	if (cloud_fields.age)
		putadif(AGE, rec.getField(AGE), cloudlog_data);

	if (cloud_fields.ten_ten)
		putadif(TEN_TEN, rec.getField(TEN_TEN), cloudlog_data);

	if (cloud_fields.cwss_check)
		putadif(SS_CHK, rec.getField(SS_CHK), cloudlog_data);

	if (cloud_fields.cwss_serno)
		putadif(SS_SERNO, rec.getField(SS_SERNO), cloudlog_data);

	if (cloud_fields.cwss_prec)
		putadif(SS_PREC, rec.getField(SS_PREC), cloudlog_data);

	if (cloud_fields.cwss_section)
		putadif(SS_SEC, rec.getField(SS_SEC), cloudlog_data);

	cloudlog_data.append("<EOR>");

	tempstr.clear();
	std::string cloudlogUrl    = progdefaults.cloudlog_api_url;
	cloudlogUrl                = cloudlogUrl + "/index.php/api/qso";
	std::string cloudlogApiKey = progdefaults.cloudlog_api_key;
	int cloudlogStationId      = progdefaults.cloudlog_station_id;
	post_http(cloudlogUrl.c_str(), cloudlogApiKey.c_str(), cloudlogStationId, cloudlog_data.c_str(), EQSL_xmlpage, 5.0);
}

static int cld_first_use = 1;

void save_cloud_prefs()
{
	cloud_fields.name         = btn_cloud_Name->value();
	cloud_fields.freq         = btn_cloud_Freq->value();
	cloud_fields.band         = btn_cloud_Band->value();
	cloud_fields.mode         = btn_cloud_Mode->value();
	cloud_fields.date_on      = btn_cloud_QSOdateOn->value();
	cloud_fields.date_off     = btn_cloud_QSOdateOff->value();
	cloud_fields.time_on      = btn_cloud_TimeON->value();
	cloud_fields.time_off     = btn_cloud_TimeOFF->value();
	cloud_fields.tx_pwr       = btn_cloud_TX_pwr->value();
	cloud_fields.rst_sent     = btn_cloud_RSTsent->value();
	cloud_fields.rst_rcvd     = btn_cloud_RSTrcvd->value();
	cloud_fields.qth          = btn_cloud_Qth->value();
	cloud_fields.gridsquare   = btn_cloud_LOC->value();
	cloud_fields.state        = btn_cloud_State->value();
	cloud_fields.age          = btn_cloud_Age->value();

	cloud_fields.sta_call     = btn_cloud_StaCall->value();
	cloud_fields.mygrid       = btn_cloud_StaGrid->value();
	cloud_fields.mycity       = btn_cloud_StaCity->value();
	cloud_fields.op_call      = btn_cloud_Operator->value();
	cloud_fields.province     = btn_cloud_Province->value();
	cloud_fields.country      = btn_cloud_Country->value();
	cloud_fields.notes        = btn_cloud_Notes->value();
	cloud_fields.qsl_rcvd     = btn_cloud_QSLrcvd->value();
	cloud_fields.qsl_sent     = btn_cloud_QSLsent->value();
	cloud_fields.eqsl_rcvd    = btn_cloud_eQSLrcvd->value();
	cloud_fields.eqsl_sent    = btn_cloud_eQSLsent->value();
	cloud_fields.lotw_rcvd    = btn_cloud_LOTWrcvd->value();
	cloud_fields.lotw_sent    = btn_cloud_LOTWsent->value();
	cloud_fields.qsl_via      = btn_cloud_QSL_VIA->value();
	cloud_fields.srx          = btn_cloud_SerialIN->value();
	cloud_fields.stx          = btn_cloud_SerialOUT->value();

	cloud_fields.check        = btn_cloud_Check->value();
	cloud_fields.xchg1        = btn_cloud_XchgIn->value();
	cloud_fields.myxchg       = btn_cloud_MyXchg->value();
	cloud_fields.county       = btn_cloud_CNTY->value();
	cloud_fields.continent    = btn_cloud_CONT->value();
	cloud_fields.cqz          = btn_cloud_CQZ->value();
	cloud_fields.dxcc         = btn_cloud_DXCC->value();
	cloud_fields.iota         = btn_cloud_IOTA->value();
	cloud_fields.ituz         = btn_cloud_ITUZ->value();
	cloud_fields.arrl_class   = btn_cloud_Class->value();
	cloud_fields.arrl_sect    = btn_cloud_Section->value();
	cloud_fields.cwss_serno   = btn_cloud_cwss_serno->value();
	cloud_fields.cwss_prec    = btn_cloud_cwss_prec->value();
	cloud_fields.cwss_check   = btn_cloud_cwss_check->value();
	cloud_fields.cwss_section = btn_cloud_cwss_section->value();
	cloud_fields.ten_ten      = btn_cloud_1010->value();

	std::string pref;
	Fl_Preferences spref(HomeDir.c_str(), "w1hkj.com", "cloud_fields");

	spref.set("cld_first_use", 0);

	spref.set("call", cloud_fields.call);
	spref.set("mode", cloud_fields.mode);
	spref.set("freq", cloud_fields.freq);
	spref.set("band", cloud_fields.band);
	spref.set("date_on", cloud_fields.date_on);
	spref.set("time_on", cloud_fields.time_on);
	spref.set("date_off", cloud_fields.date_off);
	spref.set("time_off", cloud_fields.time_off);
	spref.set("name", cloud_fields.name);
	spref.set("qth", cloud_fields.qth);
	spref.set("state", cloud_fields.state);
	spref.set("province", cloud_fields.province);
	spref.set("country", cloud_fields.country);
	spref.set("rst_sent", cloud_fields.rst_sent);
	spref.set("rst_rcvd", cloud_fields.rst_rcvd);
	spref.set("tx_pwr", cloud_fields.tx_pwr);
	spref.set("county", cloud_fields.country);
	spref.set("dxcc", cloud_fields.dxcc);
	spref.set("cqz", cloud_fields.cqz);
	spref.set("iota", cloud_fields.iota);
	spref.set("continent", cloud_fields.continent);
	spref.set("ituz", cloud_fields.ituz);
	spref.set("gridsquare", cloud_fields.gridsquare);
	spref.set("qsl_rcvd", cloud_fields.qsl_rcvd);
	spref.set("qsl_sent", cloud_fields.qsl_sent);
	spref.set("eqsl_rcvd", cloud_fields.eqsl_rcvd);
	spref.set("eqsl_sent", cloud_fields.eqsl_sent);
	spref.set("lotw_rcvd", cloud_fields.lotw_rcvd);
	spref.set("lotw_sent", cloud_fields.lotw_sent);
	spref.set("qsl_via", cloud_fields.qsl_via);
	spref.set("notes", cloud_fields.notes);
	spref.set("srx", cloud_fields.srx);
	spref.set("stx", cloud_fields.stx);
	spref.set("xchg1", cloud_fields.xchg1);
	spref.set("myxchg", cloud_fields.myxchg);
	spref.set("arrl_class", cloud_fields.arrl_class);
	spref.set("arrl_sect", cloud_fields.arrl_sect);
	spref.set("op_call", cloud_fields.op_call);
	spref.set("sta_call", cloud_fields.sta_call);
	spref.set("mygrid", cloud_fields.mygrid);
	spref.set("mycity", cloud_fields.mycity);
	spref.set("check", cloud_fields.check);
	spref.set("age", cloud_fields.age);
	spref.set("ten_ten", cloud_fields.ten_ten);
	spref.set("cwss_serno", cloud_fields.cwss_serno);
	spref.set("cwss_prec", cloud_fields.cwss_prec);
	spref.set("cwss_check", cloud_fields.cwss_check);
	spref.set("cwss_section", cloud_fields.cwss_section);

}

void load_cloud_prefs()
{
	std::string pref;
	Fl_Preferences spref(HomeDir.c_str(), "w1hkj.com", "cloud_fields");

	spref.get("cld_first_use", cld_first_use, cld_first_use);
	if (cld_first_use)
		return;

	spref.get("call", cloud_fields.call, cloud_fields.call);
	spref.get("mode", cloud_fields.mode, cloud_fields.mode);
	spref.get("freq", cloud_fields.freq, cloud_fields.freq);
	spref.get("band", cloud_fields.band, cloud_fields.band);
	spref.get("date_on", cloud_fields.date_on, cloud_fields.date_on);
	spref.get("time_on", cloud_fields.time_on, cloud_fields.time_on);
	spref.get("date_off", cloud_fields.date_off, cloud_fields.date_off);
	spref.get("time_off", cloud_fields.time_off, cloud_fields.time_off);
	spref.get("name", cloud_fields.name, cloud_fields.name);
	spref.get("qth", cloud_fields.qth, cloud_fields.qth);
	spref.get("state", cloud_fields.state, cloud_fields.state);
	spref.get("province", cloud_fields.province, cloud_fields.province);
	spref.get("country", cloud_fields.country, cloud_fields.country);
	spref.get("rst_sent", cloud_fields.rst_sent, cloud_fields.rst_sent);
	spref.get("rst_rcvd", cloud_fields.rst_rcvd, cloud_fields.rst_rcvd);
	spref.get("tx_pwr", cloud_fields.tx_pwr, cloud_fields.tx_pwr);
	spref.get("county", cloud_fields.country, cloud_fields.country);
	spref.get("dxcc", cloud_fields.dxcc, cloud_fields.dxcc);
	spref.get("cqz", cloud_fields.cqz, cloud_fields.cqz);
	spref.get("iota", cloud_fields.iota, cloud_fields.iota);
	spref.get("continent", cloud_fields.continent, cloud_fields.continent);
	spref.get("ituz", cloud_fields.ituz, cloud_fields.ituz);
	spref.get("gridsquare", cloud_fields.gridsquare, cloud_fields.gridsquare);
	spref.get("qsl_rcvd", cloud_fields.qsl_rcvd, cloud_fields.qsl_rcvd);
	spref.get("qsl_sent", cloud_fields.qsl_sent, cloud_fields.qsl_sent);
	spref.get("eqsl_rcvd", cloud_fields.eqsl_rcvd, cloud_fields.eqsl_rcvd);
	spref.get("eqsl_sent", cloud_fields.eqsl_sent, cloud_fields.eqsl_sent);
	spref.get("lotw_rcvd", cloud_fields.lotw_rcvd, cloud_fields.lotw_rcvd);
	spref.get("lotw_sent", cloud_fields.lotw_sent, cloud_fields.lotw_sent);
	spref.get("qsl_via", cloud_fields.qsl_via, cloud_fields.qsl_via);
	spref.get("notes", cloud_fields.notes, cloud_fields.notes);
	spref.get("srx", cloud_fields.srx, cloud_fields.srx);
	spref.get("stx", cloud_fields.stx, cloud_fields.stx);
	spref.get("xchg1", cloud_fields.xchg1, cloud_fields.xchg1);
	spref.get("myxchg", cloud_fields.myxchg, cloud_fields.myxchg);
	spref.get("arrl_class", cloud_fields.arrl_class, cloud_fields.arrl_class);
	spref.get("arrl_sect", cloud_fields.arrl_sect, cloud_fields.arrl_sect);
	spref.get("op_call", cloud_fields.op_call, cloud_fields.op_call);
	spref.get("sta_call", cloud_fields.sta_call, cloud_fields.sta_call);
	spref.get("mygrid", cloud_fields.mygrid, cloud_fields.mygrid);
	spref.get("mycity", cloud_fields.mycity, cloud_fields.mycity);
	spref.get("check", cloud_fields.check, cloud_fields.check);
	spref.get("age", cloud_fields.age, cloud_fields.age);
	spref.get("ten_ten", cloud_fields.ten_ten, cloud_fields.ten_ten);
	spref.get("cwss_serno", cloud_fields.cwss_serno, cloud_fields.cwss_serno);
	spref.get("cwss_prec", cloud_fields.cwss_prec, cloud_fields.cwss_prec);
	spref.get("cwss_check", cloud_fields.cwss_check, cloud_fields.cwss_check);
	spref.get("cwss_section", cloud_fields.cwss_section, cloud_fields.cwss_section);

}

//----------------------------------------------------------------------
// UDP log export
//----------------------------------------------------------------------

EXPORT_LOG_FIELDS udp_fields;

void post_udp_log(cQsoRec &rec)
{
	std::string udp_data;
	std::string tempstr;

	udp_data.clear();

	if (udp_fields.call)
		putadif(CALL, rec.getField(CALL), udp_data);

	if (udp_fields.mode) {
		putadif(ADIF_MODE, adif2export(rec.getField(ADIF_MODE)).c_str(), udp_data);

		std::string sm = adif2submode(rec.getField(ADIF_MODE));
		if (!sm.empty())
			putadif(SUBMODE, sm.c_str(), udp_data);
	}

	if (udp_fields.freq)
		putadif(FREQ, comma2period(rec.getField(FREQ)).c_str(), udp_data);

	if (udp_fields.band)
		putadif(BAND, rec.getField(BAND), udp_data);

	if (udp_fields.date_on)
		putadif(QSO_DATE, sDate_on.c_str(), udp_data);

	if (udp_fields.time_on) {
		tempstr = rec.getField(TIME_ON);
		if (tempstr.length() > 4) tempstr.erase(4);
		putadif(TIME_ON, tempstr.c_str(), udp_data);
	}

	if (udp_fields.date_off)
		putadif(QSO_DATE_OFF, rec.getField(QSO_DATE_OFF), udp_data);

	if (udp_fields.time_off) {
		tempstr = rec.getField(TIME_ON);
		if (tempstr.length() > 4) tempstr.erase(4);
		putadif(TIME_OFF, tempstr.c_str(), udp_data);
	}

	if (udp_fields.name)
		putadif(NAME, rec.getField(NAME), udp_data);

	if (udp_fields.qth)
		putadif(QTH, rec.getField(QTH), udp_data);

	if (udp_fields.state)
		putadif(STATE, rec.getField(STATE), udp_data);

	if (udp_fields.province)
		putadif(VE_PROV, rec.getField(VE_PROV), udp_data);

	if (udp_fields.country)
		putadif(COUNTRY, rec.getField(COUNTRY), udp_data);

	if (udp_fields.rst_sent)
		putadif(RST_SENT, rec.getField(RST_SENT), udp_data);

	if (udp_fields.rst_rcvd)
		putadif(RST_RCVD, rec.getField(RST_RCVD), udp_data);

	if (udp_fields.tx_pwr)
		putadif(TX_PWR, rec.getField(TX_PWR), udp_data);

	if (udp_fields.county)
		putadif(CNTY, rec.getField(CNTY), udp_data);

	if (udp_fields.dxcc)
		putadif( DXCC, rec.getField(DXCC), udp_data);

	if (udp_fields.cqz)
		putadif(CQZ, rec.getField(CQZ), udp_data);

	if (udp_fields.iota)
		putadif(IOTA, rec.getField(IOTA), udp_data);

	if (udp_fields.continent)
		putadif(CONT, rec.getField(CONT), udp_data);

	if (udp_fields.ituz)
		putadif(ITUZ, rec.getField(ITUZ), udp_data);

	if (udp_fields.gridsquare)
		putadif(GRIDSQUARE, rec.getField(GRIDSQUARE), udp_data);

	if (udp_fields.qsl_rcvd)
		putadif(QSLRDATE, rec.getField(QSLRDATE), udp_data);

	if (udp_fields.qsl_sent)
		putadif(QSLSDATE, rec.getField(QSLSDATE), udp_data);

	if (udp_fields.eqsl_rcvd)
		putadif(EQSLRDATE, rec.getField(EQSLRDATE), udp_data);

	if (udp_fields.eqsl_sent)
		putadif(EQSLSDATE, rec.getField(EQSLSDATE), udp_data);

	if (udp_fields.lotw_rcvd)
		putadif(LOTWRDATE, rec.getField(LOTWRDATE), udp_data);

	if (udp_fields.lotw_sent)
		putadif(LOTWSDATE, rec.getField(LOTWSDATE), udp_data);

	if (udp_fields.qsl_via)
		putadif(QSL_VIA, rec.getField(QSL_VIA), udp_data);

	if (udp_fields.notes) {
		std::string temp = rec.getField(NOTES);
		for (size_t n = 0; n < temp.length(); n++)
			if (temp[n] == '\n') temp[n] = ';';
		putadif(NOTES, temp.c_str(), udp_data);
	}

	if (udp_fields.srx)
		putadif(SRX, rec.getField(SRX), udp_data);

	if (udp_fields.stx)
		putadif(STX, rec.getField(STX), udp_data);

	if (udp_fields.xchg1)
		putadif(XCHG1, rec.getField(XCHG1), udp_data);

	if (udp_fields.myxchg)
		putadif(MYXCHG, rec.getField(MYXCHG), udp_data);

	if (udp_fields.arrl_class)
		putadif(CLASS, rec.getField(CLASS), udp_data);

	if (udp_fields.arrl_sect)
		putadif(ARRL_SECT, rec.getField(ARRL_SECT), udp_data);

	if (udp_fields.op_call)
		putadif(OP_CALL, rec.getField(OP_CALL), udp_data);

	if (udp_fields.sta_call)
		putadif(STA_CALL, rec.getField(STA_CALL), udp_data);

	if (udp_fields.mygrid)
		putadif(MY_GRID, rec.getField(MY_GRID), udp_data);

	if (udp_fields.mycity)
		putadif(MY_CITY, rec.getField(MY_CITY), udp_data);

	if (udp_fields.check)
		putadif(CHECK, rec.getField(CHECK), udp_data);

	if (udp_fields.age)
		putadif(AGE, rec.getField(AGE), udp_data);

	if (udp_fields.ten_ten)
		putadif(TEN_TEN, rec.getField(TEN_TEN), udp_data);

	if (udp_fields.cwss_check)
		putadif(SS_CHK, rec.getField(SS_CHK), udp_data);

	if (udp_fields.cwss_serno)
		putadif(SS_SERNO, rec.getField(SS_SERNO), udp_data);

	if (udp_fields.cwss_prec)
		putadif(SS_PREC, rec.getField(SS_PREC), udp_data);

	if (udp_fields.cwss_section)
		putadif(SS_SEC, rec.getField(SS_SEC), udp_data);

	udp_data.append("<EOR>");

	Socket * udp_socket;

	try {
		udp_socket = new Socket(Address(progdefaults.udp_address.c_str(), progdefaults.udp_port.c_str(), "udp"));
		udp_socket->connect();
		if (udp_socket->send( udp_data.c_str(), udp_data.length() ) != udp_data.length()) 
			throw SocketException("udp send failed");
		LOG_INFO("UDP record: %s", udp_data.c_str());
	} catch (const SocketException& e) {
		tempstr.assign("Could not send udp data to ").append(progdefaults.udp_address);
		tempstr.append(", port ").append(progdefaults.udp_port);
		tempstr.append(": ").append(e.what());
		LOG_ERROR("%s", tempstr.c_str());
	}

}

void udp_test()
{
	Socket *udp_socket;

	std::string payload = "Test UDP broadcast on ";
	payload.append(progdefaults.udp_address).append(" : " ).append(progdefaults.udp_port);

	char udp_data[50 + payload.length()];
	snprintf(udp_data, sizeof(udp_data), "<FLDIGI_TEST:%d>%s<STATION_CALLSIGN:5>W1HKJ<EOR>",
		(int)payload.length(), payload.c_str() );

	try {
		udp_socket = new Socket(Address(progdefaults.udp_address.c_str(), progdefaults.udp_port.c_str(), "udp"));
		udp_socket->connect();
		if (udp_socket->send( udp_data, strlen(udp_data) ) != strlen(udp_data)) 
			throw SocketException("udp send failed");
		LOG_INFO("UDP TEST record: %s", udp_data);
		fl_alert2("Sent UDP test record:\n%s", udp_data);
	} catch (const SocketException& e) {
		std::string tempstr;
		tempstr.assign("Could not send udp data to ").append(progdefaults.udp_address);
		tempstr.append(", port ").append(progdefaults.udp_port);
		tempstr.append(": ").append(e.what());
		LOG_ERROR("%s", tempstr.c_str());
	}
}

static int udp_first_use = 1;

void save_udp_prefs()
{
	udp_fields.name         = btn_udp_Name->value();
	udp_fields.freq         = btn_udp_Freq->value();
	udp_fields.band         = btn_udp_Band->value();
	udp_fields.mode         = btn_udp_Mode->value();
	udp_fields.date_on      = btn_udp_QSOdateOn->value();
	udp_fields.date_off     = btn_udp_QSOdateOff->value();
	udp_fields.time_on      = btn_udp_TimeON->value();
	udp_fields.time_off     = btn_udp_TimeOFF->value();
	udp_fields.tx_pwr       = btn_udp_TX_pwr->value();
	udp_fields.rst_sent     = btn_udp_RSTsent->value();
	udp_fields.rst_rcvd     = btn_udp_RSTrcvd->value();
	udp_fields.qth          = btn_udp_Qth->value();
	udp_fields.gridsquare   = btn_udp_LOC->value();
	udp_fields.state        = btn_udp_State->value();
	udp_fields.age          = btn_udp_Age->value();

	udp_fields.sta_call     = btn_udp_StaCall->value();
	udp_fields.mygrid       = btn_udp_StaGrid->value();
	udp_fields.mycity       = btn_udp_StaCity->value();
	udp_fields.op_call      = btn_udp_Operator->value();
	udp_fields.province     = btn_udp_Province->value();
	udp_fields.country      = btn_udp_Country->value();
	udp_fields.notes        = btn_udp_Notes->value();
	udp_fields.qsl_rcvd     = btn_udp_QSLrcvd->value();
	udp_fields.qsl_sent     = btn_udp_QSLsent->value();
	udp_fields.eqsl_rcvd    = btn_udp_eQSLrcvd->value();
	udp_fields.eqsl_sent    = btn_udp_eQSLsent->value();
	udp_fields.lotw_rcvd    = btn_udp_LOTWrcvd->value();
	udp_fields.lotw_sent    = btn_udp_LOTWsent->value();
	udp_fields.qsl_via      = btn_udp_QSL_VIA->value();
	udp_fields.srx          = btn_udp_SerialIN->value();
	udp_fields.stx          = btn_udp_SerialOUT->value();

	udp_fields.check        = btn_udp_Check->value();
	udp_fields.xchg1        = btn_udp_XchgIn->value();
	udp_fields.myxchg       = btn_udp_MyXchg->value();
	udp_fields.county       = btn_udp_CNTY->value();
	udp_fields.continent    = btn_udp_CONT->value();
	udp_fields.cqz          = btn_udp_CQZ->value();
	udp_fields.dxcc         = btn_udp_DXCC->value();
	udp_fields.iota         = btn_udp_IOTA->value();
	udp_fields.ituz         = btn_udp_ITUZ->value();
	udp_fields.arrl_class   = btn_udp_Class->value();
	udp_fields.arrl_sect    = btn_udp_Section->value();
	udp_fields.cwss_serno   = btn_udp_cwss_serno->value();
	udp_fields.cwss_prec    = btn_udp_cwss_prec->value();
	udp_fields.cwss_check   = btn_udp_cwss_check->value();
	udp_fields.cwss_section = btn_udp_cwss_section->value();
	udp_fields.ten_ten      = btn_udp_1010->value();

	std::string pref;
	Fl_Preferences spref(HomeDir.c_str(), "w1hkj.com", "udp_fields");

	spref.set("udp_first_use", 0);

	spref.set("call", udp_fields.call);
	spref.set("mode", udp_fields.mode);
	spref.set("freq", udp_fields.freq);
	spref.set("band", udp_fields.band);
	spref.set("date_on", udp_fields.date_on);
	spref.set("time_on", udp_fields.time_on);
	spref.set("date_off", udp_fields.date_off);
	spref.set("time_off", udp_fields.time_off);
	spref.set("name", udp_fields.name);
	spref.set("qth", udp_fields.qth);
	spref.set("state", udp_fields.state);
	spref.set("province", udp_fields.province);
	spref.set("country", udp_fields.country);
	spref.set("rst_sent", udp_fields.rst_sent);
	spref.set("rst_rcvd", udp_fields.rst_rcvd);
	spref.set("tx_pwr", udp_fields.tx_pwr);
	spref.set("county", udp_fields.country);
	spref.set("dxcc", udp_fields.dxcc);
	spref.set("cqz", udp_fields.cqz);
	spref.set("iota", udp_fields.iota);
	spref.set("continent", udp_fields.continent);
	spref.set("ituz", udp_fields.ituz);
	spref.set("gridsquare", udp_fields.gridsquare);
	spref.set("qsl_rcvd", udp_fields.qsl_rcvd);
	spref.set("qsl_sent", udp_fields.qsl_sent);
	spref.set("eqsl_rcvd", udp_fields.eqsl_rcvd);
	spref.set("eqsl_sent", udp_fields.eqsl_sent);
	spref.set("lotw_rcvd", udp_fields.lotw_rcvd);
	spref.set("lotw_sent", udp_fields.lotw_sent);
	spref.set("qsl_via", udp_fields.qsl_via);
	spref.set("notes", udp_fields.notes);
	spref.set("srx", udp_fields.srx);
	spref.set("stx", udp_fields.stx);
	spref.set("xchg1", udp_fields.xchg1);
	spref.set("myxchg", udp_fields.myxchg);
	spref.set("arrl_class", udp_fields.arrl_class);
	spref.set("arrl_sect", udp_fields.arrl_sect);
	spref.set("op_call", udp_fields.op_call);
	spref.set("sta_call", udp_fields.sta_call);
	spref.set("mygrid", udp_fields.mygrid);
	spref.set("mycity", udp_fields.mycity);
	spref.set("check", udp_fields.check);
	spref.set("age", udp_fields.age);
	spref.set("ten_ten", udp_fields.ten_ten);
	spref.set("cwss_serno", udp_fields.cwss_serno);
	spref.set("cwss_prec", udp_fields.cwss_prec);
	spref.set("cwss_check", udp_fields.cwss_check);
	spref.set("cwss_section", udp_fields.cwss_section);

}

void load_udp_prefs()
{
	std::string pref;
	Fl_Preferences spref(HomeDir.c_str(), "w1hkj.com", "udp_fields");

	spref.get("udp_first_use", udp_first_use, udp_first_use);
	if (udp_first_use)
		return;

	spref.get("call", udp_fields.call, udp_fields.call);
	spref.get("mode", udp_fields.mode, udp_fields.mode);
	spref.get("freq", udp_fields.freq, udp_fields.freq);
	spref.get("band", udp_fields.band, udp_fields.band);
	spref.get("date_on", udp_fields.date_on, udp_fields.date_on);
	spref.get("time_on", udp_fields.time_on, udp_fields.time_on);
	spref.get("date_off", udp_fields.date_off, udp_fields.date_off);
	spref.get("time_off", udp_fields.time_off, udp_fields.time_off);
	spref.get("name", udp_fields.name, udp_fields.name);
	spref.get("qth", udp_fields.qth, udp_fields.qth);
	spref.get("state", udp_fields.state, udp_fields.state);
	spref.get("province", udp_fields.province, udp_fields.province);
	spref.get("country", udp_fields.country, udp_fields.country);
	spref.get("rst_sent", udp_fields.rst_sent, udp_fields.rst_sent);
	spref.get("rst_rcvd", udp_fields.rst_rcvd, udp_fields.rst_rcvd);
	spref.get("tx_pwr", udp_fields.tx_pwr, udp_fields.tx_pwr);
	spref.get("county", udp_fields.country, udp_fields.country);
	spref.get("dxcc", udp_fields.dxcc, udp_fields.dxcc);
	spref.get("cqz", udp_fields.cqz, udp_fields.cqz);
	spref.get("iota", udp_fields.iota, udp_fields.iota);
	spref.get("continent", udp_fields.continent, udp_fields.continent);
	spref.get("ituz", udp_fields.ituz, udp_fields.ituz);
	spref.get("gridsquare", udp_fields.gridsquare, udp_fields.gridsquare);
	spref.get("qsl_rcvd", udp_fields.qsl_rcvd, udp_fields.qsl_rcvd);
	spref.get("qsl_sent", udp_fields.qsl_sent, udp_fields.qsl_sent);
	spref.get("eqsl_rcvd", udp_fields.eqsl_rcvd, udp_fields.eqsl_rcvd);
	spref.get("eqsl_sent", udp_fields.eqsl_sent, udp_fields.eqsl_sent);
	spref.get("lotw_rcvd", udp_fields.lotw_rcvd, udp_fields.lotw_rcvd);
	spref.get("lotw_sent", udp_fields.lotw_sent, udp_fields.lotw_sent);
	spref.get("qsl_via", udp_fields.qsl_via, udp_fields.qsl_via);
	spref.get("notes", udp_fields.notes, udp_fields.notes);
	spref.get("srx", udp_fields.srx, udp_fields.srx);
	spref.get("stx", udp_fields.stx, udp_fields.stx);
	spref.get("xchg1", udp_fields.xchg1, udp_fields.xchg1);
	spref.get("myxchg", udp_fields.myxchg, udp_fields.myxchg);
	spref.get("arrl_class", udp_fields.arrl_class, udp_fields.arrl_class);
	spref.get("arrl_sect", udp_fields.arrl_sect, udp_fields.arrl_sect);
	spref.get("op_call", udp_fields.op_call, udp_fields.op_call);
	spref.get("sta_call", udp_fields.sta_call, udp_fields.sta_call);
	spref.get("mygrid", udp_fields.mygrid, udp_fields.mygrid);
	spref.get("mycity", udp_fields.mycity, udp_fields.mycity);
	spref.get("check", udp_fields.check, udp_fields.check);
	spref.get("age", udp_fields.age, udp_fields.age);
	spref.get("ten_ten", udp_fields.ten_ten, udp_fields.ten_ten);
	spref.get("cwss_serno", udp_fields.cwss_serno, udp_fields.cwss_serno);
	spref.get("cwss_prec", udp_fields.cwss_prec, udp_fields.cwss_prec);
	spref.get("cwss_check", udp_fields.cwss_check, udp_fields.cwss_check);
	spref.get("cwss_section", udp_fields.cwss_section, udp_fields.cwss_section);

}


void save_export_prefs()
{
}

void load_export_prefs()
{
}
