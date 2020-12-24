/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define TLOG_TAG "confirmationui"

#include <lib/tipc/tipc.h>
#include <lk/err_ptr.h>
#include <lk/macros.h>
#include <stdlib.h>
#include <trusty_ipc.h>
#include <trusty_log.h>
#include <uapi/err.h>

#include <algorithm>

#include "trusty_operation.h"

#include <lib/keymaster/keymaster.h>

#define CONFIRMATIONUI_PORT_NAME "com.android.trusty.confirmationui"

/*
 * Must be kept in sync with HAL service (see TrustyApp.cpp)
 */
static constexpr size_t kPacketSize = 0x1000 - 32;

/*
 * Must be kept in sync with HAL service (see TrustyApp.cpp)
 */
enum class PacketType : uint32_t {
    SND,
    RCV,
    ACK,
};

static const char* toString(PacketType t) {
    switch (t) {
    case PacketType::SND:
        return "SND";
    case PacketType::RCV:
        return "RCV";
    case PacketType::ACK:
        return "ACK";
    default:
        return "UNKNOWN";
    }
}

/*
 * Must be kept in sync with HAL service (see TrustyApp.cpp)
 */
struct PacketHeader {
    PacketType type;
    uint32_t remaining;
};

static constexpr const size_t kMessageSize = 0x2000;  // 8K

enum class IpcState {
    SENDING,
    RECEIVING,
    DESYNC,
};

const char* toString(IpcState s) {
    switch (s) {
    case IpcState::SENDING:
        return "SENDING";
    case IpcState::RECEIVING:
        return "RECEIVING";
    case IpcState::DESYNC:
        return "DESYNC";
    default:
        return "Unknown";
    }
}

static bool get_auth_token_key(teeui::AuthTokenKey& authKey) {
    long rc = keymaster_open();

    if (rc < 0) {
        return false;
    }

    keymaster_session_t session = (keymaster_session_t)rc;
    uint8_t* key = nullptr;
    uint32_t local_length = 0;
    rc = keymaster_get_auth_token_key(session, &key, &local_length);
    keymaster_close(session);
    TLOGD("%s, key length = %u\n", __func__, local_length);
    if (local_length != teeui::kAuthTokenKeySize) {
        return false;
    }
    if (rc == NO_ERROR) {
        memcpy(authKey.data(), key, teeui::kAuthTokenKeySize);
    } else {
        return false;
    }

    return true;
}

