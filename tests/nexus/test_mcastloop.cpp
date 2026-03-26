#include <stdio.h>
#include <string.h>

#include "net/ip6.hpp"
#include "platform/nexus_core.hpp"
#include "platform/nexus_node.hpp"
#include "platform/nexus_utils.hpp"
#include "thread/mle.hpp"

namespace ot {
namespace Nexus {

static bool sSedAReceived = false;
static bool sSedBReceived = false;

// UDP receive callback for the SED nodes
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
    Core  nexus;
    Node &router = nexus.CreateNode();
    Node &sedA   = nexus.CreateNode();
    Node &sedB   = nexus.CreateNode();

    router.SetName("ROUTER");
    sedA.SetName("SED_A");
    sedB.SetName("SED_B");

    // Initialize simulation
    nexus.AdvanceTime(0);
    Instance::SetLogLevel(kLogLevelNote);

    // --- Network formation ---
    router.Form();
    nexus.AdvanceTime(15000); // allow network to form
    sedA.Join(router, Node::kAsSed);
    sedB.Join(router, Node::kAsSed);
    nexus.AdvanceTime(20000); // allow SEDs to join

    // --- MPL multicast address (ff35:30:<MLP>::1, scope 5 > realm-local) ---
    const ot::Ip6::NetworkPrefix &mlp = sedA.Get<ot::Mle::Mle>().GetMeshLocalPrefix();

    otIp6Address mcastAddr;
    memset(&mcastAddr, 0, sizeof(mcastAddr));
    mcastAddr.mFields.m8[0] = 0xff;
    mcastAddr.mFields.m8[1] = 0x35;
    mcastAddr.mFields.m8[2] = 0x00;
    mcastAddr.mFields.m8[3] = 0x30;
    for (int i = 0; i < 8; i++) mcastAddr.mFields.m8[4 + i] = mlp.m8[i];
    mcastAddr.mFields.m8[15] = 0x01;

    {
        char buf[OT_IP6_ADDRESS_STRING_SIZE];
        otIp6AddressToString(&mcastAddr, buf, sizeof(buf));
        Log("Multicast address: %s", buf);
    }

    // --- Subscribe the SEDs to the multicast group ---
    SuccessOrQuit(otIp6SubscribeMulticastAddress(&sedA.GetInstance(), &mcastAddr));
    SuccessOrQuit(otIp6SubscribeMulticastAddress(&sedB.GetInstance(), &mcastAddr));
    nexus.AdvanceTime(5000); // allow subscriptions to propagate

    // --- Open UDP sockets using the C-API ---
    otUdpSocket socketA, socketB;
    otSockAddr  bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.mPort = 1234;

    SuccessOrQuit(otUdpOpen(&sedA.GetInstance(), &socketA, HandleUdpReceive, (void *)"SED_A"));
    SuccessOrQuit(otUdpOpen(&sedB.GetInstance(), &socketB, HandleUdpReceive, (void *)"SED_B"));
    SuccessOrQuit(otUdpBind(&sedA.GetInstance(), &socketA, &bindAddr, OT_NETIF_THREAD_HOST));
    SuccessOrQuit(otUdpBind(&sedB.GetInstance(), &socketB, &bindAddr, OT_NETIF_THREAD_HOST));
    nexus.AdvanceTime(2000); // allow sockets to be ready

    otMessageInfo msgInfo;
    memset(&msgInfo, 0, sizeof(msgInfo));
    msgInfo.mPeerAddr      = mcastAddr;
    msgInfo.mPeerPort      = 1234;
    msgInfo.mMulticastLoop = true; // triggers loopback for SED

    otMessage *msg = otUdpNewMessage(&sedA.GetInstance(), nullptr);
    VerifyOrQuit(msg != nullptr);

    static const uint8_t kPayload[] = "Sending a Multicast larger than realm-local";
    SuccessOrQuit(otMessageAppend(msg, kPayload, sizeof(kPayload)));

    SuccessOrQuit(otUdpSend(&sedA.GetInstance(), &socketA, msg, &msgInfo));
    nexus.AdvanceTime(2000);

    sedB.Get<DataPollSender>().SendDataPoll();
    sedA.Get<DataPollSender>().SendDataPoll();

    nexus.AdvanceTime(2000);

    // --- Verify both SEDs received the multicast ---
    VerifyOrQuit(sSedAReceived);
    VerifyOrQuit(sSedBReceived);

    nexus.SaveTestInfo("test_mcastloop.json");
    Log("=== Test completed successfully ===");
}

} // namespace Nexus
} // namespace ot

int main(void)
{
    ot::Nexus::TestMulticastLoop();
    printf("All tests passed\n");
    return 0;
}
