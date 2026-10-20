// ----------------------------------------------------------------------------
//  logger.h Remote Log Interface for fldigi
//
// Copyright W1HKJ, Dave Freese 2006
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

#ifndef _LOGGER_H
#define _LOGGER_H

// IPC interface to Xlog and fl_logbook

#define	LOG_MVERSION	"1"
#define	LOG_MKEY	1238
#define	LOG_MTYPE	88

#define	LOG_MSG_LEN	1024

#include "qso_db.h"

typedef struct {
	long mtype;
	char mtext[LOG_MSG_LEN];
} msgtype;

extern void submit_log();
extern void submit_record(cQsoRec &);

extern	char logdate[];
extern  char logtime[];
extern  char adifdate[];
extern	const char *logmode;

extern std::string str_lotw;

// ---------------------------------------------------------------------
// UDP log record export

struct EXPORT_LOG_FIELDS {
	int call;
	int mode;
	int freq;
	int band;
	int date_on;
	int time_on;
	int date_off;
	int time_off;
	int name;
	int qth;
	int state;
	int province;
	int country;
	int rst_sent;
	int rst_rcvd;
	int tx_pwr;
	int county;
	int dxcc;
	int cqz;
	int iota;
	int continent;
	int ituz;
	int gridsquare;
	int qsl_rcvd;
	int qsl_sent;
	int eqsl_rcvd;
	int eqsl_sent;
	int lotw_rcvd;
	int lotw_sent;
	int qsl_via;
	int notes;
	int srx;
	int stx;
	int xchg1;
	int myxchg;
	int arrl_class;
	int arrl_sect;
	int op_call;
	int sta_call;
	int mygrid;
	int mycity;
	int check;
	int age;
	int ten_ten;
	int cwss_serno;
	int cwss_prec;
	int cwss_check;
	int cwss_section;

	EXPORT_LOG_FIELDS() {
		call =
		mode =
		freq =
		band =
		date_on =
		time_on =
		date_off =
		time_off =
		name =
		qth =
		state =
		province =
		country =
		rst_sent =
		rst_rcvd =
		tx_pwr =
		county =
		dxcc =
		cqz =
		iota =
		continent =
		ituz =
		gridsquare =
		qsl_rcvd =
		qsl_sent =
		eqsl_rcvd =
		eqsl_sent =
		lotw_rcvd =
		lotw_sent =
		qsl_via =
		notes =
		srx =
		stx =
		xchg1 =
		myxchg =
		arrl_class =
		arrl_sect =
		op_call =
		sta_call =
		mygrid =
		mycity =
		check =
		age =
		ten_ten =
		cwss_serno =
		cwss_prec =
		cwss_check =
		cwss_section = 1;
	}

};

extern EXPORT_LOG_FIELDS udp_fields;
extern void save_udp_prefs();
extern void load_udp_prefs();

extern EXPORT_LOG_FIELDS cloud_fields;
extern void save_cloud_prefs();
extern void load_cloud_prefs();

// ---------------------------------------------------------------------

#endif
