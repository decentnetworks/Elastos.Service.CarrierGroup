#ifndef CHATROBOT_ELA_CARRIER_COMPAT_H
#define CHATROBOT_ELA_CARRIER_COMPAT_H

#include <carrier.h>

// Backward-compatible 4-arg send API used by CarrierGroup sources.
#undef ela_send_friend_message
#define ela_send_friend_message(carrier, to, msg, len) \
    ela_send_friend_message((carrier), (to), (msg), (len), NULL, NULL, NULL)

// Logging API was removed in newer Carrier SDK; keep calls source-compatible.
#ifndef ela_log_init
#define ela_log_init(...) ((void)0)
#endif

#endif
