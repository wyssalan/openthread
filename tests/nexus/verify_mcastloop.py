#!/usr/bin/env python3
#
#  Copyright (c) 2026, The OpenThread Authors.
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are met:
#  1. Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#  2. Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#  3. Neither the name of the copyright holder nor the
#     names of its contributors may be used to endorse or promote products
#     derived from this software without specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
#  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
#  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
#  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
#  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
#  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
#  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#  POSSIBILITY OF SUCH DAMAGE.
#

import os
import sys

CUR_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.append(CUR_DIR)

import verify_utils
from pktverify.addrs import Ipv6Addr


def verify(pv):
    # Verify multicast loop delivery for a sleepy end device using a mesh-local
    # prefix-based multicast address.
    #
    # Topology
    # - Router
    # - SED_A
    # - SED_B

    pkts = pv.pkts
    pv.summary.show()

    ROUTER_RLOC16 = pv.vars['ROUTER_RLOC16']
    SED_A_RLOC16 = pv.vars['SED_A_RLOC16']
    SED_B_RLOC16 = pv.vars['SED_B_RLOC16']

    mesh_local_prefix = pv.vars['mesh_local_prefix']
    prefix = mesh_local_prefix.split('/')[0]
    multicast_addr = Ipv6Addr(bytearray([0xff, 0x35, 0x00, 0x30]) + Ipv6Addr(prefix)[:8] + bytearray([0, 0, 0, 1]))

    # Step 1: Forward to peer sleepy child
    # - Description: Verify that the router forwards the multicast packet to SED_B.
    # - Pass Criteria: SED_B receives the expected multicast destination address.
    print("Step 1: Forward to peer sleepy child")
    pkts.filter_wpan_src16(ROUTER_RLOC16).\
        filter_wpan_dst16(SED_B_RLOC16).\
        filter_ipv6_dst(multicast_addr).\
        must_next()

    # Step 2: Suppress over-the-air loopback
    # - Description: Verify that the packet is not forwarded back over the air to SED_A.
    # - Pass Criteria: No over-the-air packet from the router to SED_A matches the multicast destination address.
    print("Step 2: Suppress over-the-air loopback")
    pkts.filter_wpan_src16(ROUTER_RLOC16).\
        filter_wpan_dst16(SED_A_RLOC16).\
        filter_ipv6_dst(multicast_addr).\
        must_not_next()


if __name__ == '__main__':
    verify_utils.run_main(verify)