static void port_handler(const struct uevent* event, void* priv) {
    int rc;
    struct uuid peer_uuid;
    handle_t channel;

    TLOGD("Entering port handler %u\n", event->event);

    tipc_handle_port_errors(event);

    if (event->event & IPC_HANDLE_POLL_READY) {
        rc = accept(event->handle, &peer_uuid);
        if (rc < 0) {
            TLOGE("%s: failed (%d) to accept on port\n", __func__, rc);
            return;
        }
        TLOGD("Accepted connection\n");
        channel = (handle_t)rc;

        uint8_t message_buffer[kMessageSize];
        uint8_t* const aligned_message =
                (uint8_t*)round_up((uintptr_t)&message_buffer[0], 8);
        const size_t aligned_message_size =
                kMessageSize - (aligned_message - &message_buffer[0]);
        uint32_t mpos = 0;
        uint32_t msize = aligned_message_size;
        TrustyOperation op;

#if defined(PLATFORM_GENERIC_ARM64)
        /* Use the test key for emulator */
        constexpr const auto kTestKey = teeui::AuthTokenKey::fill(
                static_cast<uint8_t>(teeui::TestKeyBits::BYTE));
        op.setHmacKey(kTestKey);
#else
        teeui::AuthTokenKey authKey;
        if (get_auth_token_key(authKey) == true) {
            TLOGD("%s, get auth token key successfully\n", __func__);
        } else {
            TLOGE("%s, get auth token key failed\n", __func__);
            /* Abort operation and free all resources */
            op.abort();
            close(channel);
            return;
        }
        op.setHmacKey(authKey);
#endif

        IpcState state = IpcState::RECEIVING;

        while (true) {
            uevent_t event = UEVENT_INITIAL_VALUE(evt);
            TLOGD("Waiting (state: %s)\n", toString(state));
            rc = wait(channel, &event, INFINITE_TIME);
            if (rc < 0) {
                TLOGI("Wait returned error %d", rc);
                break;
            }
            TLOGD("Returned from wait with %u\n", event.event);

            tipc_handle_chan_errors(&event);
            if (event.event & IPC_HANDLE_POLL_HUP) {
                TLOGI("Got HUP\n");
                break;
            }
            if (!(event.event & IPC_HANDLE_POLL_MSG))
                continue;

            // Handle message

            PacketHeader header{};
            constexpr const size_t header_size = sizeof(PacketHeader);
            constexpr const uint32_t max_payload_size =
                    kPacketSize - header_size;
            int rc = tipc_recv_hdr_payload(channel, &header, header_size,
                                           &aligned_message[mpos],
                                           aligned_message_size - mpos);
            TLOGD("Got header msg type: %u remaining %u rc %d\n", header.type,
                  header.remaining, rc);
            if (rc < 0) {
                TLOGE("Error reading command %d\n", rc);
                break;
            }

            switch (header.type) {
            case PacketType::SND: {
                if (state != IpcState::RECEIVING) {
                    state = IpcState::DESYNC;
                    break;
                }
                // We are receiving SND packets
                size_t body_size = rc - header_size;
                mpos += body_size;
                header.type = PacketType::ACK;
                header.remaining -= body_size;
                rc = tipc_send1(channel, &header, header_size);
                if (rc != header_size) {
                    TLOGE("Failed to send ACK %d\n", rc);
                    state = IpcState::DESYNC;
                    break;
                }
                if (header.remaining == 0) {
                    // we got a full message

                    // handleMsg reads the message from the first buffer, then
                    // writes the reponse to the second buffer. The last
                    // argument is the second buffer size (in) and the written
                    // response size (out);
                    msize = aligned_message_size;

                    TLOGD("Calling event handler\n");
                    op.handleMsg(&aligned_message[0], mpos, &aligned_message[0],
                                 &msize);
                    TLOGD("Returned from event handler \n");

                    // the response starts at message[0]
                    mpos = 0;
                    state = IpcState::SENDING;
                }
                break;
            }
            case PacketType::RCV: {
                if (state != IpcState::SENDING) {
                    state = IpcState::DESYNC;
                    break;
                }
                // We are sending RCV packets
                header.remaining = msize - mpos;
                header.type = PacketType::ACK;
                size_t body_size = std::min(max_payload_size, header.remaining);
                rc = tipc_send2(channel, &header, header_size,
                                &aligned_message[mpos], body_size);
                mpos += body_size;
                if (mpos == msize) {
                    TLOGD("complete response sent\n");
                    // we sent the full response -> switch to expecting the next
                    // message.
                    state = IpcState::RECEIVING;
                    mpos = 0;
                    msize = 0;
                }
                break;
            }
            case PacketType::ACK:
            default:
                state = IpcState::DESYNC;
            }
            if (state == IpcState::DESYNC) {
                TLOGE("Protocol out of sync\n");
                break;
            }
        }

        TLOGD("Leaving session loop\n");
        // Abort operation and free all resources
        op.abort();
        close(channel);
    }
}

int main(void) {
    int rc;
    handle_t port;

    TLOGI("Initializing ConfirmationUI app\n");

    rc = port_create(CONFIRMATIONUI_PORT_NAME, 1, 4096,
                     IPC_PORT_ALLOW_NS_CONNECT);
    if (rc < 0) {
        TLOGE("%s: failed (%d) create port\n", __func__, rc);
        return rc;
    }
    port = (handle_t)rc;

    uevent_t event = UEVENT_INITIAL_VALUE(evt);

    do {
        rc = wait(port, &event, INFINITE_TIME);
        TLOGD("Got a connection\n");
        port_handler(&event, nullptr);
    } while (rc == 0);

    TLOGE("wait on port returned unexpected %d\n", rc);
    close(port);

    return rc;
}
