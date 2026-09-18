// The group service was written against the 2019 `ela_*` Carrier API and kept
// its own copy of that header. The current SDK still exports every ela_* name
// as an alias of the carrier_* symbol (carrier_deprecated.h), so the service
// now includes the SDK's headers and adds only the two things the private
// copy provided: the 4-argument send (no receipt) and a no-op log init.
//
// This is what puts the service on the AgentNet proto v2 core: the 16 MB bulk
// cap, the receipt timeout, and the client / profile fields it forwards to
// members (see buildAgentNetJson in CarrierRobot.cpp).
#ifndef CARRIER_COMPAT_H
#define CARRIER_COMPAT_H

#define DEPRECATED_NO_WARNINGS 1
#include <carrier.h>
#include <carrier_deprecated.h>

// Send without a receipt callback, as the old 4-argument API did.
#ifndef ela_send_friend_message_noreceipt
#define ela_send_friend_message_noreceipt(carrier, to, msg, len) \
    carrier_send_friend_message(reinterpret_cast<Carrier *>(carrier), (to), (msg), (len), NULL, NULL, NULL)
#endif

// Logging init was removed from the SDK; the service never depended on it.
#ifndef ela_log_init
#define ela_log_init(...) ((void)0)
#endif

#endif
