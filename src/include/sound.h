// ----------------------------------------------------------------------------
//
//      sound.h
//
// Copyright (C) 2006-2007
//              Dave Freese, W1HKJ
//
// Copyright (C) 2007-2009
//              Stelios Bounanos, M0GLD
//
// Modified 2026-10-08: modem audio is SoundTCI. SoundNull remains so a
// launch with TCI audio off does not open a sound card. The sound-card
// device, file capture, and alert playback paths are gone.
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

#ifndef _SOUND_H
#define _SOUND_H

#include <exception>
#include <string>
#include <cstring>
#include <climits>

#define SCBLOCKSIZE 512

class SndException : public std::exception
{
public:
	SndException(int err_ = 0)
		: err(err_), msg(std::string("Sound error: ") + err_to_str(err_))
	{ }
	SndException(const char* msg_)
		: err(1), msg(msg_)
	{ }
	SndException(int err_, const std::string& msg_) : err(err_), msg(msg_) { }
	virtual ~SndException() throw() { }

	const char*	what(void) const throw() { return msg.c_str(); }
	int		error(void) const { return err; }

protected:
	const char* err_to_str(int e) { return strerror(e); }

	int		err;
	std::string	msg;
};


class SoundBase {
protected:
	int		sample_frequency;

public:
	SoundBase();
	virtual ~SoundBase();
	virtual int	Open(int mode, int freq = 8000) = 0;
	virtual void    Close(unsigned dir = UINT_MAX) = 0;
	virtual void    Abort(unsigned dir = UINT_MAX) = 0;
	virtual size_t	Write(double *, size_t) = 0;
	virtual size_t	Write_stereo(double *, double *, size_t) = 0;
	virtual size_t	Read(float *, size_t) = 0;
	virtual void    flush(unsigned dir = UINT_MAX) = 0;
	virtual bool	must_close(int dir = 0) = 0;
};


// Silent device. Read returns zeros. Write paces the modem loop.
class SoundNull : public SoundBase
{
public:
	int	Open(int mode, int freq = 44100) { sample_frequency = freq; return 0; }
	void    Close(unsigned) { }
	void    Abort(unsigned) { }
	size_t	Write(double* buf, size_t count);
	size_t	Write_stereo(double* bufleft, double* bufright, size_t count);
	size_t	Read(float *buf, size_t count);
	bool	must_close(int dir = 0) { return false; }
	void	flush(unsigned);
};

// Modem audio carried as TCI binary frames (Zeus / ExpertSDR3).
class SoundTCI : public SoundBase
{
public:
	SoundTCI();
	int	Open(int mode, int freq = 8000);
	void	Close(unsigned dir = UINT_MAX);
	void	Abort(unsigned dir = UINT_MAX);
	size_t	Write(double* buf, size_t count);
	size_t	Write_stereo(double* bufleft, double* bufright, size_t count);
	size_t	Read(float *buf, size_t count);
	bool	must_close(int dir = 0) { return false; }
	void	flush(unsigned dir = UINT_MAX);
private:
	int	open_mode;
};

#endif // SOUND_H
