#ifndef DCCOMMS_ICOMMSDEVICE_H
#define DCCOMMS_ICOMMSDEVICE_H

#include <cpplogging/cpplogging.h>
#include <dccomms/Packet.h>

namespace dccomms {

using namespace cpplogging;
class CommsDevice : public virtual Loggable {
public:
  CommsDevice();

  virtual CommsDevice &operator>>(const PacketPtr &dlf) = 0;
  virtual CommsDevice &operator<<(const PacketPtr &dlf) = 0;

  virtual bool BusyTransmitting();
  virtual void SetTimeout(unsigned long);
  virtual unsigned long GetTimeout();

  virtual bool Open();
  virtual void Close();

protected:
  unsigned long _timeout = 0;
};

} /* namespace dccomms */

#endif // DCCOMMS_ICOMMSDEVICE_H