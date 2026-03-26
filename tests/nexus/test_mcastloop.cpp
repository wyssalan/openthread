/*
 *  Copyright (c) 2026, The OpenThread Authors.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>

#include "common/as_core_type.hpp"
#include "mac/data_poll_sender.hpp"
#include "net/ip6.hpp"
#include "platform/nexus_core.hpp"
#include "platform/nexus_node.hpp"
#include "platform/nexus_utils.hpp"
#include "thread/mle.hpp"

namespace ot {
namespace Nexus {

/** Time to advance for a node to form a network, in milliseconds. */
static constexpr uint32_t kFormNetworkTime = 15 * 1000;

/** Time to advance for SEDs to attach, in milliseconds. */
static constexpr uint32_t kAttachSedTime = 20 * 1000;

/** Time to advance for multicast subscriptions to propagate, in milliseconds. */
static constexpr uint32_t kSubscriptionSyncTime = 5 * 1000;

/** Time to advance for UDP sockets to become ready, in milliseconds. */
static constexpr uint32_t kSocketSetupTime = 2 * 1000;

/** Time to advance after sending a multicast message, in milliseconds. */
static constexpr uint32_t kPostSendTime = 2 * 1000;

/** UDP port used by the multicast test. */
static constexpr uint16_t kUdpPort = 1234;

static bool sSedAReceived = false;
static bool sSedBReceived = false;

void HandleUdpReceive(void                                 *aContext,
                      [[maybe_unused]] otMessage           *aMessage,
                      [[maybe_unused]] const otMessageInfo *aMessageInfo)
{
    const char *name = static_cast<const char *>(aContext);

    if (strcmp(name, "SED_A") == 0)
    {
        sSedAReceived = true;
    }
    else if (strcmp(name, "SED_B") == 0)
    {
        sSedBReceived = true;
    }
}

void TestMulticastLoop(void)
{
    /**
     * Verify multicast loop delivery for a sleepy end device using a site-local
     * prefix-based multicast address.
     *
     * Topology
     * - Router
     * - SED_A
     * - SED_B
     */

    Core  nexus;
    Node &router = nexus.CreateNode();
    Node &sedA   = nexus.CreateNode();
    Node &sedB   = nexus.CreateNode();

    router.SetName("ROUTER");
    sedA.SetName("SED_A");
    sedB.SetName("SED_B");

    Instance::SetLogLevel(kLogLevelNote);

    /**
     * Step 1: Form a Thread network with one router and attach two sleepy end devices.
     */
    Log("Step 1: Form Thread network");

    router.Form();
    nexus.AdvanceTime(kFormNetworkTime);

    sedA.Join(router, Node::kAsSed);
    sedB.Join(router, Node::kAsSed);
    nexus.AdvanceTime(kAttachSedTime);

    /**
     * Step 2: Configure multicast listeners
     * - Description: Subscribe both sleepy end devices to a mesh-local prefix-based multicast address.
     * - Pass Criteria: Both subscriptions succeed.
     */
    Log("Step 2: Configure multicast listeners");

    const Ip6::NetworkPrefix &meshLocalPrefix = sedA.Get<Mle::Mle>().GetMeshLocalPrefix();

    otIp6Address mcastAddr;
    memset(&mcastAddr, 0, sizeof(mcastAddr));
    mcastAddr.mFields.m8[0] = 0xff;
    mcastAddr.mFields.m8[1] = 0x35;
    mcastAddr.mFields.m8[2] = 0x00;
    mcastAddr.mFields.m8[3] = 0x30;
    memcpy(&mcastAddr.mFields.m8[4], meshLocalPrefix.m8, sizeof(meshLocalPrefix.m8));
    mcastAddr.mFields.m8[15] = 0x01;

    {
        char buf[OT_IP6_ADDRESS_STRING_SIZE];
        otIp6AddressToString(&mcastAddr, buf, sizeof(buf));
        Log("Multicast address: %s", buf);
    }

    SuccessOrQuit(otIp6SubscribeMulticastAddress(&sedA.GetInstance(), &mcastAddr));
    SuccessOrQuit(otIp6SubscribeMulticastAddress(&sedB.GetInstance(), &mcastAddr));
    nexus.AdvanceTime(kSubscriptionSyncTime);

    /**
     * Step 3: Send multicast packet
     * - Description: Open UDP sockets on both sleepy end devices and send a multicast packet from SED_A with
     *   multicast loop enabled.
     * - Pass Criteria: The send operation succeeds.
     */
    Log("Step 3: Send multicast packet");

    otUdpSocket socketA;
    otUdpSocket socketB;
    otSockAddr  bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.mPort = kUdpPort;

    SuccessOrQuit(otUdpOpen(&sedA.GetInstance(), &socketA, HandleUdpReceive, (void *)"SED_A"));
    SuccessOrQuit(otUdpOpen(&sedB.GetInstance(), &socketB, HandleUdpReceive, (void *)"SED_B"));
    SuccessOrQuit(otUdpBind(&sedA.GetInstance(), &socketA, &bindAddr, OT_NETIF_THREAD_HOST));
    SuccessOrQuit(otUdpBind(&sedB.GetInstance(), &socketB, &bindAddr, OT_NETIF_THREAD_HOST));
    nexus.AdvanceTime(kSocketSetupTime);

    otMessageInfo msgInfo;
    memset(&msgInfo, 0, sizeof(msgInfo));
    msgInfo.mPeerAddr      = mcastAddr;
    msgInfo.mPeerPort      = kUdpPort;
    msgInfo.mMulticastLoop = true;

    otMessage *msg = otUdpNewMessage(&sedA.GetInstance(), nullptr);
    VerifyOrQuit(msg != nullptr);

    static const uint8_t kPayload[] = "Sending a Multicast larger than realm-local";
    SuccessOrQuit(otMessageAppend(msg, kPayload, sizeof(kPayload)));
    SuccessOrQuit(otUdpSend(&sedA.GetInstance(), &socketA, msg, &msgInfo));
    nexus.AdvanceTime(kPostSendTime);

    sedB.Get<DataPollSender>().SendDataPoll();
    sedA.Get<DataPollSender>().SendDataPoll();
    nexus.AdvanceTime(kPostSendTime);

    /**
     * Step 4: Confirm multicast delivery
     * - Description: Verify that both sleepy end devices receive the multicast packet.
     * - Pass Criteria: SED_A receives the looped multicast packet and SED_B receives the forwarded multicast packet.
     */
    Log("Step 4: Confirm multicast delivery");

    VerifyOrQuit(sSedAReceived);
    VerifyOrQuit(sSedBReceived);

    nexus.SaveTestInfo("test_mcastloop.json");
}

} // namespace Nexus
} // namespace ot

int main(void)
{
    ot::Nexus::TestMulticastLoop();
    printf("All tests passed\n");
    return 0;
}
