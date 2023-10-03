#include "Common/NATPMP.h"
#include "Common/Logging/Log.h"
#include <natpmp.h>
#include <thread>

#ifndef _WIN32
#include <arpa/inet.h>
#endif

static u16 s_mapped = 0;
static natpmp_t s_natpmp;
static std::thread s_thread;

// called from ---NATPMP--- thread
static int GetNatpmpResponse(natpmpresp_t* response)
{
  int result;
  int i = 0;
  do
  {
    fd_set fds;
    struct timeval timeout;
    FD_ZERO(&fds);
    FD_SET(s_natpmp.s, &fds);
    result = getnatpmprequesttimeout(&s_natpmp, &timeout);
    if (result != 0)
      break;

    select(FD_SETSIZE, &fds, NULL, NULL, &timeout);
    result = readnatpmpresponseorretry(&s_natpmp, response);
    i++;
  } while (i < 3 && result == NATPMP_TRYAGAIN);
  // 3 tries takes 1750ms. Doesn't seem good to wait longer than that.

  return result;
}

// called from ---NATPMP--- thread
// discovers the NAT-PMP/PCP gateway
static bool InitNATPMP()
{
  static bool s_natpmpInited = false;
  static bool s_natpmpError = false;

  // Don't init if already inited
  if (s_natpmpInited)
    return true;

  // Don't init if it failed before
  if (s_natpmpError)
    return false;

  int result = initnatpmp(&s_natpmp, /* forcegw */ 0, /* forcedgw */ 0);
  if (result != 0)
  {
    WARN_LOG_FMT(NETPLAY, "[NAT-PMP] initnatpmp failed: {}", result);
    s_natpmpError = true;
    return false;
  }
  result = sendpublicaddressrequest(&s_natpmp);
  if (result != 2)
  {
    WARN_LOG_FMT(NETPLAY, "[NAT-PMP] sendpublicaddressrequest failed: {}", result);
    s_natpmpError = true;
    return false;
  }
  natpmpresp_t response;
  result = GetNatpmpResponse(&response);
  if (result != 0)
  {
    WARN_LOG_FMT(NETPLAY, "[NAT-PMP] publicaddress error: {}", result);
    s_natpmpError = true;
    return false;
  }

  WARN_LOG_FMT(NETPLAY, "[NAT-PMP] Inited");
  s_natpmpInited = true;
  return true;
}

// called from ---NATPMP--- thread
static void UnmapPort()
{
  sendnewportmappingrequest(&s_natpmp, NATPMP_PROTOCOL_UDP, s_mapped, s_mapped, 0);
  natpmpresp_t response;
  GetNatpmpResponse(&response);
  s_mapped = 0;
}

// called from ---NATPMP--- thread
static bool MapPort(const u16 port)
{
  if (s_mapped > 0 && s_mapped != port)
    UnmapPort();

  int result = sendnewportmappingrequest(&s_natpmp, NATPMP_PROTOCOL_UDP, port, port, 604800);
  if (result != 12)
  {
    WARN_LOG_FMT(NETPLAY, "[NAT-PMP] sendnewportmappingrequest failed: {}", result);
    return false;
  }
  natpmpresp_t response;
  result = GetNatpmpResponse(&response);
  if (result != 0)
  {
    WARN_LOG_FMT(NETPLAY, "[NAT-PMP] portmapping error: {}", result);
    return false;
  }

  s_mapped = port;
  return true;
}

static void MapPortThread(const u16 port)
{
  if (InitNATPMP() && MapPort(port))
  {
    NOTICE_LOG_FMT(NETPLAY, "[NAT-PMP] Successfully mapped port {}.", port);
    return;
  }

  WARN_LOG_FMT(NETPLAY, "[NAT-PMP] Failed to map port {}.", port);
}

static void UnmapPortThread()
{
  if (s_mapped > 0)
    UnmapPort();
}

void NATPMP::TryPortmappingBlocking(u16 port)
{
  if (s_thread.joinable())
    s_thread.join();
  s_thread = std::thread(&MapPortThread, port);
  s_thread.join();
}

void NATPMP::StopPortmapping()
{
  if (s_thread.joinable())
    s_thread.join();
  s_thread = std::thread(&UnmapPortThread);
  s_thread.join();
}
