/*
 * SerialPortInterface.h
 *
 *  Created on: Feb 15, 2015
 *      Author: diego
 */

#ifndef DATALINKSTREAM_H_
#define DATALINKSTREAM_H_


#include <Stream.h>

#include <stdio.h>   /* Standard input/output definitions */

#include <string>

#include <fcntl.h>   /* File control definitions */
#include <errno.h>   /* Error number definitions */
#include <termios.h> /* POSIX terminal control definitions */

#include <sys/time.h> /*para timeout*/
#include <sys/ioctl.h>

namespace radiotransmission {

class DataLinkStream : public Stream {
public:

	struct PortSettings
	{
        std::string file;

		PortSettings()
		{
			file = "/tmp/radiorx";
		}

		PortSettings(std::string f)
		{
			file = f;
		}
	};

	DataLinkStream();
	DataLinkStream(const char *);
	DataLinkStream(DataLinkStream::PortSettings);
	bool Open();
	void Close();

	int Read(void *, uint32_t, unsigned long msTimeout=0);
	int Write(const void *, uint32_t, uint32_t msTimeout=0);

	Stream & operator >> (uint8_t &);
	Stream & operator >> (char &);
	Stream & operator >> (uint16_t &);
	Stream & operator >> (uint32_t &) ;
	Stream & operator << (uint8_t);
	Stream & operator << (const char * str);
	int Available();

	bool IsOpen();
	//void TimeoutMode(bool);
	void FlushInput();
	void FlushOutput();
	void FlushIO();

	void SetTimeout(unsigned long ms);

protected:
	PortSettings portSettings;
	int fd;
    bool _open = false;
    char *  port;


};

} /* namespace radiotransmission */

#endif /* SEDATALINKSTREAM_H_ */
